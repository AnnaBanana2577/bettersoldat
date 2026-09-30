#pragma once

// The two streams (docs/netcode.md): the client's state every tick, the server's
// snapshot every tick, both unreliable and both delta-compressed against the newest
// the other side acknowledged, the acknowledgement riding in the packet going the other
// way. Both ends of both streams are here, so the tests can drive either.
//
// Client state: the owned half of the client's soldier, numbered; the server takes it
// as written after its checks (soldier_copy_owned). It is a delta against the client's
// own earlier state the server last acknowledged, which both keep in a ring; whole
// when there is none young enough.
//
// Snapshot: numbered by the server's tick. For every slot a word: no soldier, the
// soldier's halves, or nothing this time (held back to fit the datagram; the client
// keeps stepping it). A soldier's halves are a delta against the snapshot the client
// last acknowledged, if that snapshot carried the soldier, and whole otherwise; the
// server deltas against what it sent, out of the world's history ring, and the client
// against what it received. The client applies the served half of everyone and the
// owned half of everyone but itself, and its own only on a new life: a placing.
//
// Between words, everyone steps a soldier heard of on its last keys (stream_command),
// one-shot buttons cleared so a throw is not thrown again; after STREAM_RELEASE_TICKS
// of silence the keys are let go and it falls and stops.

#include "game/game.h"
#include "network/network.h"

#define STREAM_RING 32          // states kept for deltas, each side
#define STREAM_WHOLE_AFTER 24   // a baseline older than this many states or ticks: whole
#define STREAM_RELEASE_TICKS 30 // no word for this long: the keys are let go
#define NET_MAP_SIZE 64

// --- the messages ------------------------------------------------------------------

typedef struct MsgClientState {
    uint32_t seq;  // this state's number, the client's count from 1
    uint32_t base; // the state it is a delta against, 0 for whole
    uint32_t ack;  // the newest snapshot (its tick) the client has, 0 for none
    Soldier owned; // the owned half rides in a Soldier
} MsgClientState;

// `base` is the soldier the delta is against, NULL for whole; in reading, the fields
// it holds that didn't change are taken from it.
void msg_client_state(NetBuf *b, MsgClientState *m, const Soldier *base);

typedef enum SnapWord {
    SNAP_GONE,  // no soldier in the slot
    SNAP_STATE, // the soldier's halves follow
    SNAP_SAME,  // nothing this time: keep stepping it
} SnapWord;

typedef struct MsgSnapshot {
    uint32_t tick;       // the snapshot's number
    uint32_t base;       // the snapshot it is a delta against, 0 for whole
    uint32_t client_ack; // the newest client state (its seq) the server has from this client
    uint8_t word[MAX_PLAYERS];
    Soldier soldiers[MAX_PLAYERS];
} MsgSnapshot;

// `base` is the base snapshot's soldiers and `base_word` its words, both NULL for whole;
// a slot the base did not carry (not SNAP_STATE) goes whole.
void msg_snapshot(NetBuf *b, MsgSnapshot *m, const Soldier *base, const uint8_t *base_word);

// The command a soldier heard of steps on: its last keys and aim, one-shot buttons
// cleared, or no keys at all once `quiet`.
Command stream_command(const Soldier *s, bool quiet);

// --- the server's end, one per player ----------------------------------------------

typedef struct ServerStream {
    Soldier ring[STREAM_RING]; // the client states received, by seq, for the deltas
    uint32_t ring_seq[STREAM_RING];
    uint32_t newest;      // the newest client state received (its seq), 0 for none
    uint32_t newest_tick; // the server tick it came in
    uint32_t ack;         // the newest snapshot the client has
    uint8_t sent_word[STREAM_RING][MAX_PLAYERS]; // what each snapshot sent carried, by tick
    uint32_t sent_tick[STREAM_RING];
    uint32_t dropped; // client states that couldn't be read, or failed a check
} ServerStream;

void server_stream_init(ServerStream *s);

// A client state for the soldier in `slot`: read against its base, checked, and taken
// as written. False if dropped: old, unreadable, or off the map. The world's tick is
// when it came in.
bool server_stream_receive(ServerStream *s, Game *g, int slot, const uint8_t *data, size_t size);

// The snapshot for the player in `slot`, into `buf`: the bytes, or 0 if nothing could
// fit. Soldiers are held back farthest first until it fits. The world's history must
// be recorded (World.history) for the deltas; without it every snapshot is whole.
size_t server_stream_snapshot(ServerStream *s, const Game *g, int slot, uint8_t *buf, size_t size);

// Nothing heard for STREAM_RELEASE_TICKS.
bool server_stream_quiet(const ServerStream *s, uint32_t tick);

// --- the client's end --------------------------------------------------------------

typedef struct ClientStream {
    Soldier (*snaps)[MAX_PLAYERS]; // the snapshots received, by tick: STREAM_RING of them, on the heap
    uint8_t snap_word[STREAM_RING][MAX_PLAYERS];
    uint32_t snap_tick[STREAM_RING];
    Soldier own[STREAM_RING]; // the states sent, by seq
    uint32_t own_seq[STREAM_RING];
    uint32_t seq;        // the last state sent
    uint32_t server_ack; // the newest state the server has
    uint32_t newest;     // the newest snapshot received (its tick), 0 for none
    uint32_t last_word[MAX_PLAYERS]; // the snapshot tick each soldier was last heard of in
    uint32_t dropped;    // snapshots that couldn't be read
} ClientStream;

bool client_stream_init(ClientStream *c); // allocates the ring; false if it couldn't
void client_stream_free(ClientStream *c);
void client_stream_reset(ClientStream *c); // a new join: nothing heard, nothing sent

// A snapshot heard, applied to the world; `me` is the client's slot. False if dropped.
bool client_stream_hear(ClientStream *c, Game *g, int me, const uint8_t *data, size_t size);

// The client's state, its soldier `me` as it stands, into `buf`: the bytes, or 0.
size_t client_stream_state(ClientStream *c, const Soldier *me, uint8_t *buf, size_t size);

// Nothing heard of the soldier in `slot` for STREAM_RELEASE_TICKS of snapshots.
bool client_stream_quiet(const ClientStream *c, int slot);
