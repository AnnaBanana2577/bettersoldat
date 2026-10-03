#include "weapons_ini.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h" // the launcher's
#include "game/systems/systems.h"
#include "network/network.h"

// Soldat's name for each weapon a mod may change, its IniName (OpenSoldat's Weapons.pas):
// mostly the weapon's own, but not always.
static const char *const SECTIONS[WEAPON_COUNT] = {
    [WEAPON_NONE] = "Punch",           [WEAPON_EAGLE] = "Desert Eagles",  [WEAPON_MP5] = "HK MP5",
    [WEAPON_AK74] = "Ak-74",           [WEAPON_STEYR] = "Steyr AUG",      [WEAPON_SPAS] = "Spas-12",
    [WEAPON_RUGER] = "Ruger 77",       [WEAPON_M79] = "M79",              [WEAPON_BARRETT] = "Barret M82A1",
    [WEAPON_M249] = "FN Minimi",       [WEAPON_MINIGUN] = "XM214 Minigun", [WEAPON_COLT] = "USSOCOM",
    [WEAPON_KNIFE] = "Combat Knife",   [WEAPON_CHAINSAW] = "Chainsaw",    [WEAPON_LAW] = "M72 LAW",
    [WEAPON_BOW2] = "Flamed Arrows",   [WEAPON_BOW] = "Rambo Bow",        [WEAPON_FLAMER] = "Flamer",
    [WEAPON_M2] = "Stationary Gun",    [WEAPON_FRAG] = "Grenade",
};

// Soldat's key for each of WEAPON_FIELDS, in its order.
static const char *const KEYS[] = {"Damage",      "FireInterval", "Ammo",         "ReloadTime",        "Speed",
                                   "StartUpTime", "Bink",         "MovementAcc",  "BulletSpread",      "Push",
                                   "InheritedVelocity", "ModifierHead", "ModifierChest", "ModifierLegs"};
// Soldat's, which aren't this game's to change
static const char *const PASSED[] = {"BulletStyle", "Recoil", "NoCollision"};

bool weapons_moddable(WeaponId id) { return id != WEAPON_CLUSTER_NADE && id != WEAPON_CLUSTER && id != WEAPON_THROWN_KNIFE; }

const char *weapons_ini_section(WeaponId id) { return id >= 0 && id < WEAPON_COUNT ? SECTIONS[id] : NULL; }

static bool same(const char *a, const char *b)
{
    for (; *a && tolower((unsigned char)*a) == tolower((unsigned char)*b); a++, b++) {}
    return !*a && !*b;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

static void say(void (*pass)(void *, const char *), void *user, const char *fmt, const char *a, const char *b)
{
    if (!pass) return;
    char what[256];
    snprintf(what, sizeof what, fmt, a, b);
    pass(user, what);
}

bool weapons_ini_read(const char *path, WeaponStats stats[WEAPON_COUNT], char *name, size_t name_size,
                      void (*pass)(void *user, const char *what), void *user)
{
    char *text = files_read(path, NULL);
    if (!text) return false;
    if (name && name_size) name[0] = '\0';
    int weapon = -1;   // the section's weapon; -1 for none
    bool info = false; // in [Info]
    for (char *line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        char *comment = strchr(line, ';');
        if (comment) *comment = '\0';
        comment = strstr(line, "//");
        if (comment) *comment = '\0';
        line = trim(line);
        if (!*line) continue;
        if (*line == '[') {
            char *end = strchr(line, ']');
            if (end) *end = '\0';
            char *section = trim(line + 1);
            weapon = -1;
            info = same(section, "Info");
            for (int id = 0; id < WEAPON_COUNT && !info && weapon < 0; id++)
                if (SECTIONS[id] && same(section, SECTIONS[id])) weapon = id;
            if (!info && weapon < 0) say(pass, user, "no weapon [%s]%s", section, "");
            continue;
        }
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(line), *value = trim(eq + 1);
        if (info) {
            if (name && name_size && same(key, "Name")) snprintf(name, name_size, "%s", value);
            continue;
        }
        if (weapon < 0) continue;
        int k = 0;
        while (k < WEAPON_FIELD_COUNT && !same(key, KEYS[k])) k++;
        if (k == WEAPON_FIELD_COUNT) {
            bool known = false;
            for (size_t p = 0; p < sizeof PASSED / sizeof PASSED[0] && !known; p++) known = same(key, PASSED[p]);
            if (!known) say(pass, user, "no key %s in [%s]", key, SECTIONS[weapon]);
            continue;
        }
        uint8_t *at = (uint8_t *)&stats[weapon] + WEAPON_FIELDS[k].offset;
        if (WEAPON_FIELDS[k].kind == NET_F32) *(float *)at = (float)atof(value);
        else *(int32_t *)at = (int32_t)atoi(value);
    }
    free(text);
    return true;
}

bool weapons_ini_template(const char *path)
{
    static const char HEADER[] =
        "; A weapons mod, as Soldat's weapons.ini has it: a mod made for Soldat or OpenSoldat\n"
        "; may stand here as it is. Each weapon's section holds the game's own numbers, commented\n"
        "; out: take a line's ; off and change it to change that number; a weapon or a number the\n"
        "; file doesn't set keeps the game's. The server reads this as it starts, and sends its\n"
        "; numbers to every player who joins, so a mod plays the same for all of them. At the\n"
        "; server's console, `weapon` changes one while the game is on, and `weaponlist` shows\n"
        "; them all.\n"
        ";\n"
        ";   Damage            the hit's strength       FireInterval  ticks between shots\n"
        ";   Ammo              rounds in the clip       ReloadTime    ticks a reload takes\n"
        ";   Speed             the bullet's, out of the muzzle\n"
        ";   StartUpTime       ticks of wind-up (minigun, LAW, Barrett)\n"
        ";   Bink              aim disturbed: negative a shot's own kick, positive given to its holder when hit\n"
        ";   MovementAcc       inaccuracy while moving  BulletSpread  the bullets' spread\n"
        ";   Push              knockback on whom it hits\n"
        ";   InheritedVelocity share of the shooter's velocity\n"
        ";   ModifierHead, ModifierChest, ModifierLegs  the damage on each part, times this\n"
        ";\n"
        "; (60 ticks are a second.)\n"
        "\n"
        "[Info]\n"
        "Name=\n"
        "Version=\n";
    Weapons own;
    weapons_default(&own);
    WeaponStats base[WEAPON_COUNT];
    weapons_stats(&own, base);
    size_t cap = sizeof HEADER + (size_t)WEAPON_COUNT * 1024, n = 0;
    char *text = malloc(cap);
    if (!text) return false;
    n += (size_t)snprintf(text, cap, "%s", HEADER);
    for (int id = 0; id < WEAPON_COUNT; id++) {
        if (!SECTIONS[id] || !weapons_moddable((WeaponId)id)) continue;
        n += (size_t)snprintf(text + n, cap - n, "\n[%s]\n", SECTIONS[id]);
        for (int k = 0; k < WEAPON_FIELD_COUNT; k++) {
            const uint8_t *at = (const uint8_t *)&base[id] + WEAPON_FIELDS[k].offset;
            if (WEAPON_FIELDS[k].kind == NET_F32) n += (size_t)snprintf(text + n, cap - n, ";%s=%g\n", KEYS[k], *(const float *)at);
            else n += (size_t)snprintf(text + n, cap - n, ";%s=%d\n", KEYS[k], *(const int32_t *)at);
        }
    }
    bool ok = files_make_parents(path) && files_write(path, text, n);
    free(text);
    return ok;
}
