// The wire: the bits both ways, what a reader refuses, a struct whole and as a delta,
// the soldier's halves held to their copies, and the messages round trip.

#include <math.h>
#include <string.h>

#include "network/network.h"
#include "test.h"

static void bits_both_ways(void)
{
    uint8_t data[64];
    NetBuf w = netbuf_writer(data, sizeof data);
    uint32_t a = 5, b = 0xabcdef, c = 1;
    int32_t d = -3, e = -70000;
    bool f = true;
    float g = 3.25f;
    uint64_t h = 0x123456789abcdef0ull;
    char s[16] = "Major Pain";
    net_bits(&w, &a, 3);
    net_bits(&w, &b, 24);
    net_bits(&w, &c, 1);
    net_signed(&w, &d, 4);
    net_signed(&w, &e, 20);
    net_bool(&w, &f);
    net_f32(&w, &g);
    net_u64(&w, &h);
    net_string(&w, s, sizeof s);
    CHECK(netbuf_ok(&w), "everything fits");
    size_t bytes = netbuf_bytes(&w);
    CHECK(bytes == (3 + 24 + 1 + 4 + 20 + 1 + 32 + 64 + 8 + 80 + 7) / 8, "and takes exactly its bits (%zu bytes)", bytes);

    NetBuf r = netbuf_reader(data, bytes);
    uint32_t a2 = 0, b2 = 0, c2 = 0;
    int32_t d2 = 0, e2 = 0;
    bool f2 = false;
    float g2 = 0;
    uint64_t h2 = 0;
    char s2[16] = "";
    net_bits(&r, &a2, 3);
    net_bits(&r, &b2, 24);
    net_bits(&r, &c2, 1);
    net_signed(&r, &d2, 4);
    net_signed(&r, &e2, 20);
    net_bool(&r, &f2);
    net_f32(&r, &g2);
    net_u64(&r, &h2);
    net_string(&r, s2, sizeof s2);
    CHECK(a2 == a && b2 == b && c2 == c && d2 == d && e2 == e && f2 == f && g2 == g && h2 == h && strcmp(s2, s) == 0,
          "and reads back the same");
    CHECK(netbuf_done(&r), "with nothing left over");
    uint32_t more = 0;
    net_bits(&r, &more, 8);
    CHECK(!netbuf_ok(&r), "reading past the end is bad");
}

static void refusals(void)
{
    uint8_t data[16];
    uint32_t v = 300;
    NetBuf w = netbuf_writer(data, sizeof data);
    net_bits(&w, &v, 8);
    CHECK(!netbuf_ok(&w), "writing a value that doesn't fit its width is bad");

    w = netbuf_writer(data, sizeof data);
    float nan = NAN;
    net_f32(&w, &nan);
    NetBuf r = netbuf_reader(data, netbuf_bytes(&w));
    float got = 0;
    net_f32(&r, &got);
    CHECK(!netbuf_ok(&r), "a float that is not a number is refused");

    w = netbuf_writer(data, sizeof data);
    v = 7;
    net_bits(&w, &v, 3);
    r = netbuf_reader(data, netbuf_bytes(&w));
    uint32_t e = 0;
    net_range(&r, &e, 5);
    CHECK(!netbuf_ok(&r), "a value past its range is refused (read %u of at most 5)", e);

    w = netbuf_writer(data, sizeof data);
    char text[8] = "1234567";
    net_string(&w, text, sizeof text);
    r = netbuf_reader(data, netbuf_bytes(&w));
    char small[4];
    net_string(&r, small, sizeof small);
    CHECK(!netbuf_ok(&r), "a string longer than its room is refused");

    w = netbuf_writer(data, sizeof data);
    v = 1;
    net_bits(&w, &v, 4);
    net_bits(&w, &v, 4);
    net_bits(&w, &v, 8);
    r = netbuf_reader(data, netbuf_bytes(&w));
    net_bits(&r, &v, 8);
    CHECK(netbuf_ok(&r) && !netbuf_done(&r), "a message with bytes left over is not done");

    w = netbuf_writer(data, 2);
    net_bits(&w, &v, 32);
    CHECK(!netbuf_ok(&w), "writing past the buffer overflows");
}

typedef struct Sample {
    uint8_t a;
    int16_t b;
    uint32_t c;
    bool d;
    float e;
    Vec2 f;
    Rgba g;
    uint64_t h;
    int32_t untouched;
} Sample;

static const NetField SAMPLE_FIELDS[] = {
    NETFIELD(Sample, a, NET_U, 8),        NETFIELD(Sample, b, NET_I, 12),     NETFIELD_ENUM(Sample, c, 9),
    NETFIELD(Sample, d, NET_BOOL, 0),     NETFIELD(Sample, e, NET_F32, 0),    NETFIELD(Sample, f, NET_VEC2, 0),
    NETFIELD(Sample, g, NET_RGBA, 0),     NETFIELD(Sample, h, NET_U, 64),
};
#define SAMPLE_COUNT (int)(sizeof SAMPLE_FIELDS / sizeof SAMPLE_FIELDS[0])

static void fields_whole_and_delta(void)
{
    Sample base = {.a = 1, .b = -100, .c = 4, .d = true, .e = 1.5f, .f = {2, 3}, .g = {1, 2, 3, 4}, .h = 99, .untouched = 7};
    Sample now = base;
    now.b = 200;
    now.f.y = -8.0f;
    now.g.r = 250;

    uint8_t data[128];
    NetBuf w = netbuf_writer(data, sizeof data);
    netfields_serialize(&w, SAMPLE_FIELDS, SAMPLE_COUNT, &now, NULL);
    CHECK(netbuf_ok(&w), "a struct writes whole");
    size_t whole = netbuf_bytes(&w);
    Sample got = {.untouched = 42};
    NetBuf r = netbuf_reader(data, whole);
    netfields_serialize(&r, SAMPLE_FIELDS, SAMPLE_COUNT, &got, NULL);
    CHECK(netbuf_done(&r) && netfields_equal(SAMPLE_FIELDS, SAMPLE_COUNT, &got, &now) && got.untouched == 42,
          "and reads back the same, leaving what the table doesn't name alone");

    w = netbuf_writer(data, sizeof data);
    netfields_serialize(&w, SAMPLE_FIELDS, SAMPLE_COUNT, &now, &base);
    size_t delta = netbuf_bytes(&w);
    CHECK(netbuf_ok(&w) && delta < whole, "a delta against a baseline is smaller (%zu of %zu bytes)", delta, whole);
    Sample from_base = {0};
    r = netbuf_reader(data, delta);
    netfields_serialize(&r, SAMPLE_FIELDS, SAMPLE_COUNT, &from_base, &base);
    CHECK(netbuf_done(&r) && netfields_equal(SAMPLE_FIELDS, SAMPLE_COUNT, &from_base, &now),
          "and read against the same baseline gives the struct back");

    w = netbuf_writer(data, sizeof data);
    netfields_serialize(&w, SAMPLE_FIELDS, SAMPLE_COUNT, &now, &now);
    CHECK(netbuf_bytes(&w) == (SAMPLE_COUNT + 7) / 8, "nothing changed costs a bit a field");

    now.c = 12;
    w = netbuf_writer(data, sizeof data);
    netfields_serialize(&w, SAMPLE_FIELDS, SAMPLE_COUNT, &now, NULL);
    CHECK(!netbuf_ok(&w), "an enum past its last is bad");
}

// The soldier's halves hold to their copies: a half read from the wire and copied in
// gives what the copy from the original gives, so no field the copy carries is missing
// from the table.
static void soldier_halves(void)
{
    Game *g = scene("Arena", 60.0f, WEAPON_SPAS, WEAPON_AK74);
    settle(g);
    run(g, 30, press_fire);
    Soldier *src = &g->world.soldiers[0];
    src->held = 3;
    src->stat = 5;
    src->use_time = 4;
    src->look = (PlayerLook){.shirt = {1, 2, 3, 255}, .pants = {4, 5, 6, 255}, .skin = {7, 8, 9, 255}, .hair = {10, 11, 12, 255},
                             .jet = {13, 14, 15, 255}, .hair_style = 2, .head_style = 1, .chain_style = 2};
    src->kills = 3;
    src->rng = 0xdeadbeefcafef00dull;
    src->cmd_seq = 123456;

    uint8_t data[512];
    NetBuf w = netbuf_writer(data, sizeof data);
    netfields_serialize(&w, SOLDIER_OWNED_FIELDS, SOLDIER_OWNED_COUNT, src, NULL);
    CHECK(netbuf_ok(&w), "the owned half fits its widths");
    Soldier heard, applied, expected; // zeroed whole, padding and all, so they compare byte for byte
    memset(&heard, 0, sizeof heard);
    memset(&applied, 0, sizeof applied);
    memset(&expected, 0, sizeof expected);
    NetBuf r = netbuf_reader(data, netbuf_bytes(&w));
    netfields_serialize(&r, SOLDIER_OWNED_FIELDS, SOLDIER_OWNED_COUNT, &heard, NULL);
    soldier_copy_owned(g->ctx.anims, &applied, &heard);
    soldier_copy_owned(g->ctx.anims, &expected, src);
    CHECK(netbuf_done(&r) && memcmp(&applied, &expected, sizeof(Soldier)) == 0,
          "the owned half, read from the wire and copied in, is the copy of the original");

    w = netbuf_writer(data, sizeof data);
    netfields_serialize(&w, SOLDIER_SERVED_FIELDS, SOLDIER_SERVED_COUNT, src, NULL);
    CHECK(netbuf_ok(&w), "the served half fits its widths");
    memset(&heard, 0, sizeof heard);
    memset(&expected, 0, sizeof expected);
    r = netbuf_reader(data, netbuf_bytes(&w));
    netfields_serialize(&r, SOLDIER_SERVED_FIELDS, SOLDIER_SERVED_COUNT, &heard, NULL);
    soldier_copy_served(&expected, src);
    CHECK(netbuf_done(&r) && memcmp(&heard, &expected, sizeof(Soldier)) == 0,
          "the served half, read from the wire, is the copy of the original");

    // a tick's delta of the owned half is a few bytes
    Soldier before = *src;
    run(g, 1, press_nothing);
    w = netbuf_writer(data, sizeof data);
    netfields_serialize(&w, SOLDIER_OWNED_FIELDS, SOLDIER_OWNED_COUNT, src, &before);
    CHECK(netbuf_ok(&w) && netbuf_bytes(&w) < 40, "a tick's delta of the owned half is small (%zu bytes)", netbuf_bytes(&w));
    scene_free(g);
}

static void messages(void)
{
    uint8_t data[NET_MTU];
    NetBuf w = netbuf_writer(data, sizeof data);
    MsgKind kind = MSG_HELLO;
    MsgHello hello = {.version = NET_VERSION, .name = "Major Pain"};
    hello.choices.look.hair_style = 3;
    hello.choices.primary_choice = WEAPON_BARRETT;
    msg_kind(&w, &kind);
    msg_hello(&w, &hello);
    NetBuf r = netbuf_reader(data, netbuf_bytes(&w));
    MsgKind got_kind = MSG_INVALID;
    MsgHello got = {0};
    msg_kind(&r, &got_kind);
    msg_hello(&r, &got);
    CHECK(got_kind == MSG_HELLO && got.version == NET_VERSION && strcmp(got.name, hello.name) == 0 && netbuf_done(&r),
          "a Hello round trips, kind first");
    CHECK(got.choices.look.hair_style == 3 && got.choices.primary_choice == WEAPON_BARRETT, "with the player's choices");
    CHECK(MSG_RELIABLE[MSG_HELLO] && MSG_RELIABLE[MSG_CHAT] && !MSG_RELIABLE[MSG_SNAPSHOT] && !MSG_RELIABLE[MSG_CLIENT_STATE],
          "news is reliable and state is not, by the table");

    w = netbuf_writer(data, sizeof data);
    kind = MSG_CHAT;
    MsgChat chat = {.slot = MAX_PLAYERS, .team = true, .text = "gg"};
    msg_kind(&w, &kind);
    msg_chat(&w, &chat);
    r = netbuf_reader(data, netbuf_bytes(&w));
    MsgChat got_chat = {0};
    msg_kind(&r, &got_kind);
    msg_chat(&r, &got_chat);
    CHECK(got_kind == MSG_CHAT && got_chat.slot == MAX_PLAYERS && got_chat.team && strcmp(got_chat.text, "gg") == 0 && netbuf_done(&r),
          "a Chat round trips");

    data[0] = 0; // a kind of MSG_INVALID
    r = netbuf_reader(data, 1);
    msg_kind(&r, &got_kind);
    CHECK(!netbuf_ok(&r), "an invalid kind is refused");
}

void network_tests(void)
{
    bits_both_ways();
    refusals();
    fields_whole_and_delta();
    soldier_halves();
    messages();
}
