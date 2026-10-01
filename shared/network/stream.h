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
//
// The client's view (client_stream_begin_tick): its world's tick is the server tick of
// the frame it shows, and it keeps that `interp` ticks behind the newest snapshot it has,
// so the snapshot of each tick is in hand when the tick comes, whatever the line's
// jitter, and is applied then, with the server's events of that tick. A snapshot that
// comes after its tick has passed is a late one, and raises `interp` for a while; a tick
// with no snapshot steps everyone on. The clock itself runs free and is nudged a tick
// at a time when the frames in hand run consistently over or under.

#include "game/game.h"
#include "network/network.h"
#include "network/wire.h"

#define STREAM_RING 32          // states kept for deltas, each side
#define STREAM_WHOLE_AFTER 24   // a baseline older than this many states or ticks: whole
#define STREAM_RELEASE_TICKS 30 // no word for this long: the keys are let go
#define STREAM_SNAP_DISTANCE 64.0f // a correction this far is a placing to the eye: shown at once, not smoothed
#define STREAM_INTERP_MAX 8        // ticks the view keeps behind the newest snapshot, at most
#define STREAM_INTERP_SETTLE (5 * TICK_RATE) // no late snapshot for this long: a tick less behind
#define STREAM_VIEW_SNAP 8         // a view this far from where it should be jumps there
#define STREAM_VIEW_WINDOW 60      // ticks over which the frames in hand are watched before the clock is nudged
#define STREAM_VIEW_SLACK 2        // frames in hand beyond interp, at the leanest, before the view is nudged forward

// --- the messages ------------------------------------------------------------------

typedef struct MsgClientState {
    uint16_t round;     // the round it is of (MsgMap); another round's is dropped
    uint32_t seq;       // this state's number, the client's count from 1
    uint32_t base;      // the state it is a delta against, 0 for whole
    uint32_t ack;       // the newest snapshot (its tick) the client has, 0 for none
    uint32_t event_ack; // the newest of the server's events the client has applied
    uint8_t life;       // the life the soldier is on here: the word of an older life is not taken
    Soldier owned;      // the owned half, and the loadout choice, ride in a Soldier
    bool typing;        // the player is at the chat prompt: the dots over its head
    bool extrapolate;   // the player shows the others at now (cl_extrapolate): its shots are judged so
    // then the client's own decisions since the server's acknowledgement (wire.h)
} MsgClientState;

// The header and the half; `base` is the soldier the delta is against, NULL for whole;
// in reading, the fields it holds that didn't change are taken from it. The events
// follow, written and read with wire_write and wire_read.
void msg_client_state(NetBuf *b, MsgClientState *m, const Soldier *base);

typedef enum SnapWord {
    SNAP_GONE,  // no soldier in the slot
    SNAP_STATE, // the soldier's halves follow
    SNAP_SAME,  // nothing this time: keep stepping it
} SnapWord;

typedef struct MsgSnapshot {
    uint16_t round;            // the round it is of (MsgMap); another round's is dropped
    uint32_t tick;             // the snapshot's number
    uint32_t base;             // the snapshot it is a delta against, 0 for whole
    uint32_t client_ack;       // the newest client state (its seq) the server has from this client
    uint32_t client_event_ack; // the newest of the client's events the server has applied
    Match match;
    uint8_t word[MAX_PLAYERS]; // SnapWord
    Soldier soldiers[MAX_PLAYERS];
    char names[MAX_PLAYERS][NET_NAME_SIZE]; // sent with a soldier that goes whole
    uint8_t thing_word[MAX_THINGS];
    Thing things[MAX_THINGS];
    // then the server's events since the client's acknowledgement (wire.h)
} MsgSnapshot;

// What a snapshot is a delta against: the soldiers and things of the acknowledged
// snapshot and the words it carried; all NULL for whole. A slot the base did not carry
// (not SNAP_STATE) goes whole, and a soldier that goes whole brings its name.
typedef struct SnapBase {
    const Soldier *soldiers;
    const uint8_t *word;
    const Thing *things;
    const uint8_t *thing_word;
    const Match *match;
} SnapBase;

// The header, the match, the soldiers with their names, the things. The events follow,
// written and read with wire_write and wire_read.
void msg_snapshot(NetBuf *b, MsgSnapshot *m, const SnapBase *base);

// The command a soldier heard of steps on: its last keys and aim, one-shot buttons
// cleared, or no keys at all once `quiet`.
Command stream_command(const Soldier *s, bool quiet);

// --- the server's end, one per player ----------------------------------------------

typedef struct ServerStream {
    uint16_t round;            // the round this stream is of; set by server_stream_init
    Soldier ring[STREAM_RING]; // the client states received, by seq, for the deltas
    uint32_t ring_seq[STREAM_RING];
    uint32_t newest;      // the newest client state received (its seq), 0 for none
    uint32_t newest_tick; // the server tick it came in
    uint32_t ack;         // the newest snapshot the client has
    uint8_t sent_word[STREAM_RING][MAX_PLAYERS]; // what each snapshot sent carried, by tick
    uint8_t sent_thing_word[STREAM_RING][MAX_THINGS];
    Match sent_match[STREAM_RING];
    uint32_t sent_tick[STREAM_RING];
    uint32_t event_ack;  // the newest of the server's events the client has applied
    uint32_t event_last; // the newest of the client's events applied here
    uint32_t dropped;    // client states that couldn't be read, or failed a check
    uint32_t unwritable; // snapshots not sent because a value would not fit its width: a bug, not a size
} ServerStream;

// Fresh for `round`: nothing received, nothing sent, so the first snapshot goes whole.
void server_stream_init(ServerStream *s, uint16_t round);

// A client state for the soldier in `slot`: read against its base, checked, and taken
// as written, its events into the game's mailbox. False if dropped: old, unreadable,
// or off the map. The world's tick is when it came in.
bool server_stream_receive(ServerStream *s, Game *g, int slot, const uint8_t *data, size_t size);

// The snapshot for the player in `slot`, into `buf`, with the events of `events` it
// has not acknowledged and the players' `names`: the bytes, or 0 if nothing could fit.
// Soldiers and things are held back farthest first until it fits. The world's history
// must be recorded (World.history) for the deltas; without it every snapshot is whole.
size_t server_stream_snapshot(ServerStream *s, const Game *g, int slot, const WireQueue *events,
                              const char (*names)[NET_NAME_SIZE], uint8_t *buf, size_t size);

// Nothing heard for STREAM_RELEASE_TICKS.
bool server_stream_quiet(const ServerStream *s, uint32_t tick);

// --- the client's end --------------------------------------------------------------

typedef struct ClientStream {
    Soldier (*snaps)[MAX_PLAYERS]; // the snapshots received, by tick: STREAM_RING of them, on the heap
    Thing (*snap_things)[MAX_THINGS];
    uint8_t snap_word[STREAM_RING][MAX_PLAYERS];
    uint8_t snap_thing_word[STREAM_RING][MAX_THINGS];
    Match snap_match[STREAM_RING];
    uint32_t snap_tick[STREAM_RING];
    char names[MAX_PLAYERS][NET_NAME_SIZE]; // the players', as heard
    Soldier own[STREAM_RING]; // the states sent, by seq
    uint32_t own_seq[STREAM_RING];
    uint32_t seq;        // the last state sent
    uint32_t server_ack; // the newest state the server has
    uint32_t newest;     // the newest snapshot received (its tick), 0 for none
    uint32_t applied;    // the newest snapshot applied to the world (its tick), 0 for none
    int interp;          // ticks the view keeps behind the newest: the floor asked for, raised by late snapshots
    uint32_t late;       // snapshots that came after the view had passed their tick
    uint32_t late_tick;  // the newest's tick at the last of them, for settling back down
    uint32_t grew_tick;  // and at the last raise, so it is raised a tick a second at most
    uint32_t misses;     // ticks the view had no snapshot of, and stepped everyone on
    int32_t level_min;   // the fewest frames in hand over the window being watched
    int window;          // ticks of it left
    uint32_t skipped, held, resyncs; // the view clock's nudges forward and back, and its jumps
    uint32_t applies;    // snapshots applied to the world
    float correction;    // how far, all told, the applied snapshots moved the others from where stepping had them
    // The others at now (cl_extrapolate): each is stepped on from its last word by half
    // my ping and half its own, and its shots have flown that much longer than their
    // stamps say, less the half of its ping the stamp already stands behind.
    bool extrapolate;
    uint8_t lead[MAX_PLAYERS];  // ticks each is shown ahead of its last word (ping_lead)
    int8_t shift[MAX_PLAYERS];  // ticks each one's shots are further along than stamped (ping_shift)
    uint32_t word_applied[MAX_PLAYERS]; // the newest snapshot tick each soldier was taken from
    WirePending pending; // the server's events heard, each applied in the tick of its frame
    uint32_t last_word[MAX_PLAYERS]; // the snapshot tick each soldier was last heard of in
    // What a correction moved each soldier by, still to be shown: a new word snaps the
    // simulation but the picture glides, the offset shrinking to nothing over
    // cl_smooth (client_stream_smooth). A correction past STREAM_SNAP_DISTANCE snaps.
    Vec2 blend[MAX_PLAYERS];
    WireQueue out;       // my decisions, for the server
    uint32_t event_ack;  // the newest of my events the server has applied
    uint32_t event_last; // the newest of the server's events applied here
    uint16_t round;      // the round I am in (MsgMap); snapshots of another are dropped
    uint32_t dropped;    // snapshots that couldn't be read
    uint32_t stale;      // snapshots of another round
    uint32_t held_back;  // times a soldier I know was held back from a snapshot (SNAP_SAME)
    size_t largest;      // the largest snapshot heard, in bytes
} ClientStream;

bool client_stream_init(ClientStream *c); // allocates the ring; false if it couldn't
void client_stream_free(ClientStream *c);
// A round begins (a join, a new map): nothing heard, nothing sent, this round's from now.
void client_stream_reset(ClientStream *c, uint16_t round);

// A snapshot heard: read against its base and kept, with its events, for the tick that
// shows it. False if dropped.
bool client_stream_hear(ClientStream *c, Game *g, int me, const uint8_t *data, size_t size);

// Before the client's tick: the view clock set against the newest snapshot, keeping at
// least `interp` ticks behind it; the snapshot of the tick on show applied to the world
// (the match, the things, `me`'s served half, and its owned half only on a new life);
// every other soldier taken from its newest word and stepped on to the tick on show,
// and `extrapolate` ticks beyond it by half my ping and half its own, so a word that
// comes late moves nothing that stepping had right; and the server's events due by the
// tick into the game's mailbox.
void client_stream_begin_tick(ClientStream *c, Game *g, int me, int interp, bool extrapolate);

// After the client's tick: its own decisions among the tick's events, for the server.
void client_stream_collect(ClientStream *c, const Game *g, int me);

// Each frame: the offsets shrink, nine tenths of them gone `seconds` after a
// correction (none at all with 0). `dt` is the frame's seconds.
void client_stream_smooth(ClientStream *c, float dt, float seconds);

// The client's state, its soldier `me` as it stands and its decisions pending, into
// `buf`: the bytes, or 0.
size_t client_stream_state(ClientStream *c, const Soldier *me, uint8_t *buf, size_t size);

// Nothing heard of the soldier in `slot` for STREAM_RELEASE_TICKS of snapshots.
bool client_stream_quiet(const ClientStream *c, int slot);
