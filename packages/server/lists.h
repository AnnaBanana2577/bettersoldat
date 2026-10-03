#pragma once

// The server's lists, by address: who is banned (until when, as whom, why), who is muted
// (their chat goes to nobody), and who may run the admin commands (connections.h). A
// dedicated server keeps them in config/server/: banlist.cfg and mutelist.cfg, which it
// writes as admins ban and unban, mute and unmute, and admins.cfg, which only the
// server's owner writes. Each is a line per entry, words in "quotes" where they may hold
// spaces, // for a comment:
//
//   ban 1.2.3.4 0 "Major" "Cheating"            the address, the Unix time the ban lifts
//                                               (0 never), the name it was given, why
//   mute 1.2.3.4 "Major"
//   admin 1.2.3.4 "Major"
//
// A game hosted from the main menu (Local Play) keeps them in memory alone.

#include <stdbool.h>
#include <stdint.h>

#include "console/console.h"
#include "network/network.h"

#define MAX_BANS 256
#define MAX_MUTES 256
#define MAX_ADMINS 64
#define LIST_REASON_SIZE 64

typedef struct Ban {
    uint32_t host;   // the address, as ENet has it
    int64_t expires; // the Unix time it lifts at; 0 never
    char name[NET_NAME_SIZE];
    char reason[LIST_REASON_SIZE];
} Ban;

typedef struct ListEntry {
    uint32_t host;
    char name[NET_NAME_SIZE]; // as they were known when listed
} ListEntry;

typedef struct Lists {
    char dir[512]; // where they are kept; empty: in memory alone
    Console *console; // told of what couldn't be read or written; may be NULL
    Ban bans[MAX_BANS];
    int ban_count;
    ListEntry mutes[MAX_MUTES];
    int mute_count;
    ListEntry admins[MAX_ADMINS];
    int admin_count;
} Lists;

// The lists from `dir` (config/server), or none to keep them in memory alone. A file
// that isn't there is an empty list.
void lists_load(Lists *l, const char *dir, Console *console);

// The ban on `host` at `now` (Unix seconds), or NULL. One that has lifted is dropped.
const Ban *lists_banned(Lists *l, uint32_t host, int64_t now);
// `host` banned until `expires` (0 for ever), replacing a ban it had; saved.
void lists_ban(Lists *l, uint32_t host, int64_t expires, const char *name, const char *reason);
// False if it wasn't banned.
bool lists_unban(Lists *l, uint32_t host);

bool lists_muted(const Lists *l, uint32_t host);
void lists_mute(Lists *l, uint32_t host, const char *name);
bool lists_unmute(Lists *l, uint32_t host);

bool lists_admin(const Lists *l, uint32_t host);

// "1.2.3.4" to ENet's form and back. False if it isn't an IPv4 address.
bool lists_address(const char *text, uint32_t *host);
void lists_address_text(uint32_t host, char *out, size_t size);
