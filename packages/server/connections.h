#pragma once

// The server's connections: who is on the line, the join, and the two streams. A peer
// that connects is nobody until its Hello; a Hello with the right version and a free
// slot makes it a player with a soldier, told by Welcome with its slot, the tick and
// the map; anything else is Denied and dropped. A peer that leaves frees its slot. Chat
// is relayed to everyone with the sender's slot. A player's client states are taken as
// its soldier's word (stream.h), and every tick each player gets a snapshot. A line of
// chat beginning with '/' is a command: a vote to change the map or kick a player,
// and the answers to it; one vote runs at a time, for twenty seconds, and passes on
// sv_votepercent of the players. A map vote passed is the server's to act on (connections_take_vote_map);
// a kick is done here.
// A player heard from too often is warned and then kicked for flooding, and so is one
// who chats too fast (flood_tick).
//
// A bot holds a slot too (connections_add_bot): a soldier the server plays itself, with a
// name on the roster and no peer. It is placed with the players each round, its name
// goes in the snapshots, and its chat is relayed as a player's (connections_say_as);
// what it does each tick is the bots' (bots.c), not the line's.
//
// Systems in the server's shape: this takes the link and the game and keeps only the
// table; the console, if given, hears who came and went.

#include "console/console.h"
#include "game/game.h"
#include "network/stream.h"
#include "network/transport.h"
#include "lists.h"

// Why a player is being cut off, for the word of its leaving.
typedef enum KickWhy { KICK_NONE, KICK_VOTED, KICK_CONSOLE, KICK_FLOODING } KickWhy;

typedef struct Connection {
    KickWhy kick_why; // set before the kick; the leaving is announced by it
    ENetPeer *peer; // NULL: the slot is free, unless a bot's
    bool joined;    // Hello accepted: it has a soldier
    bool bot;       // the server's own player: a soldier and a name, no peer
    char name[NET_NAME_SIZE];
    bool chose_team; // said /team; in a game with teams a spectator until it does
    Team team;       // what it said
    int messages;       // heard from it this second (MessagesASecNum)
    int flood_warnings; // seconds it was heard from too often; one forgiven every five minutes (FloodWarnings)
    int chat_warnings;  // lines of chat outstanding; one forgiven a second (ChatWarnings)
    bool admin;         // may run the admin commands: on admins.txt, or logged in with sv_adminpassword
    bool muted;         // its chat reaches nobody (mutelist.txt)
    char hwid[NET_HWID_SIZE]; // its machine's hardware ID, as its Hello said it (lists.h); empty for none
} Connection;

// The votes as the original runs them (Game.pas StartVote, CountVote, TimerVote): twenty
// seconds to decide; only a yes is counted, against the number of players on when it
// began, and it passes at sv_votepercent of them; a no is the voter's own business (its
// client drops the box) and a vote that gathers too few yeses simply runs out. Nobody
// may start one within two minutes of joining or of their last. A kick passed puts the
// player off for an hour, by address and machine.
#define VOTE_TICKS (20 * TICK_RATE)             // DEFAULT_VOTING_TIME
#define VOTE_COOLDOWN_TICKS (2 * 60 * TICK_RATE) // DEFAULT_VOTE_TIME
#define VOTE_PERCENT_DEFAULT 60                  // sv_votepercent
#define VOTE_KICK_BAN_SECONDS (60 * 60) // an hour
#define VOTE_LEFT_BAN_SECONDS (5 * 60)  // a kick vote's target who leaves before it is decided

// Flooding (ServerLoop.pas): a player heard from more than net_floodingpackets times in a
// second gets a warning, and past sv_warnings_flood of them is kicked and barred for a
// quarter of an hour; a warning is forgiven every five minutes. A client sends one state a
// tick, so the default leaves room for twice that. Chat is counted apart: every line is a
// warning, one is forgiven a second, and more than five outstanding is a five-minute kick.
#define FLOOD_PACKETS_DEFAULT 120  // net_floodingpackets
#define FLOOD_WARNINGS_DEFAULT 4   // sv_warnings_flood
#define FLOOD_FORGIVE_TICKS (5 * 60 * TICK_RATE)
#define FLOOD_BAN_SECONDS (15 * 60)
#define CHAT_FLOOD_WARNINGS 5
#define CHAT_FLOOD_BAN_SECONDS (5 * 60)

typedef struct Vote {
    VoteKind kind;              // VOTE_NONE: none running
    char target[NET_MAP_SIZE];  // the map, or the player's name
    char reason[NET_REASON_SIZE]; // a kick's, as the starter typed it
    int slot;                   // the player, for a kick
    int starter;
    int32_t ticks_left;
    int max_votes;              // the players on when it began (VoteMaxVotes)
    uint8_t answer[MAX_PLAYERS]; // 1: voted yes
} Vote;


// What a host may hang on the line, for a server script: it hears a line of chat before
// it is relayed and may keep it, hears a /command the line does not know, and who came
// and went. Any of them may be NULL.
typedef struct LineHooks {
    void *user;
    bool (*chat)(void *user, int slot, const char *text, bool team); // true: kept, not relayed
    bool (*command)(void *user, int slot, const char *text);         // the text after the '/'; true: answered
    void (*joined)(void *user, int slot);
    void (*left)(void *user, int slot, const char *name);
} LineHooks;

typedef struct Connections {
    const LineHooks *hooks; // may be NULL
    NetLink *link;
    Connection items[MAX_PLAYERS]; // by slot, the soldier's index
    ServerStream *streams;         // by slot, on the heap
    WireQueue events;              // the server's decisions and what it relays, for everyone
    Console *console;              // may be NULL
    uint16_t round;                // the round being played, from 1
    char map[NET_MAP_SIZE];        // on which map
    char hostname[NET_NAME_SIZE];  // the server's name, told with the map (sv_hostname)
    char password[NET_PASSWORD_SIZE]; // what a Hello must say to join; empty asks none (sv_password)
    bool rope;                     // whether the rope is allowed (sv_rope), told with the map
    char maps_dir[512];            // where a voted map must be found, <data>/maps; empty accepts any
    const char (*maps)[64];        // the server's list of maps (the original's MapsList), for the map window and
    int map_count;                 // the votes; NULL, and a voted map is looked for in maps_dir instead
    char next_map[NET_MAP_SIZE];   // the map the round's end leads to, told with MsgMapChange
    uint32_t ticks;                // ticks run, for the cooldowns and the bans
    Vote vote;
    int vote_percent;              // sv_votepercent; VOTE_PERCENT_DEFAULT unless set
    int flood_packets;             // net_floodingpackets; FLOOD_PACKETS_DEFAULT unless set
    int flood_warnings_max;        // sv_warnings_flood; FLOOD_WARNINGS_DEFAULT unless set
    int32_t vote_cooldown[MAX_PLAYERS]; // ticks until each may start a vote; below 0 may
    char vote_map[NET_MAP_SIZE];   // a map vote passed (or an admin's /map), until the server takes it
    Lists lists;                   // the bans, the mutes and the admins (lists.h)
} Connections;

// `map` is the map being played, round 1. False if the streams couldn't be made.
bool connections_init(Connections *c, NetLink *link, Console *console, const char *map);
void connections_free(Connections *c);
// The password a Hello must say from now on; empty for none. Whoever is on stays.
void connections_set_password(Connections *c, const char *password);

// A new round on `map`, the world already made anew: everyone joined is placed on their
// team, their streams begin afresh, and everyone is told (MsgMap).
void connections_new_round(Connections *c, Game *g, const char *map);

// Everything the line has for the server right now: joins, leaves, states, chat.
void connections_poll(Connections *c, Game *g);

// The command each player's soldier steps on this tick: its last keys, or none once quiet.
void connections_commands(const Connections *c, const Game *g, Command cmds[MAX_PLAYERS]);

// After the tick: what it left that travels (the server's decisions, and the players'
// heard this tick, for the others) into the queue, then a snapshot to every player.
void connections_snapshots(Connections *c, const Game *g);

// A message to every joined player.
void connections_broadcast(Connections *c, MsgKind kind, const uint8_t *data, size_t size);

int connections_count(const Connections *c);

// A player's soldier on `team`: alive on its spawn, or a spectator, present on the roster
// and nowhere else. What it held is let go of.
void connections_place(Connections *c, Game *g, int slot, Team team);

// A map vote passed since last asked: true, with the map, once.
bool connections_take_vote_map(Connections *c, char *map, size_t size);

// The round is over and `map` follows once the match's counter runs out: everyone is
// told (MsgMapChange), and so is whoever joins before it does.
void connections_map_change(Connections *c, const Game *g, const char *map);

// The player in `slot` is put off by address and machine for `seconds` (0 for ever), with a reason the
// next Hello from it is denied with; on the ban list (lists.h).
void connections_ban(Connections *c, int slot, int64_t seconds, const char *reason);

// An admin command, said in the chat by an admin (`from` its slot) or typed at the
// server's console (`from` -1), and answered to whoever said it. False if `text` (the
// command without its '/') isn't one; then it is a player's to try as anything else.
//   kick <player> [reason]           off the server
//   ban <player> [minutes] [reason]  off it and barred by address and machine; no minutes, or 0, for ever
//   banip <address> [minutes] [reason]
//   banhw <hardware ID> [minutes] [reason]
//   unban <address, hardware ID or the name banned>
//   mute <player> / unmute <player, address, hardware ID or name>   their chat reaches nobody,
//                                    rejoining or not, by address and machine
//   map <name>                       the round ends, and that map follows
//   bans / mutes / admins            the lists
// A player names a slot, or a name or as much of one as is typed. Anyone may say
// /login <password>, which with sv_adminpassword set makes them an admin until they leave.
bool connections_admin(Connections *c, Game *g, int from, const char *text);

// The weapons' numbers as the game now has them, to everyone: a weapons mod changed while
// they play. Whoever joins hears them as they join.
void connections_send_weapons(Connections *c, const Game *g);

// The server's own chat to everyone, shown as "*SERVER*: text".
void connections_say(Connections *c, const char *text);
// A line of `kind` to everyone, or to the one player in `slot` (a bot's slot hears
// nothing). `color` is a script line's own (CHAT_SCRIPT); alpha 0 leaves the choice to
// the client.
void connections_say_kind(Connections *c, ChatKind kind, Rgba color, const char *text);
void connections_say_to(Connections *c, int slot, ChatKind kind, Rgba color, const char *text);

// A player put off the server: told why, and cut off. The slot frees as the line closes.
void connections_kick(Connections *c, int slot, const char *reason);

// A bot into a free slot, placed on `team` (the emptier side in a team game when it
// isn't alpha or bravo; none otherwise) and announced: its slot, or -1 when full. The
// soldier is the server's to play: not remote, marked a bot.
int connections_add_bot(Connections *c, Game *g, const char *name, PlayerLook look, WeaponId primary, WeaponId secondary,
                        Team team);
// The bot in `slot` leaves: its soldier gone, its slot free, its leaving announced.
void connections_remove_bot(Connections *c, Game *g, int slot);
// A line of chat from the player in `slot` (a bot's), to everyone.
void connections_say_as(Connections *c, int slot, const char *text);
