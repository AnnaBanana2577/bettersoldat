#pragma once

// The wire: what the client and the server say to each other, and how it is laid out
// (docs/netcode.md). Transport is ENet, one unreliable channel and one reliable; this
// is the bytes.
//
// One serializer serves both directions. A NetBuf is writing or reading, and every
// net_* call writes the value it is given or reads into it, so a message has one
// routine that reads and writes it and cannot disagree with itself. Reading refuses
// what it cannot trust: bits past the end, a float that is not a number, a value past
// its range, a string too long. The buffer goes bad and stays bad, and the caller
// drops the message whole (netbuf_ok). Writing a value that does not fit its width
// goes bad the same way, so a width chosen too small is found in a test, not on the
// wire.
//
// Netfields lay a struct on the wire. A table, one entry per field with its offset,
// size, kind and width, drives one routine (netfields_serialize) that writes the struct
// whole or as a delta against a baseline (one bit per field, then the fields that
// changed) and reads it back, unchanged fields taken from the baseline. The soldier's
// halves have their tables here, held to soldier_copy_owned and soldier_copy_served by
// a test.
//
// Integers are little-endian in memory on both ends, as x86 and ARM are; the wire is
// bits in emission order, so it does not care.

#include "game/entities.h"

#define NET_VERSION 17
#define NET_DEFAULT_PORT 23073
#define NET_NAME_SIZE 24 // a player's name, with its terminator
#define NET_PASSWORD_SIZE 32 // the server's password, with its terminator (sv_password, cl_password)
#define NET_TEXT_SIZE 128 // a line of chat, a reason
#define NET_MAP_SIZE 64  // a map's name, with its terminator
#define NET_REASON_SIZE 26 // a kick vote's reason (the original's REASON_CHARS)
#define NET_MTU 1200      // a packet, so that nothing is fragmented

// --- the buffer --------------------------------------------------------------------

typedef enum NetMode { NET_WRITE, NET_READ } NetMode;

typedef struct NetBuf {
    uint8_t *data;
    size_t size;   // bytes
    size_t bit;    // where the next bit goes, or comes from
    NetMode mode;
    bool overflow; // writing past the end
    bool bad;      // reading something untrustworthy, or writing what does not fit
} NetBuf;

NetBuf netbuf_writer(uint8_t *data, size_t size); // zeroes the bytes
NetBuf netbuf_reader(const uint8_t *data, size_t size);

bool netbuf_ok(const NetBuf *b);      // nothing has gone wrong
size_t netbuf_bytes(const NetBuf *b); // the bytes holding what was written, or read so far
// A reader that read everything: within the last byte's padding, and nothing wrong.
bool netbuf_done(const NetBuf *b);

// Unsigned in 1 to 32 bits; signed in 2 to 32, two's complement; a bool in 1.
void net_bits(NetBuf *b, uint32_t *v, int bits);
void net_signed(NetBuf *b, int32_t *v, int bits);
void net_bool(NetBuf *b, bool *v);
void net_u8(NetBuf *b, uint8_t *v);
void net_u16(NetBuf *b, uint16_t *v);
void net_u32(NetBuf *b, uint32_t *v);
void net_u64(NetBuf *b, uint64_t *v);
// 0 to `max`, in as few bits as max needs; reading past max is bad.
void net_range(NetBuf *b, uint32_t *v, uint32_t max);
// 32 bits; reading a NaN or an infinity is bad.
void net_f32(NetBuf *b, float *v);
void net_vec2(NetBuf *b, Vec2 *v);
// Up to size - 1 characters, with a length first; reading a longer one is bad.
void net_string(NetBuf *b, char *s, size_t size);

// --- netfields ---------------------------------------------------------------------

typedef enum NetKind {
    NET_U,    // an unsigned integer of 1, 2, 4 or 8 bytes, `bits` wide on the wire
    NET_I,    // a signed one
    NET_BOOL, // a bool, one bit
    NET_F32,  // a float, 32 bits
    NET_VEC2, // two floats
    NET_RGBA, // a colour, 32 bits
} NetKind;

typedef struct NetField {
    const char *name;
    uint16_t offset;
    uint8_t size; // in memory
    NetKind kind;
    uint8_t bits; // on the wire (NET_U, NET_I)
    uint32_t max; // NET_U: the largest value allowed, 0 for any (an enum's last)
} NetField;

// The bits a value up to `max` needs; the macro for a table's initializer.
int net_bits_for(uint32_t max);
#define NET_BITS_FOR(m)                                                                                              \
    ((m) < 2u ? 1 : (m) < 4u ? 2 : (m) < 8u ? 3 : (m) < 16u ? 4 : (m) < 32u ? 5 : (m) < 64u ? 6 : (m) < 128u ? 7 :  \
     (m) < 256u ? 8 : (m) < 512u ? 9 : (m) < 1024u ? 10 : (m) < 4096u ? 12 : (m) < 65536u ? 16 : 32)

#define NET_OFFSET(type, member) ((uint16_t)offsetof(type, member))
#define NET_SIZEOF(type, member) ((uint8_t)sizeof(((type *)0)->member))
#define NETFIELD(type, member, kind, bits) {#member, NET_OFFSET(type, member), NET_SIZEOF(type, member), kind, bits, 0}
#define NETFIELD_ENUM(type, member, max) \
    {#member, NET_OFFSET(type, member), NET_SIZEOF(type, member), NET_U, NET_BITS_FOR((uint32_t)(max)), (uint32_t)(max)}

// The struct `state` on the wire: every field, or, with a `base`, one bit per field
// and only the fields that differ from it. Reading with a base takes the unchanged
// fields from it. Fields the table does not name are left alone.
void netfields_serialize(NetBuf *b, const NetField *fields, int count, void *state, const void *base);

// Whether two structs agree on every field of the table.
bool netfields_equal(const NetField *fields, int count, const void *a, const void *b);
// Every field of the table from `src` into `dst`; the rest of `dst` left alone.
void netfields_copy(const NetField *fields, int count, void *dst, const void *src);

// The soldier's halves (soldier_copy_owned, soldier_copy_served). A received owned half
// is read into a scratch Soldier and copied in with soldier_copy_owned, which sets the
// animations' speed from the anims as the fields cannot.
extern const NetField SOLDIER_OWNED_FIELDS[];
extern const int SOLDIER_OWNED_COUNT;
extern const NetField SOLDIER_SERVED_FIELDS[];
extern const int SOLDIER_SERVED_COUNT;

// What is the player's to choose about its soldier, though the served half carries it:
// its look, said once in the Hello, as a look is for a game (the original's way), and
// the weapons of its next spawn, which ride the client state as they change with
// every pick in the menu. The server takes both as said.
extern const NetField PLAYER_LOOK_FIELDS[];
extern const int PLAYER_LOOK_COUNT;
extern const NetField SOLDIER_LOADOUT_FIELDS[];
extern const int SOLDIER_LOADOUT_COUNT;

// A thing as the wire carries it: what it is, whose, where its points are; not its
// forces, its landing counts or its background state, which are each machine's own.
extern const NetField THING_FIELDS[];
extern const int THING_COUNT;

// The match: the scores, the clock, the state.
extern const NetField MATCH_FIELDS[];
extern const int MATCH_COUNT;

// --- the messages ------------------------------------------------------------------

// Every message begins with its kind. Whether it goes reliably is the table's to say
// (MSG_RELIABLE), never a call site's: state (the client's, the snapshot) is sent over
// and over, unreliably, a lost one replaced by the next; news goes once, in order.
typedef enum MsgKind {
    MSG_INVALID,
    MSG_HELLO,        // client -> server: the version, the name and the password
    MSG_WELCOME,      // server -> client: the slot, and the tick
    MSG_DENIED,       // server -> client: why not
    MSG_CHAT,         // either way: a line said, to everyone or the team; commands and votes too
    MSG_MAP,          // server -> client: the map to play and the round's number: on joining, and each round
    MSG_CLIENT_STATE, // client -> server, every tick: the owned half (stream.h)
    MSG_SNAPSHOT,     // server -> client, every tick: everyone's halves (stream.h)
    MSG_VOTE,         // server -> client: a vote begun (for the HUD), or over (kind none)
    MSG_MAP_CHANGE,   // server -> client: the round is over; the next map, and the ticks until it
    MSG_MAP_QUERY,    // client -> server: the name of the server's map list's n-th map, for the map window
    MSG_MAP_REPLY,    // server -> client: that name, and how many the list holds
    MSG_COUNT,
} MsgKind;

extern const bool MSG_RELIABLE[MSG_COUNT];

typedef struct MsgHello {
    uint16_t version;
    char name[NET_NAME_SIZE];
    char password[NET_PASSWORD_SIZE]; // the server's (sv_password), or empty
    PlayerLook look;             // how the player dresses its soldier, for the game
    Gear gear;                   // the gear of its first placing: jets, or a rope
    WeaponId primary, secondary; // the loadout of its first placing
} MsgHello;

typedef struct MsgWelcome {
    uint8_t slot;
    uint32_t tick;
} MsgWelcome;

// The map to play and the round it begins, for a client to make its world anew; the
// streams of that round follow, stamped with its number, and those of another round
// are dropped. Joining is hearing of the first.
typedef struct MsgMap {
    uint16_t round;
    char map[NET_MAP_SIZE];
    char hostname[NET_NAME_SIZE]; // the server's, for the scoreboard
    bool rope;                    // whether the rope is allowed in this game (sv_rope)
} MsgMap;

// A vote as the HUD shows it: what is voted on and by whom, and how long it has. The
// votes themselves are chat: /votemap, /votekick, /yes and /no, which the server reads.
typedef enum VoteKind { VOTE_NONE, VOTE_KICK, VOTE_MAP } VoteKind;
typedef struct MsgVote {
    VoteKind kind;
    char target[NET_MAP_SIZE];  // the map, or the player's name
    char starter[NET_NAME_SIZE];
    char reason[NET_REASON_SIZE]; // a kick's, as typed
    uint16_t seconds;
} MsgVote;

typedef struct MsgDenied {
    char reason[NET_TEXT_SIZE];
} MsgDenied;

// The round is over (the original's MapChange): the world stands frozen with the
// scoreboard up for `counter` ticks, then `map` is played. Sent as the countdown
// begins, and to whoever joins during it.
typedef struct MsgMapChange {
    uint16_t counter;
    char map[NET_MAP_SIZE];
} MsgMapChange;

// The escape menu's map window pages through the server's own list of maps (the
// original's MapsList), one name at a time: the client asks for the n-th, the server
// answers with it and the list's length.
typedef struct MsgMapQuery {
    uint16_t index;
} MsgMapQuery;

typedef struct MsgMapReply {
    uint16_t index, count;
    char map[NET_MAP_SIZE];
} MsgMapReply;

// The kinds of line the server itself says: the original's colour classes for them
// (Constants.pas *_MESSAGE_COLOR), which the client colours as the original does.
typedef enum ChatKind {
    CHAT_SERVER,    // its own chat, said as "*SERVER*: "
    CHAT_ENTER,     // who came and went, with no team
    CHAT_ALPHA,     // who came to and left alpha
    CHAT_BRAVO,     // bravo
    CHAT_SPECTATOR, // the spectators
    CHAT_CLIENT,    // who was cut off: kicked
    CHAT_GAME,      // the game's word
    CHAT_VOTE,      // a vote's
    CHAT_SCRIPT,    // a server script's, in the script colour, or one of its own choosing
    CHAT_KINDS
} ChatKind;

typedef struct MsgChat {
    uint8_t slot; // who said it; MAX_PLAYERS for the server, whose lines are of `kind`
    bool team;    // a player's, to its team alone
    uint8_t kind; // the server's (ChatKind); nothing for a player's
    Rgba color;   // a script line's own colour, carried for CHAT_SCRIPT alone; alpha 0 for the script colour
    char text[NET_TEXT_SIZE];
} MsgChat;

// The kind, first in every message; reading one past the table is bad.
void msg_kind(NetBuf *b, MsgKind *kind);
void msg_hello(NetBuf *b, MsgHello *m);
void msg_welcome(NetBuf *b, MsgWelcome *m);
void msg_denied(NetBuf *b, MsgDenied *m);
void msg_chat(NetBuf *b, MsgChat *m);
void msg_vote(NetBuf *b, MsgVote *m);
void msg_map(NetBuf *b, MsgMap *m);
void msg_map_change(NetBuf *b, MsgMapChange *m);
void msg_map_query(NetBuf *b, MsgMapQuery *m);
void msg_map_reply(NetBuf *b, MsgMapReply *m);
