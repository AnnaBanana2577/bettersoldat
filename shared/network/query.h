#pragma once

// The query: a server asked what it is playing, out of band, on its game port, by
// anyone (a server browser pinging it, the lobby checking it can be reached). One UDP
// datagram each way, outside ENet; the transport catches them before ENet sees them
// (net_answer_queries).
//
// Plain bytes, little-endian, not the game's bit-packed wire: the lobby reads these
// too, in another language, and the layout must hold still while the game's moves.
//
//   request  FF FF FF FF 'B' 'S' 'Q' 'i'  nonce:u32  zeros to QUERY_REQUEST_SIZE
//   reply    FF FF FF FF 'B' 'S' 'R' 'i'  nonce:u32  protocol:u16
//            players:u8 bots:u8 max_players:u8 mode:u8 flags:u8
//            hostname:(len:u8, bytes)  map:(len:u8, bytes)
//
// A request is padded to at least as long as any reply, so a forged source address
// gets its victim no more bytes than the forger sent. The nonce is the asker's, echoed,
// so a reply is matched to its request. Four 0xFF bytes in front are a header ENet
// itself never sends: a peer of 0xFFF with the compressed flag, which a host without a
// compressor (ours) drops.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "network/network.h"

#define QUERY_REQUEST_SIZE 128
#define QUERY_REPLY_MAX (12 + 2 + 5 + 1 + (NET_NAME_SIZE - 1) + 1 + (NET_MAP_SIZE - 1))
#define QUERY_FLAG_PASSWORD 1

_Static_assert(QUERY_REPLY_MAX <= QUERY_REQUEST_SIZE, "a reply is never longer than the request it answers");

typedef struct ServerInfo {
    uint16_t protocol; // NET_VERSION: whether this client can join it
    uint8_t players;   // people, bots apart
    uint8_t bots;
    uint8_t max_players;
    uint8_t mode;      // MatchMode; a newer server's may be past MATCH_MODE_COUNT
    bool password;
    char hostname[NET_NAME_SIZE];
    char map[NET_MAP_SIZE];
} ServerInfo;

// The four 0xFF bytes in front: one of ours, whatever follows, and not ENet's.
bool query_is_query(const uint8_t *data, size_t size);

// QUERY_REQUEST_SIZE bytes into `out`; 0 if it hasn't the room.
size_t query_write_request(uint8_t *out, size_t size, uint32_t nonce);
// A request, padded as it must be: true, with its nonce.
bool query_read_request(const uint8_t *data, size_t size, uint32_t *nonce);

// The reply's bytes into `out` (QUERY_REPLY_MAX is room enough); 0 if it hasn't the room.
// A name or map too long for the wire is cut.
size_t query_write_reply(uint8_t *out, size_t size, uint32_t nonce, const ServerInfo *info);
// A reply to the request with `nonce`, whole and nothing after: true, with what it says.
bool query_read_reply(const uint8_t *data, size_t size, uint32_t nonce, ServerInfo *info);
