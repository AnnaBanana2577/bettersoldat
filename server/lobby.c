#include "lobby.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http.h"

#define LOBBY_TIMEOUT 10L        // seconds a heartbeat may take
#define LOBBY_GOODBYE_TIMEOUT 3L // and the goodbye, which a stopping server waits for
#define LOBBY_ANSWER_MAX 1024
#define LOBBY_DEFAULT_INTERVAL 30.0
#define LOBBY_MIN_INTERVAL 10.0 // whatever the lobby says, never more often than this

// --- threads, as script.c has them -----------------------------------------------------

#ifdef _WIN32
#define NOGDI
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef CRITICAL_SECTION Lock;
typedef HANDLE Thread;
static void lock_init(Lock *l) { InitializeCriticalSection(l); }
static void lock(Lock *l) { EnterCriticalSection(l); }
static void unlock(Lock *l) { LeaveCriticalSection(l); }
#else
#include <pthread.h>
typedef pthread_mutex_t Lock;
typedef pthread_t Thread;
static void lock_init(Lock *l) { pthread_mutex_init(l, NULL); }
static void lock(Lock *l) { pthread_mutex_lock(l); }
static void unlock(Lock *l) { pthread_mutex_unlock(l); }
#endif

// --- a request on a thread of its own ---------------------------------------------------

struct LobbyJob {
    char method[8];
    char url[512];
    char body[96];
    bool ipv4;
    long timeout;
    Thread thread;
    // written by the thread, read once `done`, under the lock
    HttpResult result;
    long status;
    char *answer;
    char error[256];
    bool done;
};

static Lock job_lock;
static bool job_lock_made;

static void job_perform(LobbyJob *job)
{
    long status = 0;
    char *answer = NULL;
    char error[256] = "";
    HttpResult result = http_request(job->method, job->url, job->body[0] ? job->body : NULL, job->ipv4, job->timeout,
                                     LOBBY_ANSWER_MAX, &status, &answer, error, sizeof error);
    lock(&job_lock);
    job->result = result;
    job->status = status;
    job->answer = answer;
    snprintf(job->error, sizeof job->error, "%s", error);
    job->done = true;
    unlock(&job_lock);
}

#ifdef _WIN32
static DWORD WINAPI job_thread(LPVOID arg)
{
    job_perform(arg);
    return 0;
}
#else
static void *job_thread(void *arg)
{
    job_perform(arg);
    return NULL;
}
#endif

static LobbyJob *job_start(const char *method, const char *url, const char *body, bool ipv4, long timeout)
{
    if (!job_lock_made) {
        lock_init(&job_lock);
        job_lock_made = true;
    }
    LobbyJob *job = calloc(1, sizeof *job);
    if (!job) return NULL;
    snprintf(job->method, sizeof job->method, "%s", method);
    snprintf(job->url, sizeof job->url, "%s", url);
    snprintf(job->body, sizeof job->body, "%s", body);
    job->ipv4 = ipv4;
    job->timeout = timeout;
#ifdef _WIN32
    job->thread = CreateThread(NULL, 0, job_thread, job, 0, NULL);
    bool started = job->thread != NULL;
#else
    bool started = pthread_create(&job->thread, NULL, job_thread, job) == 0;
#endif
    if (!started) {
        free(job);
        return NULL;
    }
    return job;
}

static bool job_done(LobbyJob *job)
{
    lock(&job_lock);
    bool done = job->done;
    unlock(&job_lock);
    return done;
}

static void job_finish(LobbyJob *job)
{
#ifdef _WIN32
    WaitForSingleObject(job->thread, INFINITE);
    CloseHandle(job->thread);
#else
    pthread_join(job->thread, NULL);
#endif
    free(job->answer);
    free(job);
}

// --- the pieces ----------------------------------------------------------------------

bool lobby_url(char *out, size_t size, const char *base)
{
    size_t n = strlen(base);
    while (n && base[n - 1] == '/') n--;
    int w = snprintf(out, size, "%.*s/v1/servers", (int)n, base);
    return n > 0 && w > 0 && (size_t)w < size;
}

// Four numbers to 255, dotted, and nothing else.
static bool is_ipv4(const char *s)
{
    for (int part = 0; part < 4; part++) {
        if (!isdigit((unsigned char)*s)) return false;
        int v = 0, digits = 0;
        while (isdigit((unsigned char)*s) && digits < 4) v = v * 10 + (*s++ - '0'), digits++;
        if (digits > 3 || v > 255) return false;
        if (part < 3 && *s++ != '.') return false;
    }
    return *s == '\0';
}

bool lobby_body(char *out, size_t size, uint16_t port, const char *address)
{
    int w;
    if (address && address[0]) {
        if (!is_ipv4(address)) return false;
        w = snprintf(out, size, "{\"port\":%u,\"address\":\"%s\"}", port, address);
    } else {
        w = snprintf(out, size, "{\"port\":%u}", port);
    }
    return w > 0 && (size_t)w < size;
}

// Where the value of "key" begins in a flat JSON object, past the colon and spaces;
// NULL if it isn't there.
static const char *json_value(const char *json, const char *key)
{
    char quoted[64];
    snprintf(quoted, sizeof quoted, "\"%s\"", key);
    const char *p = json ? strstr(json, quoted) : NULL;
    if (!p) return NULL;
    p += strlen(quoted);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p++ != ':') return NULL;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

int lobby_interval(const char *reply)
{
    const char *p = json_value(reply, "heartbeat_seconds");
    return p && isdigit((unsigned char)*p) ? atoi(p) : 0;
}

bool lobby_listed_as(const char *reply, char *out, size_t size)
{
    const char *a = json_value(reply, "address"), *p = json_value(reply, "port");
    if (!a || *a != '"' || !p || !isdigit((unsigned char)*p)) return false;
    const char *end = strchr(a + 1, '"');
    if (!end) return false;
    int w = snprintf(out, size, "%.*s:%d", (int)(end - a - 1), a + 1, atoi(p));
    return w > 0 && (size_t)w < size;
}

// --- the heartbeat -------------------------------------------------------------------

void lobby_init(Lobby *l, Console *console) { *l = (Lobby){.console = console, .interval = LOBBY_DEFAULT_INTERVAL}; }

// A line on the console, unless it is what was told last.
static void tell(Lobby *l, const char *text)
{
    if (strcmp(l->told, text) == 0) return;
    snprintf(l->told, sizeof l->told, "%s", text);
    if (l->console) console_print(l->console, "lobby: %s\n", text);
}

// What the finished heartbeat came to.
static void heard(Lobby *l, LobbyJob *job)
{
    char text[256];
    if (job->result != HTTP_OK) {
        l->listed = false;
        snprintf(text, sizeof text, "can't be reached (%s); trying again", job->error);
    } else if (job->status == 200) {
        int interval = lobby_interval(job->answer);
        if (interval > 0) l->interval = interval < LOBBY_MIN_INTERVAL ? LOBBY_MIN_INTERVAL : interval;
        char as[64] = "";
        if (!lobby_listed_as(job->answer, as, sizeof as)) snprintf(as, sizeof as, "this server");
        snprintf(text, sizeof text, "listed as %s", as);
        l->listed = true;
    } else {
        // the lobby's own words: a 422 is a port it couldn't reach
        char *answer = job->answer ? job->answer : "";
        size_t n = strcspn(answer, "\r\n");
        snprintf(text, sizeof text, "not listed: %.*s (%ld)", (int)(n > 160 ? 160 : n), answer, job->status);
        l->listed = false;
    }
    tell(l, text);
}

void lobby_pump(Lobby *l, const LobbySettings *s, double now)
{
    if (l->job) {
        if (!job_done(l->job)) return;
        if (strcmp(l->job->method, "POST") == 0) heard(l, l->job);
        job_finish(l->job);
        l->job = NULL;
    }
    char url[512], body[96];
    if (!s->public) {
        if (l->goodbye[0]) { // taken off: say so, and nothing more until it is put back
            l->job = job_start("DELETE", l->goodbye, l->goodbye_body, true, LOBBY_TIMEOUT);
            l->goodbye[0] = '\0';
            l->listed = false;
            tell(l, "off the list");
        }
        l->next = now; // put back on, it says so at once
        return;
    }
    if (now < l->next) return;
    l->next = now + l->interval;
    if (!lobby_url(url, sizeof url, s->url)) {
        tell(l, "sv_lobby is not an address");
        return;
    }
    if (!lobby_body(body, sizeof body, s->port, s->address)) {
        tell(l, "sv_lobby_ip must be an IPv4 address, as 1.2.3.4");
        return;
    }
    bool named = s->address && s->address[0];
    l->job = job_start("POST", url, body, !named, LOBBY_TIMEOUT);
    // the goodbye goes where the heartbeat went, from the address it came from; a named
    // address only time takes off
    if (!named) {
        snprintf(l->goodbye, sizeof l->goodbye, "%s", url);
        lobby_body(l->goodbye_body, sizeof l->goodbye_body, s->port, NULL);
    } else {
        l->goodbye[0] = '\0';
    }
}

void lobby_close(Lobby *l)
{
    if (l->job) {
        job_finish(l->job);
        l->job = NULL;
    }
    if (l->listed && l->goodbye[0]) {
        long status;
        char *answer = NULL, error[256];
        if (http_request("DELETE", l->goodbye, l->goodbye_body, true, LOBBY_GOODBYE_TIMEOUT, LOBBY_ANSWER_MAX, &status,
                         &answer, error, sizeof error) == HTTP_OK &&
            l->console)
            console_print(l->console, "lobby: off the list\n");
        free(answer);
    }
    l->goodbye[0] = '\0';
    l->listed = false;
}
