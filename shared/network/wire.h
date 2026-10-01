#pragma once

// The events on the wire (docs/netcode.md). The simulation's own event list is the
// vocabulary of what happens; a table here says which of them travel, and from whom:
//
//   WIRE_LOCAL   a consequence every machine produces for itself, or one system asking
//                another within a machine; never sent
//   WIRE_OWNER   a decision of the soldier's owner: its shot, its weapon or flag thrown.
//                A client sends its own to the server, which relays them to everyone
//                else and does them as its own
//   WIRE_SERVER  a decision only the server makes: a wound, a kill, a respawn, a
//                pickup, the round; sent to everyone, done by their passes
//
// Each side numbers the events it sends in a queue; a packet carries those the other
// side has not acknowledged, capped, so a lost packet is covered by the next. The
// receiver applies each once, by number, into the game's mailbox (game_hear), stamped
// with the tick it happened; a shot heard is run forward from then to now.

#include "game/game.h"
#include "network/network.h"

#define WIRE_QUEUE 128       // events kept to resend, each side
#define WIRE_PER_PACKET 32   // at most, per packet
#define WIRE_ADVANCE_MAX 30  // ticks a heard shot is run forward at most (half a second)

typedef enum WireSide { WIRE_LOCAL, WIRE_OWNER, WIRE_SERVER } WireSide;

WireSide wire_side(EventType type);
// The soldier whose decision an owner's event is (its slot), or -1.
int wire_owner(const Event *e);

// A wire event's type and payload, both ways. A local event is bad on the wire.
void wire_event(NetBuf *b, Event *e);

typedef struct WireQueue {
    Event items[WIRE_QUEUE]; // by seq
    uint8_t from[WIRE_QUEUE]; // the slot it was heard from, or WIRE_FROM_HERE
    uint32_t first;           // the oldest seq still kept
    uint32_t next;            // the seq the next event gets; the first is 1
} WireQueue;

#define WIRE_FROM_HERE 255

void wire_queue_init(WireQueue *q);

// After a tick: every event the tick left that travels goes into the queue, stamped
// with `tick`, a heard one too: the server relays a shot as of the tick it made the
// bullet, which is the frame that holds the shooter firing, so a client sees the
// bullet leave the soldier it draws. `only_owner` keeps just that owner's own decisions
// (a client's); -1 keeps every wire event (the server's, which relays what it heard
// too, remembering from whom).
void wire_collect(WireQueue *q, const Events *events, uint32_t tick, int only_owner);

// The pending events for a receiver: those past `ack`, up to `max` (WIRE_PER_PACKET at
// most), skipping what was heard from `receiver` itself (its own decisions come back
// to it as state, not as events). Writes count, then each with its seq and tick. What
// doesn't go now goes next time.
void wire_write(NetBuf *b, const WireQueue *q, uint32_t ack, int receiver, int max);

// The acknowledgement a newcomer starts with: everything so far counts as heard, since
// what happened before it came is nobody's news.
uint32_t wire_queue_present(const WireQueue *q);

// Reads what wire_write wrote and applies each event not yet applied (by `*last`, which
// advances) into the game's mailbox; a shot's advance is the game's tick minus the
// shot's. With `only_owner` a slot, only that owner's own decisions are taken and the
// rest are dropped: a client speaks for its soldier alone. -1 takes everything.
void wire_read(NetBuf *b, Game *g, uint32_t *last, int only_owner);

// The client's way in: what it hears is kept until its tick is due, so the server's
// decisions land in the tick of the frame they happened in, which the view shows some
// ticks after it arrives (stream.h). Each event is kept once by seq, and the newest
// kept is what the sender is told, so nothing held here comes again; one that arrives
// before there is room for it is not kept, and so not acknowledged, and comes again.
#define WIRE_PENDING 128

typedef struct WirePending {
    Event items[WIRE_PENDING]; // by seq
    uint32_t seq[WIRE_PENDING]; // the seq held in each slot, 0 for none
    uint32_t received;          // the newest seq kept: the acknowledgement
    uint32_t applied;           // the newest seq applied
} WirePending;

void wire_pending_init(WirePending *p);
// Reads what wire_write wrote into the ring; nothing is applied yet.
void wire_read_pending(NetBuf *b, WirePending *p);
// Applies, in order, every event due by `tick` into the game's mailbox, each once; one
// stamped past `tick` waits, and so does everything after it. A number never received
// below the newest is the receiver's own, which the sender leaves out, and is passed
// over. A shot's advance is `tick` minus its own.
void wire_pending_apply(WirePending *p, Game *g, uint32_t tick);
