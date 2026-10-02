#include "net/browser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http.h"

#define LIST_MAX (64 * 1024) // the lobby's list: 256 servers are under 6 KB
#define QUERY_RESEND 1.0     // seconds before a server is asked again
#define QUERY_TRIES 3        // and how many times, before it is given up on

// --- the list, on a thread of its own ------------------------------------------------

struct BrowserFetch {
    SDL_Thread *thread;
    SDL_atomic_t done;
    char url[512];
    // the thread's, read once `done`
    HttpResult result;
    char *body;
    char error[160];
};

static int fetch_run(void *arg)
{
    BrowserFetch *f = arg;
    size_t size;
    f->result = http_get(f->url, LIST_MAX, &f->body, &size, f->error, sizeof f->error);
    if (f->result == HTTP_NOT_FOUND) snprintf(f->error, sizeof f->error, "the lobby has no list at %s", f->url);
    SDL_AtomicSet(&f->done, 1);
    return 0;
}

static void fetch_free(BrowserFetch *f)
{
    SDL_WaitThread(f->thread, NULL);
    free(f->body);
    free(f);
}

// --- the browser ---------------------------------------------------------------------

void browser_init(Browser *b) { *b = (Browser){.socket = ENET_SOCKET_NULL}; }

static void stop(Browser *b)
{
    if (b->fetch) {
        fetch_free(b->fetch); // waits out the request: a refresh while fetching is rare
        b->fetch = NULL;
    }
    if (b->socket != ENET_SOCKET_NULL) {
        enet_socket_destroy(b->socket);
        b->socket = ENET_SOCKET_NULL;
    }
}

void browser_refresh(Browser *b, const char *lobby_url, double now)
{
    stop(b);
    b->count = b->answered = 0;
    b->error[0] = '\0';
    b->refreshes++;
    b->started = now;
    BrowserFetch *f = calloc(1, sizeof *f);
    size_t n = strlen(lobby_url);
    while (n && lobby_url[n - 1] == '/') n--;
    if (f) snprintf(f->url, sizeof f->url, "%.*s/v1/servers.txt", (int)n, lobby_url);
    if (!f || !n || !(f->thread = SDL_CreateThread(fetch_run, "browser", f))) {
        free(f);
        b->state = BROWSER_FAILED;
        snprintf(b->error, sizeof b->error, n ? "the list couldn't be asked for" : "no lobby: cl_lobby is empty");
        return;
    }
    b->fetch = f;
    b->state = BROWSER_FETCHING;
}

// The list has come: every server on it asked the query.
static void take_list(Browser *b, double now)
{
    BrowserFetch *f = b->fetch;
    b->fetch = NULL;
    if (f->result != HTTP_OK) {
        b->state = BROWSER_FAILED;
        snprintf(b->error, sizeof b->error, "%s", f->error);
        fetch_free(f);
        return;
    }
    QueryAddress list[BROWSER_MAX];
    b->count = query_parse_list(f->body, list, BROWSER_MAX);
    fetch_free(f);
    uint32_t base = (uint32_t)SDL_GetPerformanceCounter() * 2654435761u;
    for (int i = 0; i < b->count; i++)
        b->servers[i] = (BrowserServer){.address = list[i], .nonce = base ^ ((uint32_t)i * 0x9E3779B9u), .sent = -1e9};
    b->socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (b->socket == ENET_SOCKET_NULL) {
        b->state = BROWSER_FAILED;
        snprintf(b->error, sizeof b->error, "no socket to ask the servers with");
        return;
    }
    enet_socket_set_option(b->socket, ENET_SOCKOPT_NONBLOCK, 1);
    b->started = now;
    b->state = b->count ? BROWSER_QUERYING : BROWSER_DONE;
}

// Every answer waiting on the socket, each matched to the server that was asked.
static void take_answers(Browser *b, double now)
{
    for (;;) {
        uint8_t data[QUERY_REPLY_MAX + 16];
        ENetAddress from;
        ENetBuffer buffer = {.data = data, .dataLength = sizeof data};
        int got = enet_socket_receive(b->socket, &from, &buffer, 1);
        if (got <= 0) return;
        char ip[16];
        if (enet_address_get_host_ip(&from, ip, sizeof ip) != 0) continue;
        for (int i = 0; i < b->count; i++) {
            BrowserServer *s = &b->servers[i];
            if (s->answered || s->address.port != from.port || strcmp(s->address.ip, ip) != 0) continue;
            if (query_read_reply(data, (size_t)got, s->nonce, &s->info)) {
                s->answered = true;
                s->ping = (int)((now - s->sent) * 1000.0 + 0.5);
                b->answered++;
            }
            break;
        }
    }
}

void browser_pump(Browser *b, double now)
{
    if (b->state == BROWSER_FETCHING && SDL_AtomicGet(&b->fetch->done)) take_list(b, now);
    if (b->state != BROWSER_QUERYING) return;
    take_answers(b, now);
    bool waiting = false;
    for (int i = 0; i < b->count; i++) {
        BrowserServer *s = &b->servers[i];
        if (s->answered) continue;
        if (now - s->sent >= QUERY_RESEND && s->asked < QUERY_TRIES) {
            uint8_t request[QUERY_REQUEST_SIZE];
            ENetAddress to = {.port = s->address.port};
            if (enet_address_set_host_ip(&to, s->address.ip) == 0) {
                ENetBuffer buffer = {.data = request, .dataLength = query_write_request(request, sizeof request, s->nonce)};
                enet_socket_send(b->socket, &to, &buffer, 1);
            }
            s->sent = now;
            s->asked++;
        }
        if (s->asked < QUERY_TRIES || now - s->sent < QUERY_RESEND) waiting = true;
    }
    if (!waiting) { // nobody else will answer
        b->state = BROWSER_DONE;
        enet_socket_destroy(b->socket);
        b->socket = ENET_SOCKET_NULL;
    }
}

void browser_close(Browser *b)
{
    stop(b);
    b->state = BROWSER_IDLE;
}
