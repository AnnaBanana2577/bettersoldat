#pragma once

// Demos (the original's Demo.pas): a game as this client saw it, kept in a file and
// played back. A demo is what the server said, every packet as it came, between marks
// of where the client's frames and ticks fell among them; and, each tick, my own part,
// which the server never says back to me: my command and my soldier as the tick left
// it. Played back, the packets go down the same road the line's do (client_net_feed),
// at the same frames and ticks, so the world is made from them as it was; my soldier
// steps on my commands, so my shots fly as they flew, and is put where it stood.
//
// A file (.srdm, in demos/) is a header, then records: a kind byte, a little-endian
// u16 size, and that many bytes.
//
//   header  "SRDM", u16 DEMO_VERSION, u16 NET_VERSION, u32 when it began (Unix
//           seconds), u32 its ticks (written as it closes; a file cut short says 0 and
//           is counted as it opens), u8 my slot, my name (NET_NAME_SIZE bytes), the map
//           (NET_MAP_SIZE bytes)
//   packet  a message as the server sent it, its kind first
//   frame   the frame's packets are all in: what they began (a map, a round's end, a
//           vote, a line of chat) is taken now, before the ticks
//   tick    one tick: the tick on show, my command, my cursor, and my soldier if it is
//           in the game (its owned half, its loadout, look and typing, and its life)
//
// A demo begun mid-round (demo_record_join) starts with the round's Map, the vote on,
// and the snapshots the client keeps for its deltas, whole, with the server's events
// still to show, so the packets that follow have every base they were sent against.
// Only a demo of this NET_VERSION plays: the packets are its wire.

#include <stdio.h>

#include "net/client_net.h"

#define DEMO_VERSION 1
#define DEMO_DIR "demos"
#define DEMO_EXT ".srdm"
#define DEMO_RECORD_MAX 65535 // bytes in a record

typedef enum DemoRecordKind { DEMO_PACKET = 1, DEMO_FRAME, DEMO_TICK } DemoRecordKind;

typedef struct DemoHeader {
    uint16_t version, net_version;
    uint32_t date;  // when it began, Unix seconds
    uint32_t ticks; // how long it is
    uint8_t slot;   // mine
    char name[NET_NAME_SIZE];
    char map[NET_MAP_SIZE];
} DemoHeader;

// A tick of mine.
typedef struct DemoTick {
    uint32_t view; // the tick on show, as the view clock had it (ClientStream.view_at)
    Command cmd;   // my command
    Vec2 cursor;   // my cursor, in the view's units, for the camera
    bool soldier;  // my soldier was in the game: `self` holds it
    Soldier self;  // its owned half, loadout, look, typing and life, as the tick left it
} DemoTick;

// --- recording ---------------------------------------------------------------------

typedef struct DemoRecorder {
    FILE *file; // NULL while not recording
    char path[256];
    char name[64];  // the file's, without directory or extension: the scoreboard shows it
    uint32_t ticks;
    bool unframed;  // packets written since the last frame record
} DemoRecorder;

// A file begun at `path` with `header` (its version, net version and ticks set here).
// False, with nothing open, if it couldn't be written.
bool demo_record_open(DemoRecorder *r, const char *path, const DemoHeader *header);
bool demo_recording(const DemoRecorder *r);
// The round as the client has it, mid-way: its Map, its vote, its snapshots in hand.
void demo_record_join(DemoRecorder *r, const ClientNet *n);
void demo_record_packet(DemoRecorder *r, const uint8_t *data, size_t size);
// The frame's packets are in; nothing is written if there were none.
void demo_record_frame(DemoRecorder *r);
// After my tick: `me` is my soldier, NULL if it isn't in the game.
void demo_record_tick(DemoRecorder *r, uint32_t view, const Command *cmd, Vec2 cursor, const Soldier *me);
// The ticks written into the header, and the file closed.
void demo_record_close(DemoRecorder *r);

// --- playback ----------------------------------------------------------------------

typedef struct DemoPlayer {
    uint8_t *data; // the whole file; NULL while not playing
    size_t size, at;
    DemoHeader header; // its ticks counted, if the file didn't say
    uint32_t tick;     // ticks played
    char name[64];     // the file's, without directory or extension
} DemoPlayer;

typedef enum DemoNext { DEMO_NEXT_END, DEMO_NEXT_PACKET, DEMO_NEXT_FRAME, DEMO_NEXT_TICK } DemoNext;

// The file at `path`, read whole. False, with `error` saying why, if it can't be
// played.
bool demo_play_open(DemoPlayer *p, const char *path, char *error, size_t error_size);
bool demo_playing(const DemoPlayer *p);
// The next record: a packet into `data` and `size`, which stay good until the player
// closes; a tick into `tick`. The end at the file's end, or at a record that is cut
// short or can't be read.
DemoNext demo_play_next(DemoPlayer *p, const uint8_t **data, size_t *size, DemoTick *tick);
void demo_play_close(DemoPlayer *p);

// Back to the start, for a seek backward: the world is made again from its first Map.
void demo_play_rewind(DemoPlayer *p);

// --- the demos kept ----------------------------------------------------------------

// A demo in demos/, as its header says: for the main menu's list.
typedef struct DemoListing {
    char name[64]; // its file's, without the extension: what playdemo takes
    DemoHeader header; // ticks 0 for a file cut short, whose length isn't known without reading it all
} DemoListing;

// The header of the demo at `path`, read alone. False if it isn't a demo this version plays.
bool demo_read_header(const char *path, DemoHeader *h);
// The demos in demos/ this version plays, newest first, up to `max`: how many.
int demo_list(DemoListing *out, int max);

// The path of the demo `name` (a name without directory or extension, or a path).
void demo_path(char *out, size_t size, const char *name);
// A name for a demo begun now on `map`: the date and time, then the map.
void demo_default_name(char *out, size_t size, const char *map);
