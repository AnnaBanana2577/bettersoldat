#pragma once

// The heartbeat: a dedicated server listing itself with the lobby (the
// soldatreloaded-lobby repository; docs/netcode.md, The query, and the lobby). While
// sv_public is on it says it is up as often as the lobby asks (half a minute), with its
// port, and the lobby asks it the query (network/query.h) before it lists it. A server
// behind a proxy that sends from another address than players reach it on (Fly's
// fly-global-services) names the one they reach in sv_lobby_ip; without it the request
// goes over IPv4, since the lobby lists the address it is reached from and ENet is IPv4
// only.
//
// Each request runs on a thread of its own, so a slow lobby never holds up a tick, and
// what comes of it is told on the console when it changes rather than every half
// minute. A server leaving the list (sv_public turned off, or stopping) says so, unless
// it named its address: the lobby lets only time take that one off.

#include "console/console.h"
#include "network/query.h" // QUERY_LOBBY_URL, sv_lobby's default

typedef struct LobbySettings {
    bool public;         // sv_public
    const char *url;     // sv_lobby: the lobby's base address
    const char *address; // sv_lobby_ip: the address to list; empty for the one the request comes from
    uint16_t port;       // sv_port
} LobbySettings;

typedef struct LobbyJob LobbyJob;

typedef struct Lobby {
    Console *console;      // may be NULL
    LobbyJob *job;         // the request in flight, or NULL
    double next;           // when the next heartbeat is due, on the pump's clock
    double interval;       // seconds between heartbeats, as the lobby last said
    bool listed;           // the last heartbeat was taken
    char told[256];        // what the console was last told, so it is told once
    char goodbye[512];     // the lobby to say goodbye to, and how (lobby_url), while listed; empty for none
    char goodbye_body[96];
} Lobby;

void lobby_init(Lobby *l, Console *console);
// Heartbeats while `s->public`, a goodbye once it isn't, and what came of the last
// request on the console. `now` is seconds on any steady clock.
void lobby_pump(Lobby *l, const LobbySettings *s, double now);
// The request in flight waited for, then the goodbye if listed, waited for a few
// seconds at most.
void lobby_close(Lobby *l);

// --- the pieces, for the tests ----------------------------------------------------

// `base` with the servers' path: "https://x/" and "https://x" both give
// "https://x/v1/servers". False if it doesn't fit.
bool lobby_url(char *out, size_t size, const char *base);
// The heartbeat's JSON: the port, and the address if one is named. False if the
// address is not an IPv4 address or it doesn't fit.
bool lobby_body(char *out, size_t size, uint16_t port, const char *address);
// The heartbeat interval the lobby's answer asks for, in seconds; 0 if it says none.
int lobby_interval(const char *reply);
// The address and port the lobby's answer lists the server as ("1.2.3.4:23073"); false
// if it says none.
bool lobby_listed_as(const char *reply, char *out, size_t size);
