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
// with `tick` unless it carries its own. `only_owner` keeps just that owner's own
// decisions (a client's); -1 keeps every wire event (the server's, which relays what it
// heard too, remembering from whom).
void wire_collect(WireQueue *q, const Events *events, uint32_t tick, int only_owner);

// The pending events for a receiver: those past `ack`, up to WIRE_PER_PACKET, skipping
// what was heard from `receiver` itself (its own decisions come back to it as state,
// not as events). Writes count, then each with its seq and tick.
void wire_write(NetBuf *b, const WireQueue *q, uint32_t ack, int receiver);

// Reads what wire_write wrote and applies each event not yet applied (by `*last`, which
// advances) into the game's mailbox; a shot's advance is the game's tick minus the
// shot's. With `only_owner` a slot, only that owner's own decisions are taken and the
// rest are dropped: a client speaks for its soldier alone. -1 takes everything.
void wire_read(NetBuf *b, Game *g, uint32_t *last, int only_owner);
