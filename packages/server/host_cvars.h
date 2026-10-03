#pragma once

// The settings a game is hosted by: a dedicated server's, and the client's Local Play
// alike. Both register them here, saved, and keep them in config/server.cfg, so what a
// player hosts with from the main menu is what bin/server hosts with beside it.

#include <stdbool.h>
#include <stddef.h>

#include "console/console.h"

typedef struct HostCvars {
    Cvar *map;      // the first round's
    Cvar *maps;     // sv_maps: a rotation for this run, over maplist.txt
    Cvar *port, *ip, *hostname, *password;
    Cvar *gamemode, *timelimit, *killlimit;
    Cvar *bots_noteam, *bots_alpha, *bots_bravo, *bots_difficulty, *bots_chat;
    Cvar *script;   // sv_script
    Cvar *votepercent, *floodingpackets, *warnings_flood;
    Cvar *rope;     // sv_rope: the rope allowed; off, everyone's boots are jets
    Cvar *public, *lobby_url, *lobby_ip; // sv_public, sv_lobby, sv_lobby_ip
} HostCvars;

void host_cvars_register(Console *con, HostCvars *c);

// The names config/server.cfg keeps (console_save_files): NULL-ended.
extern const char *const HOST_CVAR_PREFIXES[];

// The settings files (config/): each program runs its own as it starts and writes it whole,
// every setting in it with what it is beside it, commented out while it holds its default
// (console_save_files). A server runs server.cfg; the game runs client.cfg, and server.cfg
// too, which its Local Play page sets.
#define CONFIG_SERVER "config/server.cfg"

// config/server.cfg as console_save_files writes it: the hosting settings, written by the
// game as it closes and by a server as it starts.
extern const ConsoleFile HOST_CONFIG_FILE;

// The rotation: config/maplist.txt, a map to a line, played in turn. A server reads it as
// it starts (sv_maps, given on the command line, goes over it for that run); the game's
// Local Play page writes it as maps are ticked.
#define CONFIG_MAPLIST "config/maplist.txt"

// Its maps, space-separated into `out`; empty if there are none or no file.
void maplist_read(char *out, size_t size);
// `maps` (space-separated) written as the file, a map to a line. False if it can't be.
bool maplist_write(const char *maps);
// The file, made empty where it is missing.
void maplist_make(void);

// sv_maps as an old config set it, which the rotation is now: into maplist.txt, and the
// cvar cleared. For a config.cfg from before config/.
void maplist_take_cvar(HostCvars *c, Console *con);
