// Weapons mods: the server's numbers written as what differs from the game's own, in as
// many messages as fit the datagram and read back the same; the defaults file the game
// ships is the game's own numbers exactly; and a client joining a modded server takes them.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/systems/systems.h"
#include "host.h"
#include "net/client_net.h"
#include "test.h"

#define PORT 40051
#define DEFAULTS_FILE "runtime/config/defaults/weapons.server.cfg"

// Every weapon's numbers read back from the messages `stats` made.
static bool round_trip(const WeaponStats stats[WEAPON_COUNT], int *messages, size_t *largest)
{
    static MsgWeapons msgs[WEAPON_COUNT];
    static MsgWeapons heard;
    Weapons own;
    weapons_default(&own);
    weapons_stats(&own, heard.stats);
    *messages = msg_weapons_fit(stats, NET_MTU, msgs, WEAPON_COUNT);
    *largest = 0;
    for (int i = 0; i < *messages; i++) {
        uint8_t buf[NET_MTU];
        NetBuf b = netbuf_writer(buf, sizeof buf);
        MsgKind kind = MSG_WEAPONS;
        msg_kind(&b, &kind);
        msg_weapons(&b, &msgs[i]);
        if (!netbuf_ok(&b)) return false;
        if (netbuf_bytes(&b) > *largest) *largest = netbuf_bytes(&b);
        NetBuf r = netbuf_reader(buf, netbuf_bytes(&b));
        msg_kind(&r, &kind);
        msg_weapons(&r, &heard);
        if (!netbuf_done(&r)) return false;
    }
    return memcmp(heard.stats, stats, sizeof heard.stats) == 0;
}

// The defaults file's numbers, line by line, against the game's own.
static int file_differs(void)
{
    FILE *f = fopen(DEFAULTS_FILE, "rb");
    if (!f) return -1;
    Weapons own;
    weapons_default(&own);
    char line[1024];
    int lines = 0, wrong = 0;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "weapon \"", 8) != 0) continue;
        char *name = line + 8, *end = strchr(name, '"');
        if (!end) return -1;
        *end = '\0';
        WeaponId id = weapon_named(name);
        lines++;
        for (char *p = strtok(end + 1, " \r\n"); p; p = strtok(NULL, " \r\n")) {
            char *value = strtok(NULL, " \r\n");
            if (!value) break;
            for (int k = 0; k < WEAPON_FIELD_COUNT; k++) {
                const NetField *field = &WEAPON_FIELDS[k];
                if (strcmp(field->name, p) != 0) continue;
                const uint8_t *at = (const uint8_t *)&own.info[id].stats + field->offset;
                bool same = field->kind == NET_F32 ? *(const float *)at == (float)atof(value) : *(const int32_t *)at == atoi(value);
                if (!same) wrong++;
            }
        }
    }
    fclose(f);
    return lines == WEAPON_COUNT - 3 ? wrong : -1; // the three that follow others aren't listed
}

void weapons_mod_tests(void)
{
    Weapons own;
    weapons_default(&own);
    WeaponStats stats[WEAPON_COUNT];
    weapons_stats(&own, stats);
    int messages;
    size_t largest;
    CHECK(round_trip(stats, &messages, &largest) && messages == 1 && largest < 64,
          "the game's own numbers go in one small message (%d, %zu bytes)", messages, largest);
    for (int i = 0; i < WEAPON_COUNT; i++) { // every number of every weapon changed
        for (int k = 0; k < WEAPON_FIELD_COUNT; k++) {
            uint8_t *at = (uint8_t *)&stats[i] + WEAPON_FIELDS[k].offset;
            if (WEAPON_FIELDS[k].kind == NET_F32) *(float *)at += 0.5f + (float)k;
            else *(int32_t *)at += 3 + k;
        }
    }
    CHECK(round_trip(stats, &messages, &largest) && messages > 1 && largest <= NET_MTU,
          "a mod changing everything is split to fit the datagram and read back the same (%d messages, the largest %zu bytes)",
          messages, largest);
    CHECK(file_differs() == 0, "%s holds the game's own numbers for every weapon a mod may change", DEFAULTS_FILE);

    // a client joining a modded server takes its numbers
    CHECK(net_init(), "ENet starts");
    static Host host;
    HostSettings settings = {.port = PORT, .mode = MATCH_DEATHMATCH, .hostname = "weapons test", .quiet = true, .weapons_mod = true};
    snprintf(settings.data, sizeof settings.data, "%s", TEST_DATA);
    snprintf(settings.map, sizeof settings.map, "Arena");
    weapons_stats(&own, settings.weapons);
    settings.weapons[WEAPON_EAGLE].damage = 9.5f;
    settings.weapons[WEAPON_MINIGUN].fire_interval = 1;
    if (!host_open(&host, NULL, &settings)) {
        CHECK(false, "a host on port %d", PORT);
        return;
    }
    CHECK(host.game->ctx.weapons.info[WEAPON_EAGLE].stats.damage == 9.5f, "the server plays its mod");
    Console *con = console_create(NULL, NULL);
    static ClientNet client;
    client_net_init(&client);
    client_net_connect(&client, con, "127.0.0.1", PORT, "Tester", "");
    for (int i = 0; i < 300 && !client.weapons_heard; i++) {
        host_pump(&host, TICK_SECONDS);
        client_net_poll(&client, con, NULL);
    }
    CHECK(client.weapons_heard && client.weapons[WEAPON_EAGLE].damage == 9.5f && client.weapons[WEAPON_MINIGUN].fire_interval == 1 &&
              client.weapons[WEAPON_AK74].damage == own.info[WEAPON_AK74].stats.damage,
          "and a client joining it hears the mod, the rest the game's own");
    static Game g;
    weapons_default(&g.ctx.weapons);
    client_net_weapons(&client, &g);
    CHECK(g.ctx.weapons.info[WEAPON_EAGLE].stats.damage == 9.5f && g.ctx.weapons.info[WEAPON_CLUSTER_NADE].stats.damage ==
                                                                        g.ctx.weapons.info[WEAPON_FRAG].stats.damage,
          "which its world takes, the derived weapons following theirs");
    client_net_disconnect(&client, con);
    client_stream_free(&client.stream);
    host_close(&host);
    console_destroy(con);
    net_shutdown();
}
