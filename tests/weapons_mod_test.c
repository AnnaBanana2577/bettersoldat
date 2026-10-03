// Weapons mods: the server's numbers written as what differs from the game's own, in as
// many messages as fit the datagram and read back the same; and a client joining a modded
// server takes them.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/systems/systems.h"
#include "host.h"
#include "net/client_net.h"
#include "test.h"
#include "weapons_ini.h"

#define PORT 40051

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



// Soldat's keys, in WEAPON_FIELDS' order: the test's own copy, so it checks the reader's.
static const char *const INI_KEYS[] = {"Damage",      "FireInterval", "Ammo",         "ReloadTime",        "Speed",
                                       "StartUpTime", "Bink",         "MovementAcc",  "BulletSpread",      "Push",
                                       "InheritedVelocity", "ModifierHead", "ModifierChest", "ModifierLegs"};

static void count_passed(void *user, const char *what)
{
    (void)what;
    ++*(int *)user;
}

// Every number `path` gives, by a reading of its own, is the one weapons_ini_read took:
// how many it found, or -1 at the first that differs.
static int ini_matches(const char *path, const WeaponStats stats[WEAPON_COUNT])
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    char line[512];
    int weapon = -1, found = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n;")] = '\0';
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end) *end = '\0';
            weapon = -1;
            for (int id = 0; id < WEAPON_COUNT; id++)
                if (weapons_ini_section((WeaponId)id) && !strcmp(weapons_ini_section((WeaponId)id), line + 1)) weapon = id;
            continue;
        }
        char *eq = strchr(line, '=');
        if (weapon < 0 || !eq) continue;
        *eq = '\0';
        for (int k = 0; k < WEAPON_FIELD_COUNT; k++) {
            if (strcmp(line, INI_KEYS[k]) != 0) continue;
            const uint8_t *at = (const uint8_t *)&stats[weapon] + WEAPON_FIELDS[k].offset;
            bool same = WEAPON_FIELDS[k].kind == NET_F32 ? *(const float *)at == (float)atof(eq + 1)
                                                         : *(const int32_t *)at == atoi(eq + 1);
            if (!same) {
                fclose(f);
                return -1;
            }
            found++;
        }
    }
    fclose(f);
    return found;
}

static void weapons_ini_tests(void)
{
    Weapons own;
    weapons_default(&own);
    WeaponStats base[WEAPON_COUNT], read[WEAPON_COUNT];
    weapons_stats(&own, base);

    const char *path = "build/weapons-test.ini";
    remove(path);
    memcpy(read, base, sizeof read);
    char name[64] = "x";
    CHECK(weapons_ini_template(path) && weapons_ini_read(path, read, name, sizeof name, NULL, NULL) &&
              memcmp(read, base, sizeof read) == 0 && name[0] == '\0',
          "the template, all commented out, reads as the game's own");

    static const char SOLDAT[] = "; a Soldat mod\r\n"
                                 "[Info]\r\nName=Test Mod\r\nVersion=1\r\n"
                                 "[Barret M82A1]\r\nDamage=4.45\r\nBulletStyle=1\r\nFireInterval=230 // slower\r\n"
                                 "[Punch]\r\nAmmo=2\r\nRecoil=0\r\n"
                                 "[Grenade]\r\nSpeed=1.5\r\n"
                                 "[No Such Gun]\r\nDamage=1\r\n"
                                 "[Ak-74]\r\nWobble=3\r\n";
    FILE *f = fopen(path, "wb");
    if (f) fwrite(SOLDAT, 1, sizeof SOLDAT - 1, f), fclose(f);
    memcpy(read, base, sizeof read);
    int passed = 0;
    CHECK(weapons_ini_read(path, read, name, sizeof name, count_passed, &passed), "a Soldat weapons.ini reads");
    CHECK(read[WEAPON_BARRETT].damage == 4.45f && read[WEAPON_BARRETT].fire_interval == 230 && read[WEAPON_NONE].ammo == 2 &&
              read[WEAPON_FRAG].speed == 1.5f && read[WEAPON_BARRETT].ammo == base[WEAPON_BARRETT].ammo &&
              read[WEAPON_AK74].damage == base[WEAPON_AK74].damage && !strcmp(name, "Test Mod"),
          "by Soldat's names for the weapons, the rest the game's own");
    CHECK(passed == 2, "the section and the key it doesn't know are told of, Soldat's own passed over (%d)", passed);
    remove(path);

    memcpy(read, base, sizeof read);
    int found = weapons_ini_read("runtime/config/weapons.ini", read, name, sizeof name, NULL, NULL)
                    ? ini_matches("runtime/config/weapons.ini", read) : -1;
    CHECK(found == 20 * WEAPON_FIELD_COUNT, "the shipped weapons.ini (%s) gives every number as it has it (%d)", name, found);
}

void weapons_mod_tests(void)
{
    weapons_ini_tests();

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
