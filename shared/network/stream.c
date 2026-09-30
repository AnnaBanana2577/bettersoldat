// The two streams, both ends.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/systems/systems.h"
#include "network/stream.h"

// --- the messages ------------------------------------------------------------------

void msg_client_state(NetBuf *b, MsgClientState *m, const Soldier *base)
{
    net_u16(b, &m->round);
    net_u32(b, &m->seq);
    net_u32(b, &m->base);
    net_u32(b, &m->ack);
    net_u32(b, &m->event_ack);
    netfields_serialize(b, SOLDIER_OWNED_FIELDS, SOLDIER_OWNED_COUNT, &m->owned, base);
    netfields_serialize(b, SOLDIER_LOADOUT_FIELDS, SOLDIER_LOADOUT_COUNT, &m->owned, base);
}

static const SnapBase NO_BASE = {0};

void msg_snapshot(NetBuf *b, MsgSnapshot *m, const SnapBase *base)
{
    if (!base) base = &NO_BASE;
    net_u16(b, &m->round);
    net_u32(b, &m->tick);
    net_u32(b, &m->base);
    net_u32(b, &m->client_ack);
    net_u32(b, &m->client_event_ack);
    netfields_serialize(b, MATCH_FIELDS, MATCH_COUNT, &m->match, base->match);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        uint32_t word = m->word[i];
        net_range(b, &word, SNAP_SAME);
        m->word[i] = (uint8_t)word;
        if (word != SNAP_STATE) continue;
        const Soldier *against = base->soldiers && base->word[i] == SNAP_STATE ? &base->soldiers[i] : NULL;
        netfields_serialize(b, SOLDIER_SERVED_FIELDS, SOLDIER_SERVED_COUNT, &m->soldiers[i], against);
        netfields_serialize(b, SOLDIER_OWNED_FIELDS, SOLDIER_OWNED_COUNT, &m->soldiers[i], against);
        if (!against) net_string(b, m->names[i], NET_NAME_SIZE); // a soldier going whole brings its name
    }
    for (int i = 0; i < MAX_THINGS; i++) {
        uint32_t word = m->thing_word[i];
        net_range(b, &word, SNAP_SAME);
        m->thing_word[i] = (uint8_t)word;
        if (word != SNAP_STATE) continue;
        const Thing *against = base->things && base->thing_word[i] == SNAP_STATE ? &base->things[i] : NULL;
        netfields_serialize(b, THING_FIELDS, THING_COUNT, &m->things[i], against);
    }
}

Command stream_command(const Soldier *s, bool quiet)
{
    return (Command){
        .seq = s->cmd_seq,
        .buttons = quiet ? 0 : (Buttons)(s->controls & ~BUTTONS_ONE_SHOT),
        .aim = vec2_sub(s->aim, s->vel), // the step leads the aim by the velocity again
    };
}

// --- the server's end --------------------------------------------------------------

void server_stream_init(ServerStream *s, uint16_t round)
{
    memset(s, 0, sizeof *s);
    s->round = round;
}

bool server_stream_receive(ServerStream *s, Game *g, int slot, const uint8_t *data, size_t size)
{
    NetBuf b = netbuf_reader(data, size);
    MsgKind kind;
    MsgClientState m;
    msg_kind(&b, &kind);
    net_u16(&b, &m.round);
    net_u32(&b, &m.seq);
    net_u32(&b, &m.base);
    net_u32(&b, &m.ack);
    net_u32(&b, &m.event_ack);
    if (!netbuf_ok(&b) || m.round != s->round || m.seq <= s->newest) {
        s->dropped++;
        return false;
    }
    const Soldier *base = NULL;
    if (m.base) {
        if (s->ring_seq[m.base % STREAM_RING] != m.base) { // a base we never had, or lost
            s->dropped++;
            return false;
        }
        base = &s->ring[m.base % STREAM_RING];
    }
    m.owned = base ? *base : (Soldier){0};
    netfields_serialize(&b, SOLDIER_OWNED_FIELDS, SOLDIER_OWNED_COUNT, &m.owned, base);
    netfields_serialize(&b, SOLDIER_LOADOUT_FIELDS, SOLDIER_LOADOUT_COUNT, &m.owned, base);
    if (!netbuf_ok(&b) || soldier_out_of_bounds(&g->ctx, m.owned.pos)) {
        s->dropped++;
        return false;
    }
    // its decisions, each once, into the mailbox: the passes do them next tick. Those of
    // a soldier not alive here are heard and dropped by the passes' own rules.
    uint32_t event_last = s->event_last;
    wire_read(&b, g, &event_last, slot);
    if (!netbuf_done(&b)) {
        s->dropped++;
        return false;
    }
    s->event_last = event_last;

    s->ring[m.seq % STREAM_RING] = m.owned;
    s->ring_seq[m.seq % STREAM_RING] = m.seq;
    s->newest = m.seq;
    s->newest_tick = g->world.tick;
    if (m.ack > s->ack) s->ack = m.ack;
    if (m.event_ack > s->event_ack) s->event_ack = m.event_ack;

    // the owner's word, unless the soldier is dead here and the client hasn't heard;
    // its loadout always, a weapon that isn't a primary or a secondary put right
    Soldier *soldier = &g->world.soldiers[slot];
    if (soldier->active && !soldier->dead) soldier_copy_owned(g->ctx.anims, soldier, &m.owned);
    soldier->primary_choice = weapon_is_primary(m.owned.primary_choice) ? m.owned.primary_choice : WEAPON_EAGLE;
    soldier->secondary_choice = weapon_is_secondary(m.owned.secondary_choice) ? m.owned.secondary_choice : WEAPON_KNIFE;
    return true;
}

bool server_stream_quiet(const ServerStream *s, uint32_t tick)
{
    return s->newest == 0 || tick - s->newest_tick > STREAM_RELEASE_TICKS;
}

// The snapshot as `m` says, against its base, with up to `event_max` of the events
// pending for `slot`; the bytes, or 0 with the buffer overflowed, `*bad` set instead if
// a value would not fit its width (which no holding back can mend).
static size_t snapshot_bytes(MsgSnapshot *m, const SnapBase *base, const WireQueue *events, uint32_t event_ack, int slot,
                             int event_max, uint8_t *buf, size_t size, bool *bad)
{
    NetBuf b = netbuf_writer(buf, size);
    MsgKind kind = MSG_SNAPSHOT;
    msg_kind(&b, &kind);
    msg_snapshot(&b, m, base);
    wire_write(&b, events, event_ack, slot, event_max);
    *bad = b.bad;
    return netbuf_ok(&b) ? netbuf_bytes(&b) : 0;
}

size_t server_stream_snapshot(ServerStream *s, const Game *g, int slot, const WireQueue *events,
                              const char (*names)[NET_NAME_SIZE], uint8_t *buf, size_t size)
{
    const World *w = &g->world;
    MsgSnapshot m = {.round = s->round, .tick = w->tick, .client_ack = s->newest, .client_event_ack = s->event_last, .match = g->match};

    // the base: the snapshot the client has, if young enough and still in the history
    SnapBase base = {0};
    const SnapBase *against = NULL;
    if (s->ack && w->tick - s->ack <= STREAM_WHOLE_AFTER && s->sent_tick[s->ack % STREAM_RING] == s->ack) {
        base.soldiers = history_at(w, s->ack);
        base.things = history_things_at(w, s->ack);
        if (base.soldiers && base.things) {
            base.word = s->sent_word[s->ack % STREAM_RING];
            base.thing_word = s->sent_thing_word[s->ack % STREAM_RING];
            base.match = &s->sent_match[s->ack % STREAM_RING];
            against = &base;
            m.base = s->ack;
        }
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        m.word[i] = w->soldiers[i].active ? SNAP_STATE : SNAP_GONE;
        m.soldiers[i] = w->soldiers[i];
        snprintf(m.names[i], NET_NAME_SIZE, "%s", names ? names[i] : "");
    }
    for (int i = 0; i < MAX_THINGS; i++) {
        m.thing_word[i] = w->things[i].style != THING_NONE ? SNAP_STATE : SNAP_GONE;
        m.things[i] = w->things[i];
    }

    // until it fits: fewer events first (they go next time regardless), then the
    // farthest soldier or thing held back, never the receiver's own soldier; what is held
    // back goes next time, whole if need be
    Vec2 here = w->soldiers[slot].pos;
    int event_max = WIRE_PER_PACKET;
    size_t n;
    bool bad;
    while ((n = snapshot_bytes(&m, against, events, s->event_ack, slot, event_max, buf, size, &bad)) == 0) {
        if (bad) { // a width too small somewhere: nothing to hold back would mend it, and a guess would cull
            s->unwritable++;
            return 0;
        }
        if (event_max > 0) {
            event_max /= 2;
            continue;
        }
        int soldier = -1, thing = -1;
        float far = -1.0f;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (i == slot || m.word[i] != SNAP_STATE) continue;
            float d = vec2_length(vec2_sub(w->soldiers[i].pos, here));
            if (d > far) far = d, soldier = i, thing = -1;
        }
        for (int i = 0; i < MAX_THINGS; i++) {
            if (m.thing_word[i] != SNAP_STATE) continue;
            float d = vec2_length(vec2_sub(w->things[i].pos[0], here));
            if (d > far) far = d, thing = i, soldier = -1;
        }
        if (thing >= 0) m.thing_word[thing] = SNAP_SAME;
        else if (soldier >= 0) m.word[soldier] = SNAP_SAME;
        else return 0; // not even alone
    }

    memcpy(s->sent_word[w->tick % STREAM_RING], m.word, sizeof m.word);
    memcpy(s->sent_thing_word[w->tick % STREAM_RING], m.thing_word, sizeof m.thing_word);
    s->sent_match[w->tick % STREAM_RING] = g->match;
    s->sent_tick[w->tick % STREAM_RING] = w->tick;
    return n;
}

// --- the client's end --------------------------------------------------------------

bool client_stream_init(ClientStream *c)
{
    memset(c, 0, sizeof *c);
    c->snaps = calloc(STREAM_RING, sizeof *c->snaps);
    c->snap_things = calloc(STREAM_RING, sizeof *c->snap_things);
    return c->snaps != NULL && c->snap_things != NULL;
}

void client_stream_free(ClientStream *c)
{
    free(c->snaps);
    free(c->snap_things);
    c->snaps = NULL;
    c->snap_things = NULL;
}

void client_stream_reset(ClientStream *c, uint16_t round)
{
    Soldier(*snaps)[MAX_PLAYERS] = c->snaps;
    Thing(*things)[MAX_THINGS] = c->snap_things;
    memset(c, 0, sizeof *c);
    c->snaps = snaps;
    c->snap_things = things;
    c->round = round;
    if (snaps) memset(snaps, 0, STREAM_RING * sizeof *snaps);
    if (things) memset(things, 0, STREAM_RING * sizeof *things);
    wire_queue_init(&c->out);
}

#define THING_TOLERANCE 10.0f // a thing's points are taken only when they disagree by more than this

// A thing as heard, onto the client's: what it is and whose, always; where its points
// are, only when they disagree with the client's own by more than a little, and never
// while it is held, since a held thing rides its holder here. A thing that appeared or
// changed kind is taken whole.
static void thing_apply(Thing *t, const Thing *heard)
{
    bool fresh = t->style != heard->style || t->points != heard->points;
    Thing was = *t;
    netfields_copy(THING_FIELDS, THING_COUNT, t, heard);
    if (fresh) return;
    bool keep = heard->holder != 0 || (vec2_length(vec2_sub(was.pos[0], heard->pos[0])) <= THING_TOLERANCE &&
                                       vec2_length(vec2_sub(was.pos[1], heard->pos[1])) <= THING_TOLERANCE);
    if (keep) {
        for (int k = 0; k < 4; k++) {
            t->pos[k] = was.pos[k];
            t->old_pos[k] = was.old_pos[k];
        }
    }
}

bool client_stream_hear(ClientStream *c, Game *g, int me, const uint8_t *data, size_t size)
{
    NetBuf b = netbuf_reader(data, size);
    MsgKind kind;
    MsgSnapshot m;
    msg_kind(&b, &kind);
    net_u16(&b, &m.round);
    net_u32(&b, &m.tick);
    net_u32(&b, &m.base);
    net_u32(&b, &m.client_ack);
    net_u32(&b, &m.client_event_ack);
    if (netbuf_ok(&b) && m.round != c->round) { // another round's: the Map that begins it hasn't come, or it is over
        c->stale++;
        return false;
    }
    if (!netbuf_ok(&b) || m.tick <= c->newest) {
        c->dropped++;
        return false;
    }
    SnapBase base = {0};
    const SnapBase *against = NULL;
    if (m.base) {
        if (c->snap_tick[m.base % STREAM_RING] != m.base) {
            c->dropped++;
            return false;
        }
        int k = (int)(m.base % STREAM_RING);
        base = (SnapBase){.soldiers = c->snaps[k], .word = c->snap_word[k], .things = c->snap_things[k],
                          .thing_word = c->snap_thing_word[k], .match = &c->snap_match[k]};
        against = &base;
    }
    // the soldiers and things start from the base, where it carried them, so the delta
    // lands on it; the match from the base's match
    memset(m.soldiers, 0, sizeof m.soldiers);
    memset(m.things, 0, sizeof m.things);
    memset(&m.match, 0, sizeof m.match);
    if (against) {
        for (int i = 0; i < MAX_PLAYERS; i++)
            if (base.word[i] == SNAP_STATE) m.soldiers[i] = base.soldiers[i];
        for (int i = 0; i < MAX_THINGS; i++)
            if (base.thing_word[i] == SNAP_STATE) m.things[i] = base.things[i];
        m.match = *base.match;
    }
    // re-read from the top: the routine reads the header again, into the same values
    b = netbuf_reader(data, size);
    msg_kind(&b, &kind);
    msg_snapshot(&b, &m, against);
    if (!netbuf_ok(&b)) {
        c->dropped++;
        return false;
    }

    World *w = &g->world;
    netfields_copy(MATCH_FIELDS, MATCH_COUNT, &g->match, &m.match); // the match is the server's
    // the world's tick keeps to the server's: a snapshot is of its tick, and arrives
    // about as far after it as my states take to get there, so now is its tick. Off by
    // a little the ticks drift back; off by more they snap.
    int32_t off = (int32_t)(w->tick - m.tick);
    if (off > 2 || off < -2) w->tick = m.tick;

    // the server's decisions, each once, into the mailbox for the next tick's passes
    uint32_t event_last = c->event_last;
    wire_read(&b, g, &event_last, -1);
    if (!netbuf_done(&b)) {
        c->dropped++;
        return false;
    }
    c->event_last = event_last;
    if (m.client_event_ack > c->event_ack) c->event_ack = m.client_event_ack;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Soldier *s = &w->soldiers[i];
        switch (m.word[i]) {
        case SNAP_GONE:
            if (i != me) s->active = false;
            break;
        case SNAP_STATE: {
            const Soldier *heard = &m.soldiers[i];
            bool placed = heard->life != s->life;
            Vec2 before = s->pos;
            if (i == me) {
                // the server's word of me, but my look and loadout are mine
                PlayerLook look = s->look;
                WeaponId primary = s->primary_choice, secondary = s->secondary_choice;
                soldier_copy_served(s, heard);
                s->look = look;
                s->primary_choice = primary;
                s->secondary_choice = secondary;
                if (placed) soldier_copy_owned(g->ctx.anims, s, heard);
            } else {
                soldier_copy_served(s, heard);
                soldier_copy_owned(g->ctx.anims, s, heard);
                s->remote = true;
                // the correction goes to the picture, to be shown over a little while;
                // a placing, or a jump too far to be a correction, shows at once
                Vec2 jump = vec2_sub(before, s->pos);
                c->blend[i] = placed ? vec2(0, 0) : vec2_add(c->blend[i], jump);
                if (vec2_length(c->blend[i]) > STREAM_SNAP_DISTANCE) c->blend[i] = vec2(0, 0);
            }
            c->last_word[i] = m.tick;
            if (m.names[i][0]) snprintf(c->names[i], NET_NAME_SIZE, "%s", m.names[i]);
            break;
        }
        default: // SNAP_SAME: keep stepping it
            if (s->active) c->held_back++;
            break;
        }
    }
    if (size > c->largest) c->largest = size;
    for (int i = 0; i < MAX_THINGS; i++) {
        Thing *t = &w->things[i];
        if (m.thing_word[i] == SNAP_GONE && t->style != THING_NONE) thing_kill(t);
        else if (m.thing_word[i] == SNAP_STATE) thing_apply(t, &m.things[i]);
    }

    memcpy(c->snaps[m.tick % STREAM_RING], m.soldiers, sizeof m.soldiers);
    memcpy(c->snap_word[m.tick % STREAM_RING], m.word, sizeof m.word);
    memcpy(c->snap_things[m.tick % STREAM_RING], m.things, sizeof m.things);
    memcpy(c->snap_thing_word[m.tick % STREAM_RING], m.thing_word, sizeof m.thing_word);
    c->snap_match[m.tick % STREAM_RING] = m.match;
    c->snap_tick[m.tick % STREAM_RING] = m.tick;
    c->newest = m.tick;
    if (m.client_ack > c->server_ack) c->server_ack = m.client_ack;
    return true;
}

void client_stream_collect(ClientStream *c, const Game *g, int me)
{
    wire_collect(&c->out, &g->events, g->world.tick - 1, me); // the tick just run
}

void client_stream_smooth(ClientStream *c, float dt, float seconds)
{
    // nine tenths gone after `seconds`: the factor per frame is that decay's dt-th part
    float keep = seconds > 0.0f ? expf(-2.302585f * dt / seconds) : 0.0f;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        c->blend[i] = vec2_scale(c->blend[i], keep);
        if (vec2_length(c->blend[i]) < 0.05f) c->blend[i] = vec2(0, 0);
    }
}

size_t client_stream_state(ClientStream *c, const Soldier *me, uint8_t *buf, size_t size)
{
    uint32_t seq = c->seq + 1;
    const Soldier *base = NULL;
    uint32_t base_seq = 0;
    if (c->server_ack && seq - c->server_ack <= STREAM_WHOLE_AFTER && c->own_seq[c->server_ack % STREAM_RING] == c->server_ack) {
        base = &c->own[c->server_ack % STREAM_RING];
        base_seq = c->server_ack;
    }

    NetBuf b = netbuf_writer(buf, size);
    MsgKind kind = MSG_CLIENT_STATE;
    MsgClientState m = {.round = c->round, .seq = seq, .base = base_seq, .ack = c->newest, .event_ack = c->event_last, .owned = *me};
    msg_kind(&b, &kind);
    msg_client_state(&b, &m, base);
    wire_write(&b, &c->out, c->event_ack, -1, WIRE_PER_PACKET);
    if (!netbuf_ok(&b)) return 0;

    c->own[seq % STREAM_RING] = *me;
    c->own_seq[seq % STREAM_RING] = seq;
    c->seq = seq;
    return netbuf_bytes(&b);
}

bool client_stream_quiet(const ClientStream *c, int slot)
{
    return c->last_word[slot] == 0 || c->newest - c->last_word[slot] > STREAM_RELEASE_TICKS;
}
