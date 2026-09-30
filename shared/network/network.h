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

#define NET_VERSION 1
#define NET_DEFAULT_PORT 23073
#define NET_NAME_SIZE 24 // a player's name, with its terminator
#define NET_TEXT_SIZE 128 // a line of chat, a reason
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

#define NET_SIZEOF(type, member) sizeof(((type *)0)->member)
#define NETFIELD(type, member, kind, bits) {#member, offsetof(type, member), NET_SIZEOF(type, member), kind, bits, 0}
#define NETFIELD_ENUM(type, member, max) \
    {#member, offsetof(type, member), NET_SIZEOF(type, member), NET_U, NET_BITS_FOR((uint32_t)(max)), (uint32_t)(max)}

// The struct `state` on the wire: every field, or, with a `base`, one bit per field
// and only the fields that differ from it. Reading with a base takes the unchanged
// fields from it. Fields the table does not name are left alone.
void netfields_serialize(NetBuf *b, const NetField *fields, int count, void *state, const void *base);

// Whether two structs agree on every field of the table.
bool netfields_equal(const NetField *fields, int count, const void *a, const void *b);

// The soldier's halves (soldier_copy_owned, soldier_copy_served). A received owned half
// is read into a scratch Soldier and copied in with soldier_copy_owned, which sets the
// animations' speed from the anims as the fields cannot.
extern const NetField SOLDIER_OWNED_FIELDS[];
extern const int SOLDIER_OWNED_COUNT;
extern const NetField SOLDIER_SERVED_FIELDS[];
extern const int SOLDIER_SERVED_COUNT;

// --- the messages ------------------------------------------------------------------

// Every message begins with its kind. Whether it goes reliably is the table's to say
// (MSG_RELIABLE), never a call site's: state (the client's, the snapshot) is sent over
// and over, unreliably, a lost one replaced by the next; news goes once, in order.
typedef enum MsgKind {
    MSG_INVALID,
    MSG_HELLO,        // client -> server: the version and the name
    MSG_WELCOME,      // server -> client: the slot, and the tick
    MSG_DENIED,       // server -> client: why not
    MSG_CHAT,         // either way: a line said, to everyone or the team; commands and votes too
    MSG_CLIENT_STATE, // client -> server, every tick: the owned half (stream.h)
    MSG_SNAPSHOT,     // server -> client, every tick: everyone's halves (stream.h)
    MSG_COUNT,
} MsgKind;

extern const bool MSG_RELIABLE[MSG_COUNT];

typedef struct MsgHello {
    uint16_t version;
    char name[NET_NAME_SIZE];
} MsgHello;

typedef struct MsgWelcome {
    uint8_t slot;
    uint32_t tick;
    char map[64]; // the map to load: the snapshots that follow are of it
} MsgWelcome;

typedef struct MsgDenied {
    char reason[NET_TEXT_SIZE];
} MsgDenied;

typedef struct MsgChat {
    uint8_t slot; // who said it; MAX_PLAYERS for the server
    bool team;
    char text[NET_TEXT_SIZE];
} MsgChat;

// The kind, first in every message; reading one past the table is bad.
void msg_kind(NetBuf *b, MsgKind *kind);
void msg_hello(NetBuf *b, MsgHello *m);
void msg_welcome(NetBuf *b, MsgWelcome *m);
void msg_denied(NetBuf *b, MsgDenied *m);
void msg_chat(NetBuf *b, MsgChat *m);
