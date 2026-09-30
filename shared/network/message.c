// The messages: their kinds, which go reliably, and one routine each that reads and
// writes it.

#include "network/network.h"

const bool MSG_RELIABLE[MSG_COUNT] = {
    [MSG_INVALID] = false,
    [MSG_HELLO] = true,
    [MSG_WELCOME] = true,
    [MSG_DENIED] = true,
    [MSG_CHAT] = true,
    [MSG_MAP] = true,
    [MSG_CLIENT_STATE] = false,
    [MSG_SNAPSHOT] = false,
    [MSG_VOTE] = true,
};

void msg_kind(NetBuf *b, MsgKind *kind)
{
    uint32_t k = (uint32_t)*kind;
    net_range(b, &k, MSG_COUNT - 1);
    if (b->mode == NET_READ && k == MSG_INVALID) b->bad = true;
    *kind = (MsgKind)k;
}

void msg_hello(NetBuf *b, MsgHello *m)
{
    net_u16(b, &m->version);
    net_string(b, m->name, sizeof m->name);
    netfields_serialize(b, PLAYER_LOOK_FIELDS, PLAYER_LOOK_COUNT, &m->look, NULL);
    uint32_t primary = m->primary, secondary = m->secondary;
    net_range(b, &primary, WEAPON_COUNT - 1);
    net_range(b, &secondary, WEAPON_COUNT - 1);
    m->primary = (WeaponId)primary;
    m->secondary = (WeaponId)secondary;
}

void msg_welcome(NetBuf *b, MsgWelcome *m)
{
    uint32_t slot = m->slot;
    net_range(b, &slot, MAX_PLAYERS - 1);
    m->slot = (uint8_t)slot;
    net_u32(b, &m->tick);
}

void msg_map(NetBuf *b, MsgMap *m)
{
    net_u16(b, &m->round);
    net_string(b, m->map, sizeof m->map);
    net_string(b, m->hostname, sizeof m->hostname);
}

void msg_vote(NetBuf *b, MsgVote *m)
{
    uint32_t kind = (uint32_t)m->kind;
    net_range(b, &kind, VOTE_MAP);
    m->kind = (VoteKind)kind;
    net_string(b, m->target, sizeof m->target);
    net_string(b, m->starter, sizeof m->starter);
    net_string(b, m->reason, sizeof m->reason);
    net_u16(b, &m->seconds);
}

void msg_denied(NetBuf *b, MsgDenied *m) { net_string(b, m->reason, sizeof m->reason); }

void msg_chat(NetBuf *b, MsgChat *m)
{
    uint32_t slot = m->slot;
    net_range(b, &slot, MAX_PLAYERS); // MAX_PLAYERS: the server
    m->slot = (uint8_t)slot;
    net_bool(b, &m->team);
    net_string(b, m->text, sizeof m->text);
}
