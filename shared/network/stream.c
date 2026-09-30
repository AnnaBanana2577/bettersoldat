// The two streams, both ends.

#include <stdlib.h>
#include <string.h>

#include "game/systems/systems.h"
#include "network/stream.h"

// --- the messages ------------------------------------------------------------------

void msg_client_state(NetBuf *b, MsgClientState *m, const Soldier *base)
{
    net_u32(b, &m->seq);
    net_u32(b, &m->base);
    net_u32(b, &m->ack);
    netfields_serialize(b, SOLDIER_OWNED_FIELDS, SOLDIER_OWNED_COUNT, &m->owned, base);
}

void msg_snapshot(NetBuf *b, MsgSnapshot *m, const Soldier *base, const uint8_t *base_word)
{
    net_u32(b, &m->tick);
    net_u32(b, &m->base);
    net_u32(b, &m->client_ack);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        uint32_t word = m->word[i];
        net_range(b, &word, SNAP_SAME);
        m->word[i] = (uint8_t)word;
        if (word != SNAP_STATE) continue;
        const Soldier *against = base && base_word && base_word[i] == SNAP_STATE ? &base[i] : NULL;
        netfields_serialize(b, SOLDIER_SERVED_FIELDS, SOLDIER_SERVED_COUNT, &m->soldiers[i], against);
        netfields_serialize(b, SOLDIER_OWNED_FIELDS, SOLDIER_OWNED_COUNT, &m->soldiers[i], against);
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

void server_stream_init(ServerStream *s) { memset(s, 0, sizeof *s); }

bool server_stream_receive(ServerStream *s, Game *g, int slot, const uint8_t *data, size_t size)
{
    NetBuf b = netbuf_reader(data, size);
    MsgKind kind;
    MsgClientState m;
    msg_kind(&b, &kind);
    net_u32(&b, &m.seq);
    net_u32(&b, &m.base);
    net_u32(&b, &m.ack);
    if (!netbuf_ok(&b) || m.seq <= s->newest) {
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
    if (!netbuf_done(&b) || soldier_out_of_bounds(&g->ctx, m.owned.pos)) {
        s->dropped++;
        return false;
    }

    s->ring[m.seq % STREAM_RING] = m.owned;
    s->ring_seq[m.seq % STREAM_RING] = m.seq;
    s->newest = m.seq;
    s->newest_tick = g->world.tick;
    if (m.ack > s->ack) s->ack = m.ack;

    // the owner's word, unless the soldier is dead here and the client hasn't heard
    Soldier *soldier = &g->world.soldiers[slot];
    if (soldier->active && !soldier->dead) soldier_copy_owned(g->ctx.anims, soldier, &m.owned);
    return true;
}

bool server_stream_quiet(const ServerStream *s, uint32_t tick)
{
    return s->newest == 0 || tick - s->newest_tick > STREAM_RELEASE_TICKS;
}

// The snapshot as `m` says, against its base; the bytes or 0 with the buffer overflowed.
static size_t snapshot_bytes(MsgSnapshot *m, const Soldier *base, const uint8_t *base_word, uint8_t *buf, size_t size)
{
    NetBuf b = netbuf_writer(buf, size);
    MsgKind kind = MSG_SNAPSHOT;
    msg_kind(&b, &kind);
    msg_snapshot(&b, m, base, base_word);
    return netbuf_ok(&b) ? netbuf_bytes(&b) : 0;
}

size_t server_stream_snapshot(ServerStream *s, const Game *g, int slot, uint8_t *buf, size_t size)
{
    const World *w = &g->world;
    MsgSnapshot m = {.tick = w->tick, .client_ack = s->newest};

    // the base: the snapshot the client has, if young enough and still in the history
    const Soldier *base = NULL;
    const uint8_t *base_word = NULL;
    if (s->ack && w->tick - s->ack <= STREAM_WHOLE_AFTER && s->sent_tick[s->ack % STREAM_RING] == s->ack) {
        base = history_at(w, s->ack);
        if (base) {
            base_word = s->sent_word[s->ack % STREAM_RING];
            m.base = s->ack;
        }
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        m.word[i] = w->soldiers[i].active ? SNAP_STATE : SNAP_GONE;
        m.soldiers[i] = w->soldiers[i];
    }

    // hold back the farthest soldiers, never the receiver's own, until it fits
    Vec2 here = w->soldiers[slot].pos;
    size_t n;
    while ((n = snapshot_bytes(&m, base, base_word, buf, size)) == 0) {
        int farthest = -1;
        float far = -1.0f;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (i == slot || m.word[i] != SNAP_STATE) continue;
            float d = vec2_length(vec2_sub(w->soldiers[i].pos, here));
            if (d > far) far = d, farthest = i;
        }
        if (farthest < 0) return 0; // not even alone
        m.word[farthest] = SNAP_SAME;
    }

    memcpy(s->sent_word[w->tick % STREAM_RING], m.word, sizeof m.word);
    s->sent_tick[w->tick % STREAM_RING] = w->tick;
    return n;
}

// --- the client's end --------------------------------------------------------------

bool client_stream_init(ClientStream *c)
{
    memset(c, 0, sizeof *c);
    c->snaps = calloc(STREAM_RING, sizeof *c->snaps);
    return c->snaps != NULL;
}

void client_stream_free(ClientStream *c)
{
    free(c->snaps);
    c->snaps = NULL;
}

void client_stream_reset(ClientStream *c)
{
    Soldier(*snaps)[MAX_PLAYERS] = c->snaps;
    memset(c, 0, sizeof *c);
    c->snaps = snaps;
    if (snaps) memset(snaps, 0, STREAM_RING * sizeof *snaps);
}

bool client_stream_hear(ClientStream *c, Game *g, int me, const uint8_t *data, size_t size)
{
    NetBuf b = netbuf_reader(data, size);
    MsgKind kind;
    MsgSnapshot m;
    msg_kind(&b, &kind);
    net_u32(&b, &m.tick);
    net_u32(&b, &m.base);
    net_u32(&b, &m.client_ack);
    if (!netbuf_ok(&b) || m.tick <= c->newest) {
        c->dropped++;
        return false;
    }
    const Soldier *base = NULL;
    const uint8_t *base_word = NULL;
    if (m.base) {
        if (c->snap_tick[m.base % STREAM_RING] != m.base) {
            c->dropped++;
            return false;
        }
        base = c->snaps[m.base % STREAM_RING];
        base_word = c->snap_word[m.base % STREAM_RING];
    }
    // the soldiers start from the base, where it carried them, so the delta lands on it
    memset(m.soldiers, 0, sizeof m.soldiers);
    if (base) {
        for (int i = 0; i < MAX_PLAYERS; i++)
            if (base_word[i] == SNAP_STATE) m.soldiers[i] = base[i];
    }
    // re-read from the top: the routine reads the header again, into the same values
    b = netbuf_reader(data, size);
    msg_kind(&b, &kind);
    msg_snapshot(&b, &m, base, base_word);
    if (!netbuf_done(&b)) {
        c->dropped++;
        return false;
    }

    World *w = &g->world;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Soldier *s = &w->soldiers[i];
        switch (m.word[i]) {
        case SNAP_GONE:
            if (i != me) s->active = false;
            break;
        case SNAP_STATE: {
            const Soldier *heard = &m.soldiers[i];
            bool placed = heard->life != s->life;
            soldier_copy_served(s, heard);
            if (i != me) {
                soldier_copy_owned(g->ctx.anims, s, heard);
                s->remote = true;
            } else if (placed) {
                soldier_copy_owned(g->ctx.anims, s, heard);
            }
            c->last_word[i] = m.tick;
            break;
        }
        default: break; // SNAP_SAME: keep stepping it
        }
    }

    memcpy(c->snaps[m.tick % STREAM_RING], m.soldiers, sizeof m.soldiers);
    memcpy(c->snap_word[m.tick % STREAM_RING], m.word, sizeof m.word);
    c->snap_tick[m.tick % STREAM_RING] = m.tick;
    c->newest = m.tick;
    if (m.client_ack > c->server_ack) c->server_ack = m.client_ack;
    return true;
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
    MsgClientState m = {.seq = seq, .base = base_seq, .ack = c->newest, .owned = *me};
    msg_kind(&b, &kind);
    msg_client_state(&b, &m, base);
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
