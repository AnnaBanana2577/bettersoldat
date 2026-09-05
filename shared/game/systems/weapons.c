// The weapons: names, how they reload and fire, and the balance numbers. The defaults
// are Soldat 1.7.1's, which is also OpenSoldat's built-in table; a server's weapons mod
// overrides them. Ported from Weapons.pas by way of soldat-odin.

#include <ctype.h>
#include <string.h>

#include "game/systems/systems.h"

// ticks
#define BULLET_TIMEOUT (60 * 7)
#define GRENADE_TIMEOUT (60 * 3)
#define M2BULLET_TIMEOUT 60
#define FLAMER_TIMEOUT 32
#define MELEE_TIMEOUT 1

typedef struct WeaponBase {
    const char *name;
    bool clip_reload;
    bool semi_auto;
} WeaponBase;

static const WeaponBase WEAPON_BASE[WEAPON_COUNT] = {
    [WEAPON_NONE] = {"Hands", false, false},
    [WEAPON_EAGLE] = {"Desert Eagles", true, true},
    [WEAPON_MP5] = {"HK MP5", true, false},
    [WEAPON_AK74] = {"Ak-74", true, false},
    [WEAPON_STEYR] = {"Steyr AUG", true, false},
    [WEAPON_SPAS] = {"Spas-12", false, true},
    [WEAPON_RUGER] = {"Ruger 77", false, true},
    [WEAPON_M79] = {"M79", true, false},
    [WEAPON_BARRETT] = {"Barrett M82A1", true, true},
    [WEAPON_M249] = {"FN Minimi", true, false},
    [WEAPON_MINIGUN] = {"XM214 Minigun", false, false},
    [WEAPON_COLT] = {"USSOCOM", true, true},
    [WEAPON_KNIFE] = {"Combat Knife", false, false},
    [WEAPON_CHAINSAW] = {"Chainsaw", false, false},
    [WEAPON_LAW] = {"LAW", true, false},
    [WEAPON_BOW2] = {"Flame Bow", false, false},
    [WEAPON_BOW] = {"Bow", false, false},
    [WEAPON_FLAMER] = {"Flamer", false, false},
    [WEAPON_M2] = {"M2 MG", false, false},
    [WEAPON_FRAG] = {"Frag Grenade", false, false},
    [WEAPON_CLUSTER_NADE] = {"Cluster Grenade", false, false},
    [WEAPON_CLUSTER] = {"Cluster", false, false},
    [WEAPON_THROWN_KNIFE] = {"Combat Knife", false, false},
};

// Soldat 1.7.1. The cluster grenade, cluster and thrown knife derive from the frag
// grenade and the knife in weapons_finalize.
static const WeaponStats WEAPON_DEFAULTS[WEAPON_COUNT] = {
    //                      damage  fire ammo reload speed  style                startup bink moveacc spread  push      inherit head   chest  legs
    [WEAPON_EAGLE]    = {1.81f,   24,  7,   87,    19.0f, BULLET_PLAIN,        0,      0,   0.009f, 0.15f,  0.0176f,  0.5f,   1.1f,  0.95f, 0.85f},
    [WEAPON_MP5]      = {1.01f,   6,   30,  105,   18.9f, BULLET_PLAIN,        0,      0,   0.0f,   0.14f,  0.0112f,  0.5f,   1.1f,  0.95f, 0.85f},
    [WEAPON_AK74]     = {1.004f,  10,  35,  165,   24.6f, BULLET_PLAIN,        0,      -12, 0.011f, 0.025f, 0.01376f, 0.5f,   1.1f,  0.95f, 0.85f},
    [WEAPON_STEYR]    = {0.71f,   7,   25,  125,   26.0f, BULLET_PLAIN,        0,      0,   0.0f,   0.075f, 0.0084f,  0.5f,   1.1f,  0.95f, 0.85f},
    [WEAPON_SPAS]     = {1.22f,   32,  7,   175,   14.0f, BULLET_SHOTGUN,      0,      0,   0.0f,   0.8f,   0.0188f,  0.5f,   1.1f,  0.95f, 0.85f},
    [WEAPON_RUGER]    = {2.49f,   45,  4,   78,    33.0f, BULLET_PLAIN,        0,      0,   0.03f,  0.0f,   0.012f,   0.5f,   1.2f,  1.05f, 1.0f},
    [WEAPON_M79]      = {1550.0f, 6,   1,   178,   10.7f, BULLET_M79,          0,      0,   0.0f,   0.0f,   0.036f,   0.5f,   1.15f, 1.0f,  0.9f},
    [WEAPON_BARRETT]  = {4.45f,   225, 10,  70,    55.0f, BULLET_PLAIN,        19,     65,  0.05f,  0.0f,   0.018f,   0.5f,   1.0f,  1.0f,  1.0f},
    [WEAPON_M249]     = {0.85f,   9,   50,  250,   27.0f, BULLET_PLAIN,        0,      0,   0.013f, 0.064f, 0.0128f,  0.5f,   1.1f,  0.95f, 0.85f},
    [WEAPON_MINIGUN]  = {0.468f,  3,   100, 480,   29.0f, BULLET_PLAIN,        25,     0,   0.0625f, 0.3f,  0.0104f,  0.5f,   1.1f,  0.95f, 0.85f},
    [WEAPON_COLT]     = {1.49f,   10,  14,  60,    18.0f, BULLET_PLAIN,        0,      0,   0.0f,   0.0f,   0.02f,    0.5f,   1.1f,  0.95f, 0.85f},
    [WEAPON_KNIFE]    = {2150.0f, 6,   1,   3,     6.0f,  BULLET_KNIFE,        0,      0,   0.0f,   0.0f,   0.12f,    0.0f,   1.15f, 1.0f,  0.9f},
    [WEAPON_CHAINSAW] = {50.0f,   2,   200, 110,   8.0f,  BULLET_KNIFE,        0,      0,   0.0f,   0.0f,   0.0028f,  0.0f,   1.15f, 1.0f,  0.9f},
    [WEAPON_LAW]      = {1550.0f, 6,   1,   300,   23.0f, BULLET_LAW,          13,     0,   0.0f,   0.0f,   0.028f,   0.5f,   1.15f, 1.0f,  0.9f},
    [WEAPON_BOW2]     = {8.0f,    10,  1,   39,    18.0f, BULLET_FLAME_ARROW,  0,      0,   0.0f,   0.0f,   0.0f,     0.5f,   1.15f, 1.0f,  0.9f},
    [WEAPON_BOW]      = {12.0f,   10,  1,   25,    21.0f, BULLET_ARROW,        0,      0,   0.0f,   0.0f,   0.0148f,  0.5f,   1.15f, 1.0f,  0.9f},
    [WEAPON_FLAMER]   = {19.0f,   6,   200, 5,     10.5f, BULLET_FLAME,        0,      0,   0.0f,   0.0f,   0.016f,   0.5f,   1.15f, 1.0f,  0.9f},
    [WEAPON_M2]       = {1.8f,    10,  100, 366,   36.0f, BULLET_M2,           0,      0,   0.0f,   0.0f,   0.0088f,  0.0f,   1.1f,  0.95f, 0.85f},
    [WEAPON_NONE]     = {330.0f,  6,   1,   3,     5.0f,  BULLET_PUNCH,        0,      0,   0.0f,   0.0f,   0.0f,     0.0f,   1.15f, 1.0f,  0.9f},
    [WEAPON_FRAG]     = {1500.0f, 80,  1,   20,    5.0f,  BULLET_FRAG_GRENADE, 0,      0,   0.0f,   0.0f,   0.0f,     1.0f,   1.0f,  1.0f,  1.0f},
};

bool weapon_is_primary(WeaponId id)
{
    return id >= WEAPON_EAGLE && id <= WEAPON_MINIGUN;
}

bool weapon_is_secondary(WeaponId id)
{
    return id >= WEAPON_COLT && id <= WEAPON_LAW;
}

void weapon_set_stats(WeaponInfo *info, WeaponStats stats)
{
    info->stats = stats;
}

void weapons_default(Weapons *w)
{
    memset(w, 0, sizeof(*w));
    for (int id = 0; id < WEAPON_COUNT; id++) {
        WeaponInfo *info = &w->info[id];
        info->name = WEAPON_BASE[id].name;
        info->clip_reload = WEAPON_BASE[id].clip_reload;
        info->semi_auto = WEAPON_BASE[id].semi_auto;
        weapon_set_stats(info, WEAPON_DEFAULTS[id]);
    }
    weapons_finalize(w);
}

// A weapon that is another with its own name, reload and style.
static void derive(Weapons *w, WeaponId id, WeaponId from, BulletStyle style)
{
    w->info[id] = w->info[from];
    w->info[id].name = WEAPON_BASE[id].name;
    w->info[id].clip_reload = WEAPON_BASE[id].clip_reload;
    w->info[id].semi_auto = WEAPON_BASE[id].semi_auto;
    w->info[id].stats.style = style;
}

void weapons_finalize(Weapons *w)
{
    derive(w, WEAPON_CLUSTER_NADE, WEAPON_FRAG, BULLET_CLUSTER_NADE);
    derive(w, WEAPON_CLUSTER, WEAPON_CLUSTER_NADE, BULLET_CLUSTER);
    derive(w, WEAPON_THROWN_KNIFE, WEAPON_KNIFE, BULLET_THROWN_KNIFE);

    for (int id = 0; id < WEAPON_COUNT; id++) {
        WeaponInfo *info = &w->info[id];
        info->clip_out_time = 0;
        info->clip_in_time = 0;
        if (info->clip_reload) {
            info->clip_out_time = (int32_t)((float)info->stats.reload_time * 0.8f);
            info->clip_in_time = (int32_t)((float)info->stats.reload_time * 0.3f);
        }

        switch (info->stats.style) {
        case BULLET_FRAG_GRENADE:
        case BULLET_CLUSTER_NADE: info->timeout = GRENADE_TIMEOUT; break;
        case BULLET_FLAME: info->timeout = FLAMER_TIMEOUT; break;
        case BULLET_PUNCH:
        case BULLET_KNIFE: info->timeout = MELEE_TIMEOUT; break;
        case BULLET_M2: info->timeout = M2BULLET_TIMEOUT; break;
        default: info->timeout = BULLET_TIMEOUT; break;
        }
    }
}

static bool equal_ignoring_case(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    }
    return *a == *b;
}

WeaponId weapon_named(const char *name)
{
    if (!name || !name[0]) return WEAPON_NONE;
    for (int id = 0; id < WEAPON_COUNT; id++) {
        if (equal_ignoring_case(WEAPON_BASE[id].name, name)) return (WeaponId)id;
    }
    return WEAPON_NONE;
}
