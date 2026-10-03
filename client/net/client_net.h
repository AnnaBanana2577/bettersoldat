#pragma once

// The client's end of the line: connecting, the join, and the two streams (stream.h).
// Everything it hears about the line it tells the console; what it hears about the
// world it applies to the game it is given.

#include "console/console.h"
#include "network/stream.h"
#include "network/transport.h"

#define CLIENT_NET_INBOX 8 // lines of chat kept between frames; past that the oldest is lost

typedef enum ClientNetState { CLIENT_NET_OFF, CLIENT_NET_CONNECTING, CLIENT_NET_JOINING, CLIENT_NET_JOINED } ClientNetState;

typedef struct ClientNet {
    NetLink link;
    ClientNetState state;
    ClientStream stream;
    char name[NET_NAME_SIZE];
    char password[NET_PASSWORD_SIZE]; // the server's, said in the Hello
    PlayerLook look;             // mine, as the app keeps it current: the Hello says it
    Gear gear;                   // the gear of my first placing, likewise
    WeaponId primary, secondary; // the loadout of my first placing, likewise
    int slot;               // mine on the server, once welcomed; -1 before
    uint32_t tick;          // the server's, as of the welcome
    uint16_t round;         // the round being played, as of the last Map
    char map[NET_MAP_SIZE]; // on which map
    char hostname[NET_NAME_SIZE]; // the server's name, as the Map said
    bool rope; // whether the rope is allowed in this game (sv_rope), as the Map said
    char address[64];       // the server's, as connected to
    uint16_t port;
    bool had_map;           // a Map came since the join: the next is a change of map
    bool mapped;            // a Map not yet taken (client_net_take_map)
    MsgChat inbox[CLIENT_NET_INBOX]; // chat heard and not yet taken (client_net_take_chat), oldest first
    int inbox_count;
    MsgVote vote;           // the vote on, kind none for none; for the HUD
    uint32_t vote_seq;      // votes begun, counted: a new one is told from the last
    MsgMapChange map_change; // the round's end as last told: the map coming
    bool map_changing;      // a MapChange not yet taken (client_net_take_map_change)
    MsgMapReply map_reply;  // the server's answer to the map window's last question
    bool map_replied;       // one has come since the last question
    bool playback;          // a demo plays: joined with no line, its packets fed in (client_net_feed)
    // The weapons' numbers as the server said them (a weapons mod), taken over the game's own
    // in every world made for its maps (client_net_weapons).
    WeaponStats weapons[WEAPON_COUNT];
    bool weapons_heard;
    // Told of every message the line brings, before it is heard: a demo records them.
    void (*tap)(void *user, const uint8_t *data, size_t size, MsgKind kind);
    void *tap_user;
} ClientNet;

// Once per program; false if ENet or the ring wouldn't start.
bool client_net_init(ClientNet *n);
void client_net_shutdown(ClientNet *n);

// Connects and, once the line is up, says Hello with `name` and `password` (empty
// for none). Any line already open is closed first.
void client_net_connect(ClientNet *n, Console *con, const char *address, uint16_t port, const char *name,
                        const char *password);
void client_net_disconnect(ClientNet *n, Console *con);

// Everything the line has for the client right now. Snapshots go into `g`, as the
// soldier in `n->slot`.
void client_net_poll(ClientNet *n, Console *con, Game *g);
// A Map came (a join, a new round): once, true, with `n->map` and `n->round` set, for
// the world to be made anew for it. The streams start over from that round.
bool client_net_take_map(ClientNet *n);
// A line of chat heard, oldest first: its sender's slot (MAX_PLAYERS for the server),
// whether to the team, and the text. False when there is none.
bool client_net_take_chat(ClientNet *n, MsgChat *out);
// The round ended (the original's MapChange): once, true, with `n->map_change` set:
// the map coming and the ticks until it, for the scoreboard to come up.
bool client_net_take_map_change(ClientNet *n);
// The map window's question: the name of the server's n-th map. The answer lands in
// `n->map_reply` (client_net_map_replied, once per answer).
void client_net_map_query(ClientNet *n, int index);
bool client_net_map_replied(ClientNet *n);
// After the client's tick: its decisions among the tick's events and its state to the
// server.
void client_net_tick(ClientNet *n, const Game *g);
void client_net_flush(ClientNet *n);

bool client_net_joined(const ClientNet *n);

// The server's weapons onto `g` (a world just made for its map), if it has said any.
void client_net_weapons(const ClientNet *n, Game *g);

// A demo's playback (net/demo.h): joined, as `slot`, with no line; what the demo
// recorded comes in by client_net_feed, and nothing goes out. client_net_disconnect ends it.
void client_net_play(ClientNet *n, int slot);
// A message as the line would bring it, heard as one.
void client_net_feed(ClientNet *n, Console *con, Game *g, const uint8_t *data, size_t size);

// A line of chat to the server, which relays it. False if not joined.
bool client_net_say(ClientNet *n, const char *text, bool team);
