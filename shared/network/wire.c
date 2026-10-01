// The events on the wire: which travel, how they are laid out, and the queues.

#include <string.h>

#include "network/wire.h"

WireSide wire_side(EventType type)
{
    switch (type) {
    case EVENT_SHOT:
    case EVENT_WEAPON_DROP:
    case EVENT_FLAG_THROW: return WIRE_OWNER;
    case EVENT_DAMAGE:
    case EVENT_KILL:
    case EVENT_RESPAWN:
    case EVENT_FLAG_GRAB:
    case EVENT_FLAG_RETURN:
    case EVENT_FLAG_SCORE:
    case EVENT_KIT_PICKUP:
    case EVENT_WEAPON_PICKUP:
    case EVENT_MATCH_END:
    case EVENT_FLAG_DROP: return WIRE_SERVER;
    case EVENT_FIRE:
    case EVENT_BULLET_SPAWN:
    case EVENT_BULLET_END:
    case EVENT_WALL_HIT:
    case EVENT_RICOCHET:
    case EVENT_COLLIDER_HIT:
    case EVENT_GRENADE_BOUNCE:
    case EVENT_CLUSTER_SPLIT:
    case EVENT_BLOOD:
    case EVENT_EXPLOSION:
    case EVENT_HIT:
    case EVENT_KNIFE_LAND:
    case EVENT_THING_HIT:
    case EVENT_THING_KNOCK:
    case EVENT_POLY_EFFECT:
    case EVENT_CORPSE_HIT:
    case EVENT_ANTIC:
    case EVENT_ECHO_TEST: return WIRE_LOCAL;
    }
    return WIRE_LOCAL;
}

int wire_owner(const Event *e)
{
    switch (e->type) {
    case EVENT_SHOT: return e->shot.player;
    case EVENT_WEAPON_DROP: return e->weapon_drop.player;
    case EVENT_FLAG_THROW: return e->flag_throw.player;
    default: return -1;
    }
}

// --- the payloads ------------------------------------------------------------------

static void slot(NetBuf *b, uint8_t *v)
{
    uint32_t x = *v;
    net_range(b, &x, MAX_PLAYERS - 1);
    *v = (uint8_t)x;
}

static void weapon(NetBuf *b, WeaponId *v)
{
    uint32_t x = (uint32_t)*v;
    net_range(b, &x, WEAPON_COUNT - 1);
    *v = (WeaponId)x;
}

static void style(NetBuf *b, ThingStyle *v)
{
    uint32_t x = (uint32_t)*v;
    net_range(b, &x, THING_STYLE_COUNT - 1);
    *v = (ThingStyle)x;
}

static void team(NetBuf *b, Team *v)
{
    uint32_t x = (uint32_t)*v;
    net_range(b, &x, TEAM_COUNT - 1);
    *v = (Team)x;
}

static void i16(NetBuf *b, int32_t *v)
{
    net_signed(b, v, 16);
}

void wire_event(NetBuf *b, Event *e)
{
    uint32_t type = (uint32_t)e->type;
    net_range(b, &type, EVENT_ECHO_TEST);
    e->type = (EventType)type;
    switch (e->type) {
    case EVENT_SHOT: {
        EventShot *s = &e->shot;
        slot(b, &s->player);
        weapon(b, &s->weapon);
        net_vec2(b, &s->pos);
        net_vec2(b, &s->vel);
        net_f32(b, &s->damage);
        net_u32(b, &s->shot);
        net_bool(b, &s->self);
        break;
    }
    case EVENT_WEAPON_DROP: {
        EventWeaponDrop *d = &e->weapon_drop;
        slot(b, &d->player);
        weapon(b, &d->weapon);
        i16(b, &d->ammo);
        net_bool(b, &d->thrown);
        net_vec2(b, &d->pos);
        net_vec2(b, &d->impact);
        break;
    }
    case EVENT_FLAG_THROW: slot(b, &e->flag_throw.player); break;
    case EVENT_DAMAGE: {
        EventDamage *d = &e->damage;
        slot(b, &d->attacker);
        slot(b, &d->target);
        weapon(b, &d->weapon);
        net_f32(b, &d->amount);
        net_bool(b, &d->vest);
        break;
    }
    case EVENT_KILL: {
        EventKill *k = &e->kill;
        slot(b, &k->killer);
        slot(b, &k->target);
        weapon(b, &k->weapon);
        net_vec2(b, &k->pos);
        net_f32(b, &k->health);
        net_u8(b, &k->part);
        i16(b, &k->kills);
        net_f32(b, &k->distance);
        i16(b, &k->airtime);
        net_u8(b, &k->ricochets);
        break;
    }
    case EVENT_RESPAWN: {
        EventRespawn *r = &e->respawn;
        slot(b, &r->target);
        net_u8(b, &r->life);
        team(b, &r->team);
        weapon(b, &r->primary);
        weapon(b, &r->secondary);
        net_vec2(b, &r->pos);
        break;
    }
    case EVENT_FLAG_GRAB: {
        EventFlagGrab *f = &e->flag_grab;
        slot(b, &f->player);
        net_u8(b, &f->thing);
        style(b, &f->flag);
        net_vec2(b, &f->pos);
        break;
    }
    case EVENT_FLAG_RETURN: {
        EventFlagReturn *f = &e->flag_return;
        net_u8(b, &f->player); // 255: timed out
        style(b, &f->flag);
        net_vec2(b, &f->pos);
        break;
    }
    case EVENT_FLAG_SCORE: {
        EventFlagScore *f = &e->flag_score;
        slot(b, &f->player);
        style(b, &f->flag);
        net_vec2(b, &f->pos);
        break;
    }
    case EVENT_FLAG_DROP: {
        EventFlagDrop *f = &e->flag_drop;
        slot(b, &f->player);
        style(b, &f->flag);
        net_vec2(b, &f->pos);
        break;
    }
    case EVENT_KIT_PICKUP: {
        EventKitPickup *k = &e->kit_pickup;
        slot(b, &k->player);
        net_u8(b, &k->thing);
        style(b, &k->kit);
        net_vec2(b, &k->pos);
        break;
    }
    case EVENT_WEAPON_PICKUP: {
        EventWeaponPickup *p = &e->weapon_pickup;
        slot(b, &p->player);
        net_u8(b, &p->thing);
        weapon(b, &p->weapon);
        i16(b, &p->ammo);
        net_vec2(b, &p->pos);
        break;
    }
    case EVENT_MATCH_END: team(b, &e->match_end.winner); break;
    default: b->bad = true; break; // a local event has no place on the wire
    }
}

// --- the queues --------------------------------------------------------------------

void wire_queue_init(WireQueue *q)
{
    memset(q, 0, sizeof *q);
    q->first = q->next = 1;
}

static void push(WireQueue *q, const Event *e, uint8_t from)
{
    if (q->next - q->first == WIRE_QUEUE) q->first++; // the oldest goes: a receiver that far behind rejoins
    q->items[q->next % WIRE_QUEUE] = *e;
    q->from[q->next % WIRE_QUEUE] = from;
    q->next++;
}

void wire_collect(WireQueue *q, const Events *events, uint32_t tick, int only_owner)
{
    for (int i = 0; i < events->count; i++) {
        const Event *e = &events->items[i];
        WireSide side = wire_side(e->type);
        if (side == WIRE_LOCAL) continue;
        bool heard = e->tick != 0;
        int owner = wire_owner(e);
        if (only_owner >= 0 && (heard || side != WIRE_OWNER || owner != only_owner)) continue; // a client: its own decisions alone
        Event stamped = *e;
        if (!heard) stamped.tick = tick;
        push(q, &stamped, heard && owner >= 0 ? (uint8_t)owner : WIRE_FROM_HERE);
    }
}

uint32_t wire_queue_present(const WireQueue *q) { return q->next - 1; }

void wire_write(NetBuf *b, const WireQueue *q, uint32_t ack, int receiver, int max)
{
    if (max > WIRE_PER_PACKET) max = WIRE_PER_PACKET;
    uint32_t start = ack + 1 > q->first ? ack + 1 : q->first;
    uint32_t count = 0;
    for (uint32_t seq = start; seq < q->next && count < (uint32_t)max; seq++) {
        if (q->from[seq % WIRE_QUEUE] != receiver) count++;
    }
    net_range(b, &count, WIRE_PER_PACKET);
    uint32_t written = 0;
    for (uint32_t seq = start; seq < q->next && written < count; seq++) {
        if (q->from[seq % WIRE_QUEUE] == receiver) continue;
        Event e = q->items[seq % WIRE_QUEUE];
        uint32_t s = seq;
        net_u32(b, &s);
        net_u32(b, &e.tick);
        wire_event(b, &e);
        written++;
    }
}

// The advance a heard shot gets at `now`: from its own tick to then, half a second at most.
static void shot_advance(Event *e, uint32_t now)
{
    if (e->tick == 0) e->tick = 1; // heard, whatever it said: never mistaken for this tick's
    if (e->type != EVENT_SHOT) return;
    uint32_t behind = now > e->tick ? now - e->tick : 0;
    e->shot.advance = (uint8_t)(behind > WIRE_ADVANCE_MAX ? WIRE_ADVANCE_MAX : behind);
}

void wire_pending_init(WirePending *p) { memset(p, 0, sizeof *p); }

void wire_read_pending(NetBuf *b, WirePending *p)
{
    uint32_t count = 0;
    net_range(b, &count, WIRE_PER_PACKET);
    for (uint32_t i = 0; i < count && netbuf_ok(b); i++) {
        Event e = {0};
        uint32_t seq = 0;
        net_u32(b, &seq);
        net_u32(b, &e.tick);
        wire_event(b, &e);
        if (!netbuf_ok(b)) continue;
        // the first heard begins the count: what came before a newcomer is nobody's news
        if (p->received == 0 && p->applied == 0 && seq > 0) p->applied = seq - 1;
        if (seq <= p->applied || seq >= p->applied + WIRE_PENDING) continue;
        if (p->seq[seq % WIRE_PENDING] == seq) continue; // a resend of one still waiting
        p->items[seq % WIRE_PENDING] = e;
        p->seq[seq % WIRE_PENDING] = seq;
        if (seq > p->received) p->received = seq;
    }
}

void wire_pending_apply(WirePending *p, Game *g, uint32_t tick)
{
    for (uint32_t seq = p->applied + 1; seq <= p->received; seq++) {
        uint32_t k = seq % WIRE_PENDING;
        // A packet carries everything past the acknowledgement but the receiver's own
        // decisions, so a number missing below the newest received is one of those, not
        // a loss, and is passed over.
        if (p->seq[k] != seq) {
            p->applied = seq;
            continue;
        }
        Event e = p->items[k];
        if (e.tick > tick) return; // not yet
        p->seq[k] = 0;
        p->applied = seq;
        shot_advance(&e, tick);
        game_hear(g, e);
    }
}

void wire_read(NetBuf *b, Game *g, uint32_t *last, int only_owner)
{
    uint32_t count = 0;
    net_range(b, &count, WIRE_PER_PACKET);
    for (uint32_t i = 0; i < count && netbuf_ok(b); i++) {
        Event e = {0};
        uint32_t seq = 0;
        net_u32(b, &seq);
        net_u32(b, &e.tick);
        wire_event(b, &e);
        if (!netbuf_ok(b) || seq <= *last) continue;
        *last = seq;
        if (only_owner >= 0 && (wire_side(e.type) != WIRE_OWNER || wire_owner(&e) != only_owner)) continue;
        shot_advance(&e, g->world.tick);
        game_hear(g, e);
    }
}
