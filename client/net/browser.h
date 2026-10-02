#pragma once

// The server browser's data: the lobby's list of servers (cl_lobby), and each server
// asked the query (network/query.h) for what it is playing and how far away it is.
// The list comes over HTTPS on a thread of its own, so the frame never waits for the
// lobby; the queries go out together from one socket and their answers are taken as
// they come, each server asked again if it hasn't answered within a second, and given
// up on after three. What a server says is its own, now: the lobby only knows where it
// is.
//
// The main menu reads it and asks for a refresh with the `browse` command; nothing
// here draws.

#include <SDL.h>

#include "network/transport.h"

#define BROWSER_MAX 256

typedef enum BrowserState {
    BROWSER_IDLE,     // never asked
    BROWSER_FETCHING, // the lobby asked for its list
    BROWSER_QUERYING, // the servers asked what they are playing
    BROWSER_DONE,     // everything that will answer has
    BROWSER_FAILED,   // the lobby couldn't be reached; `error` says why
} BrowserState;

typedef struct BrowserServer {
    QueryAddress address;
    bool answered;
    ServerInfo info; // once answered
    int ping;        // milliseconds, once answered
    uint32_t nonce;  // the query's
    double sent;     // when it was last asked
    int asked;       // how many times
} BrowserServer;

typedef struct BrowserFetch BrowserFetch;

typedef struct Browser {
    BrowserState state;
    char error[160];
    BrowserServer servers[BROWSER_MAX]; // in the lobby's order
    int count;
    int answered;
    ENetSocket socket; // the queries', while querying
    BrowserFetch *fetch; // the list on its way, while fetching
    double started;      // when the queries went out
    uint32_t refreshes;  // counted, so a reader can tell a new list from the last
} Browser;

void browser_init(Browser *b);
// The list asked for anew from the lobby at `lobby_url`, everything known forgotten.
// `now` is seconds on the clock browser_pump is given.
void browser_refresh(Browser *b, const char *lobby_url, double now);
// The list taken when it comes, the queries sent and resent, the answers read.
void browser_pump(Browser *b, double now);
void browser_close(Browser *b);
