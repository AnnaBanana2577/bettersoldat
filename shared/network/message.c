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
