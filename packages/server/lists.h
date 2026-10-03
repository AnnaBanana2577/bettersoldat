#pragma once

// The server's lists: who is banned (until when, as whom, why) and who is muted (their
// chat goes to nobody), by address and by the machine's hardware ID (net/hwid.h), so a
// new address alone doesn't lift either; and who may run the admin commands
// (connections.h), by address. The server keeps them in config/: banlist.txt and
// mutelist.txt, which it writes as admins ban and unban, mute and unmute, and admins.txt,
// which only the server's owner writes. Each is a line per entry, words in "quotes" where
// they may hold spaces, - for an address or hardware ID it doesn't name, // for a comment:
//
//   ban 1.2.3.4 0A1B2C3D4E5 0 "Major" "Cheating"   the address, the hardware ID, the Unix
//                                                  time the ban lifts (0 never), the name
//                                                  it was given, why
//   mute 1.2.3.4 0A1B2C3D4E5 "Major"
//   admin 1.2.3.4 "Major"
//
// A ban or mute line from before hardware IDs (`ban 1.2.3.4 0 "Major" "Cheating"`,
// `mute 1.2.3.4 "Major"`) is read as one by address.

#include <stdbool.h>
#include <stdint.h>

#include "console/console.h"
#include "network/network.h"

#define MAX_BANS 256
#define MAX_MUTES 256
#define MAX_ADMINS 64
#define LIST_REASON_SIZE 64

typedef struct Ban {
    uint32_t host;              // the address, as ENet has it; 0 for none
    char hwid[NET_HWID_SIZE];   // the machine's (net/hwid.h); empty for none
    int64_t expires;            // the Unix time it lifts at; 0 never
    char name[NET_NAME_SIZE];
    char reason[LIST_REASON_SIZE];
} Ban;

typedef struct ListEntry {
    uint32_t host;              // 0 for none
    char hwid[NET_HWID_SIZE];   // empty for none; an admin is by address alone
    char name[NET_NAME_SIZE];   // as they were known when listed
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

// The lists from `dir` (config), or none to keep them in memory alone. A file
// that isn't there is an empty list.
void lists_load(Lists *l, const char *dir, Console *console);

// A player is named by their address and their machine's hardware ID (either may be
// missing: host 0, hwid NULL or empty); an entry names them if it names either.

// The ban on them at `now` (Unix seconds), or NULL. One that has lifted is dropped.
const Ban *lists_banned(Lists *l, uint32_t host, const char *hwid, int64_t now);
// Them banned until `expires` (0 for ever), replacing a ban that named them; saved.
void lists_ban(Lists *l, uint32_t host, const char *hwid, int64_t expires, const char *name, const char *reason);
// Every ban that named them lifted. False if none did.
bool lists_unban(Lists *l, uint32_t host, const char *hwid);

bool lists_muted(const Lists *l, uint32_t host, const char *hwid);
void lists_mute(Lists *l, uint32_t host, const char *hwid, const char *name);
bool lists_unmute(Lists *l, uint32_t host, const char *hwid);

bool lists_admin(const Lists *l, uint32_t host);

// "1.2.3.4" to ENet's form and back. False if it isn't an IPv4 address.
bool lists_address(const char *text, uint32_t *host);
void lists_address_text(uint32_t host, char *out, size_t size);

// Whether `text` is a hardware ID, eleven hex digits; into `out` in capitals if so.
bool lists_hwid(const char *text, char out[NET_HWID_SIZE]);
