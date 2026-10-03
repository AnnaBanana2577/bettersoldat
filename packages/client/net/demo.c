#include "net/demo.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "files.h"
#include "utils/utils.h"

#define DEMO_MAGIC "SRDM"
#define DEMO_TICKS_AT 12 // the header's ticks: after the magic, the versions and the date
#define DEMO_HEADER_SIZE (4 + 2 + 2 + 4 + 4 + 1 + NET_NAME_SIZE + NET_MAP_SIZE)

static void put_u16(uint8_t *at, uint16_t v)
{
    at[0] = (uint8_t)v;
    at[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *at, uint32_t v)
{
    for (int i = 0; i < 4; i++) at[i] = (uint8_t)(v >> (8 * i));
}

static uint16_t get_u16(const uint8_t *at) { return (uint16_t)(at[0] | at[1] << 8); }

static uint32_t get_u32(const uint8_t *at)
{
    return (uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16 | (uint32_t)at[3] << 24;
}

// The file's name without its directory or extension.
static void stem(char *out, size_t size, const char *path)
{
    const char *base = path;
    for (const char *c = path; *c; c++)
        if (*c == '/' || *c == '\\') base = c + 1;
    snprintf(out, size, "%s", base);
    char *dot = strrchr(out, '.');
    if (dot && dot != out) *dot = '\0';
}

void demo_path(char *out, size_t size, const char *name)
{
    bool path = strchr(name, '/') || strchr(name, '\\');
    size_t n = strlen(name), ext = strlen(DEMO_EXT);
    bool has_ext = n > ext && strcmp(name + n - ext, DEMO_EXT) == 0;
    snprintf(out, size, "%s%s%s%s", path ? "" : DEMO_DIR, path ? "" : "/", name, has_ext ? "" : DEMO_EXT);
}

void demo_default_name(char *out, size_t size, const char *map)
{
    time_t now = time(NULL);
    char date[32];
    strftime(date, sizeof date, "%Y-%m-%d_%H-%M-%S", localtime(&now));
    snprintf(out, size, "%s_%s", date, map);
}

// --- recording ---------------------------------------------------------------------

static void record(DemoRecorder *r, DemoRecordKind kind, const uint8_t *data, size_t size)
{
    if (!r->file || size > DEMO_RECORD_MAX) return;
    uint8_t head[3] = {(uint8_t)kind};
    put_u16(head + 1, (uint16_t)size);
    fwrite(head, 1, sizeof head, r->file);
    if (size) fwrite(data, 1, size, r->file);
}

bool demo_record_open(DemoRecorder *r, const char *path, const DemoHeader *header)
{
    demo_record_close(r);
    files_make_parents(path);
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    uint8_t head[DEMO_HEADER_SIZE] = {0};
    memcpy(head, DEMO_MAGIC, 4);
    put_u16(head + 4, DEMO_VERSION);
    put_u16(head + 6, NET_VERSION);
    put_u32(head + 8, header->date);
    put_u32(head + DEMO_TICKS_AT, 0);
    head[16] = header->slot;
    snprintf((char *)head + 17, NET_NAME_SIZE, "%s", header->name);
    snprintf((char *)head + 17 + NET_NAME_SIZE, NET_MAP_SIZE, "%s", header->map);
    if (fwrite(head, 1, sizeof head, f) != sizeof head) {
        fclose(f);
        return false;
    }
    *r = (DemoRecorder){.file = f};
    snprintf(r->path, sizeof r->path, "%s", path);
    stem(r->name, sizeof r->name, path);
    return true;
}

bool demo_recording(const DemoRecorder *r) { return r->file != NULL; }

void demo_record_packet(DemoRecorder *r, const uint8_t *data, size_t size)
{
    if (!r->file) return;
    record(r, DEMO_PACKET, data, size);
    r->unframed = true;
}

void demo_record_frame(DemoRecorder *r)
{
    if (!r->file || !r->unframed) return;
    record(r, DEMO_FRAME, NULL, 0);
    r->unframed = false;
}

// The snapshots the client keeps for its deltas, oldest first, each whole; the newest
// with the server's events the client has heard and not yet shown.
static void join_snapshots(DemoRecorder *r, const ClientStream *c)
{
    static uint8_t buf[DEMO_RECORD_MAX];
    static MsgSnapshot m; // large
    if (c->newest == 0) return;
    uint32_t from = c->newest >= STREAM_RING ? c->newest - STREAM_RING + 1 : 1;
    for (uint32_t t = from; t <= c->newest; t++) {
        int k = (int)(t % STREAM_RING);
        if (c->snap_tick[k] != t) continue;
        memset(&m, 0, sizeof m);
        m.round = c->round;
        m.tick = t;
        m.match = c->snap_match[k];
        memcpy(m.word, c->snap_word[k], sizeof m.word);
        memcpy(m.soldiers, c->snaps[k], sizeof m.soldiers);
        memcpy(m.thing_word, c->snap_thing_word[k], sizeof m.thing_word);
        memcpy(m.things, c->snap_things[k], sizeof m.things);
        for (int i = 0; i < MAX_PLAYERS; i++)
            if (m.word[i] == SNAP_STATE) snprintf(m.names[i], NET_NAME_SIZE, "%s", c->names[i]);

        NetBuf b = netbuf_writer(buf, sizeof buf);
        MsgKind kind = MSG_SNAPSHOT;
        msg_kind(&b, &kind);
        msg_snapshot(&b, &m, NULL);
        // the events, as wire_write lays them out
        const WirePending *p = &c->pending;
        uint32_t count = 0;
        if (t == c->newest)
            for (uint32_t seq = p->applied + 1; seq <= p->received && count < WIRE_PER_PACKET; seq++)
                if (p->seq[seq % WIRE_PENDING] == seq) count++;
        net_range(&b, &count, WIRE_PER_PACKET);
        for (uint32_t seq = p->applied + 1, n = 0; n < count && seq <= p->received; seq++) {
            if (p->seq[seq % WIRE_PENDING] != seq) continue;
            Event e = p->items[seq % WIRE_PENDING];
            uint32_t s = seq;
            net_u32(&b, &s);
            net_u32(&b, &e.tick);
            wire_event(&b, &e);
            n++;
        }
        if (netbuf_ok(&b)) demo_record_packet(r, buf, netbuf_bytes(&b));
    }
}

void demo_record_join(DemoRecorder *r, const ClientNet *n)
{
    if (!r->file) return;
    uint8_t buf[NET_MTU];
    if (n->weapons_heard) { // the server's weapons mod, before the world it is played in
        static MsgWeapons msgs[WEAPON_COUNT];
        int count = msg_weapons_fit(n->weapons, NET_MTU, msgs, WEAPON_COUNT);
        for (int i = 0; i < count; i++) {
            MsgKind weapons = MSG_WEAPONS;
            NetBuf w = netbuf_writer(buf, sizeof buf);
            msg_kind(&w, &weapons);
            msg_weapons(&w, &msgs[i]);
            if (netbuf_ok(&w)) demo_record_packet(r, buf, netbuf_bytes(&w));
        }
    }
    MsgKind kind = MSG_MAP;
    MsgMap map = {.round = n->round, .rope = n->rope};
    snprintf(map.map, sizeof map.map, "%s", n->map);
    snprintf(map.hostname, sizeof map.hostname, "%s", n->hostname);
    NetBuf b = netbuf_writer(buf, sizeof buf);
    msg_kind(&b, &kind);
    msg_map(&b, &map);
    if (netbuf_ok(&b)) demo_record_packet(r, buf, netbuf_bytes(&b));
    if (n->vote.kind != VOTE_NONE) {
        MsgVote vote = n->vote;
        kind = MSG_VOTE;
        b = netbuf_writer(buf, sizeof buf);
        msg_kind(&b, &kind);
        msg_vote(&b, &vote);
        if (netbuf_ok(&b)) demo_record_packet(r, buf, netbuf_bytes(&b));
    }
    demo_record_frame(r); // the world made for the map before its snapshots come
    join_snapshots(r, &n->stream);
    demo_record_frame(r);
}

void demo_record_tick(DemoRecorder *r, uint32_t view, const Command *cmd, Vec2 cursor, const Soldier *me)
{
    if (!r->file) return;
    uint8_t buf[2048];
    NetBuf b = netbuf_writer(buf, sizeof buf);
    uint32_t seq = cmd->seq, buttons = cmd->buttons;
    Vec2 aim = cmd->aim;
    bool soldier = me != NULL;
    net_u32(&b, &view);
    net_u32(&b, &seq);
    net_bits(&b, &buttons, 16);
    net_vec2(&b, &aim);
    net_vec2(&b, &cursor);
    net_bool(&b, &soldier);
    if (soldier) {
        Soldier *s = (Soldier *)me; // written, not changed
        uint8_t life = s->life;
        bool typing = s->typing;
        net_u8(&b, &life);
        netfields_serialize(&b, SOLDIER_OWNED_FIELDS, SOLDIER_OWNED_COUNT, s, NULL);
        netfields_serialize(&b, SOLDIER_LOADOUT_FIELDS, SOLDIER_LOADOUT_COUNT, s, NULL);
        netfields_serialize(&b, PLAYER_LOOK_FIELDS, PLAYER_LOOK_COUNT, &s->look, NULL);
        net_bool(&b, &typing);
        net_u16(&b, &s->hit_spray);
    }
    if (!netbuf_ok(&b)) return;
    record(r, DEMO_TICK, buf, netbuf_bytes(&b));
    r->ticks++;
}

void demo_record_close(DemoRecorder *r)
{
    if (!r->file) return;
    uint8_t ticks[4];
    put_u32(ticks, r->ticks);
    if (fseek(r->file, DEMO_TICKS_AT, SEEK_SET) == 0) fwrite(ticks, 1, sizeof ticks, r->file);
    fclose(r->file);
    r->file = NULL;
}

// --- playback ----------------------------------------------------------------------

static bool read_tick(const uint8_t *data, size_t size, DemoTick *t)
{
    NetBuf b = netbuf_reader(data, size);
    uint32_t buttons = 0;
    *t = (DemoTick){0};
    net_u32(&b, &t->view);
    net_u32(&b, &t->cmd.seq);
    net_bits(&b, &buttons, 16);
    net_vec2(&b, &t->cmd.aim);
    net_vec2(&b, &t->cursor);
    net_bool(&b, &t->soldier);
    t->cmd.buttons = (Buttons)buttons;
    if (t->soldier) {
        uint8_t life = 0;
        bool typing = false;
        net_u8(&b, &life);
        netfields_serialize(&b, SOLDIER_OWNED_FIELDS, SOLDIER_OWNED_COUNT, &t->self, NULL);
        netfields_serialize(&b, SOLDIER_LOADOUT_FIELDS, SOLDIER_LOADOUT_COUNT, &t->self, NULL);
        netfields_serialize(&b, PLAYER_LOOK_FIELDS, PLAYER_LOOK_COUNT, &t->self.look, NULL);
        net_bool(&b, &typing);
        net_u16(&b, &t->self.hit_spray);
        t->self.life = life;
        t->self.typing = typing;
    }
    return netbuf_done(&b);
}

typedef enum HeaderRead { HEADER_OK, HEADER_NOT_DEMO, HEADER_OTHER_VERSION } HeaderRead;

// The header from the file's first bytes, `size` of them.
static HeaderRead header_read(const uint8_t *data, size_t size, DemoHeader *h)
{
    if (size < DEMO_HEADER_SIZE || memcmp(data, DEMO_MAGIC, 4) != 0) return HEADER_NOT_DEMO;
    *h = (DemoHeader){.version = get_u16(data + 4), .net_version = get_u16(data + 6), .date = get_u32(data + 8),
                      .ticks = get_u32(data + DEMO_TICKS_AT), .slot = data[16]};
    if (h->version != DEMO_VERSION || h->net_version != NET_VERSION || h->slot >= MAX_PLAYERS) return HEADER_OTHER_VERSION;
    memcpy(h->name, data + 17, NET_NAME_SIZE);
    h->name[NET_NAME_SIZE - 1] = '\0';
    memcpy(h->map, data + 17 + NET_NAME_SIZE, NET_MAP_SIZE);
    h->map[NET_MAP_SIZE - 1] = '\0';
    return HEADER_OK;
}

bool demo_read_header(const char *path, DemoHeader *h)
{
    uint8_t head[DEMO_HEADER_SIZE];
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(head, 1, sizeof head, f);
    fclose(f);
    return header_read(head, n, h) == HEADER_OK;
}

static int newest_first(const void *a, const void *b)
{
    const DemoListing *x = a, *y = b;
    if (x->header.date != y->header.date) return x->header.date < y->header.date ? 1 : -1;
    return strcmp(y->name, x->name);
}

int demo_list(DemoListing *out, int max)
{
    static char names[512][64];
    int found = list_files(DEMO_DIR, DEMO_EXT, names, 512), count = 0;
    for (int i = 0; i < found && count < max; i++) {
        char path[256];
        demo_path(path, sizeof path, names[i]);
        if (!demo_read_header(path, &out[count].header)) continue;
        snprintf(out[count].name, sizeof out[count].name, "%s", names[i]);
        count++;
    }
    qsort(out, (size_t)count, sizeof *out, newest_first);
    return count;
}

bool demo_play_open(DemoPlayer *p, const char *path, char *error, size_t error_size)
{
    demo_play_close(p);
    size_t size = 0;
    uint8_t *data = file_read_all(path, &size);
    if (!data) {
        snprintf(error, error_size, "no demo at %s", path);
        return false;
    }
    DemoHeader h;
    HeaderRead read = header_read(data, size, &h);
    if (read != HEADER_OK) {
        snprintf(error, error_size, read == HEADER_NOT_DEMO ? "%s is not a demo" : "%s was recorded by another version of the game", path);
        free(data);
        return false;
    }
    *p = (DemoPlayer){.data = data, .size = size, .at = DEMO_HEADER_SIZE, .header = h};
    stem(p->name, sizeof p->name, path);
    if (h.ticks == 0) { // cut short: counted
        for (size_t at = DEMO_HEADER_SIZE; at + 3 <= size;) {
            size_t n = get_u16(data + at + 1);
            if (at + 3 + n > size) break;
            if (data[at] == DEMO_TICK) p->header.ticks++;
            at += 3 + n;
        }
    }
    return true;
}

bool demo_playing(const DemoPlayer *p) { return p->data != NULL; }

void demo_play_rewind(DemoPlayer *p)
{
    p->at = DEMO_HEADER_SIZE;
    p->tick = 0;
}

DemoNext demo_play_next(DemoPlayer *p, const uint8_t **data, size_t *size, DemoTick *tick)
{
    while (p->data && p->at + 3 <= p->size) {
        uint8_t kind = p->data[p->at];
        size_t n = get_u16(p->data + p->at + 1);
        const uint8_t *body = p->data + p->at + 3;
        if (p->at + 3 + n > p->size) break;
        p->at += 3 + n;
        switch (kind) {
        case DEMO_PACKET:
            *data = body;
            *size = n;
            return DEMO_NEXT_PACKET;
        case DEMO_FRAME: return DEMO_NEXT_FRAME;
        case DEMO_TICK:
            if (!read_tick(body, n, tick)) return DEMO_NEXT_END;
            p->tick++;
            return DEMO_NEXT_TICK;
        default: break; // a kind a later version wrote: passed over
        }
    }
    return DEMO_NEXT_END;
}

void demo_play_close(DemoPlayer *p)
{
    free(p->data);
    *p = (DemoPlayer){0};
}
