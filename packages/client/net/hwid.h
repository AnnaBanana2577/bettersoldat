#pragma once

// The machine's hardware ID, which the client says in its Hello so a server's bans and
// mutes (server/lists.h) hold past a new address: eleven hex digits, as Soldat's were.
// It is made from the operating system's own ID of the install (Windows' MachineGuid,
// Linux's /etc/machine-id), hashed with the game's name so it says nothing of that ID
// and matches nothing else made from it; only reinstalling the system changes it. Like
// anything a client says of itself it can be lied about by a client built to; it holds
// against a player who only changes their address.

#include "network/network.h"

// Into `out`; empty if the system's ID can't be read.
void hwid_get(char out[NET_HWID_SIZE]);
