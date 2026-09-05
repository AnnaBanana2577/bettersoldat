#include "render/gostek.h"

#include <rlgl.h>
#include <stdio.h>

typedef enum GostekColor {
    GOSTEK_COLOR_NONE,
    GOSTEK_COLOR_MAIN,
    GOSTEK_COLOR_PANTS,
    GOSTEK_COLOR_SKIN,
    GOSTEK_COLOR_HAIR,
    GOSTEK_COLOR_HEAD_BLOOD,
} GostekColor;

typedef struct GostekPart {
    const char *file; // base name under `dir`
    const char *dir;  // the folder it is read from; gostek-gfx when NULL
    int p1, p2;       // skeleton points, the original's 1-based numbering
    float cx, cy;     // anchor within the sprite, 0..1
    float flex;       // if > 0, stretch along the part's length
    bool flip;        // has a mirrored "<file>2" image for facing left
    bool team;        // has a team2/<file> variant
    GostekColor color;
    bool jets;  // drawn only while jetting (replaces the matching foot)
    bool foot;  // hidden while jetting
    bool blood; // a wound over the part before it, shown as health runs low
    bool grip;  // the held weapon goes just before it, so the arm wraps the grip
    bool vest;  // drawn while the vest holds
    bool badge; // drawn while a bow is in the hands
    int nade;   // the nth grenade on the belt, 1 to 5; drawn while that many are carried
} GostekPart;

static const GostekPart GOSTEK_PARTS[] = {
    {.file = "udo", .p1 = 6, .p2 = 3, .cx = 0.2f, .cy = 0.5f, .flex = 5, .flip = true, .team = true, .color = GOSTEK_COLOR_PANTS},
    {.file = "ranny/udo", .p1 = 6, .p2 = 3, .cx = 0.2f, .cy = 0.5f, .flex = 5, .flip = true, .team = true, .blood = true},
    {.file = "stopa", .p1 = 2, .p2 = 18, .cx = 0.35f, .cy = 0.35f, .flip = true, .team = true, .foot = true},
    {.file = "lecistopa", .p1 = 2, .p2 = 18, .cx = 0.35f, .cy = 0.35f, .flip = true, .team = true, .jets = true},
    {.file = "noga", .p1 = 3, .p2 = 2, .cx = 0.15f, .cy = 0.55f, .flip = true, .team = true, .color = GOSTEK_COLOR_PANTS},
    {.file = "ranny/noga", .p1 = 3, .p2 = 2, .cx = 0.15f, .cy = 0.55f, .flip = true, .team = true, .blood = true},
    {.file = "ramie", .p1 = 11, .p2 = 14, .cx = 0, .cy = 0.5f, .flip = true, .team = true, .color = GOSTEK_COLOR_MAIN},
    {.file = "ranny/ramie", .p1 = 11, .p2 = 14, .cx = 0, .cy = 0.5f, .flip = true, .team = true, .blood = true},
    {.file = "reka", .p1 = 14, .p2 = 15, .cx = 0, .cy = 0.5f, .flex = 5, .team = true, .color = GOSTEK_COLOR_MAIN},
    {.file = "ranny/reka", .p1 = 14, .p2 = 15, .cx = 0, .cy = 0.5f, .flex = 5, .flip = true, .team = true, .blood = true},
    {.file = "dlon", .p1 = 15, .p2 = 19, .cx = 0, .cy = 0.4f, .flip = true, .team = true, .color = GOSTEK_COLOR_SKIN},
    {.file = "udo", .p1 = 5, .p2 = 4, .cx = 0.2f, .cy = 0.65f, .flex = 5, .flip = true, .team = true, .color = GOSTEK_COLOR_PANTS},
    {.file = "ranny/udo", .p1 = 5, .p2 = 4, .cx = 0.2f, .cy = 0.65f, .flex = 5, .flip = true, .team = true, .blood = true},
    {.file = "stopa", .p1 = 1, .p2 = 17, .cx = 0.35f, .cy = 0.35f, .flip = true, .team = true, .foot = true},
    {.file = "lecistopa", .p1 = 1, .p2 = 17, .cx = 0.35f, .cy = 0.35f, .flip = true, .team = true, .jets = true},
    {.file = "noga", .p1 = 4, .p2 = 1, .cx = 0.15f, .cy = 0.55f, .flip = true, .team = true, .color = GOSTEK_COLOR_PANTS},
    {.file = "ranny/noga", .p1 = 4, .p2 = 1, .cx = 0.15f, .cy = 0.55f, .flip = true, .team = true, .blood = true},
    {.file = "klata", .p1 = 10, .p2 = 11, .cx = 0.1f, .cy = 0.3f, .flip = true, .team = true, .color = GOSTEK_COLOR_MAIN},
    {.file = "kamizelka", .p1 = 10, .p2 = 11, .cx = 0.1f, .cy = 0.3f, .flip = true, .team = true, .vest = true},
    {.file = "ranny/klata", .p1 = 10, .p2 = 11, .cx = 0.1f, .cy = 0.3f, .flip = true, .team = true, .blood = true},
    {.file = "biodro", .p1 = 5, .p2 = 6, .cx = 0.25f, .cy = 0.6f, .flip = true, .team = true, .color = GOSTEK_COLOR_MAIN},
    {.file = "ranny/biodro", .p1 = 5, .p2 = 6, .cx = 0.25f, .cy = 0.6f, .flip = true, .team = true, .blood = true},
    {.file = "morda", .p1 = 9, .p2 = 12, .cx = 0, .cy = 0.5f, .flip = true, .team = true, .color = GOSTEK_COLOR_SKIN},
    {.file = "ranny/morda", .p1 = 9, .p2 = 12, .cx = 0, .cy = 0.5f, .flip = true, .team = true, .color = GOSTEK_COLOR_HEAD_BLOOD, .blood = true},
    {.file = "badge", .p1 = 9, .p2 = 12, .cx = 0, .cy = 0.5f, .flip = true, .team = true, .badge = true},
    // The belt, between the hips. The original's data pins all five to the same spot, so
    // a soldier carrying more shows no more; the count is still the original's.
    {.file = "frag-grenade", .dir = "weapons-gfx", .p1 = 5, .p2 = 6, .cx = 0.5f, .cy = 0.1f, .nade = 1},
    {.file = "frag-grenade", .dir = "weapons-gfx", .p1 = 5, .p2 = 6, .cx = 0.5f, .cy = 0.1f, .nade = 2},
    {.file = "frag-grenade", .dir = "weapons-gfx", .p1 = 5, .p2 = 6, .cx = 0.5f, .cy = 0.1f, .nade = 3},
    {.file = "frag-grenade", .dir = "weapons-gfx", .p1 = 5, .p2 = 6, .cx = 0.5f, .cy = 0.1f, .nade = 4},
    {.file = "frag-grenade", .dir = "weapons-gfx", .p1 = 5, .p2 = 6, .cx = 0.5f, .cy = 0.1f, .nade = 5},
    {.file = "ramie", .p1 = 10, .p2 = 13, .cx = 0, .cy = 0.6f, .flip = true, .team = true, .color = GOSTEK_COLOR_MAIN, .grip = true},
    {.file = "ranny/ramie", .p1 = 10, .p2 = 13, .cx = -0.1f, .cy = 0.5f, .flip = true, .team = true, .blood = true},
    {.file = "reka", .p1 = 13, .p2 = 16, .cx = 0, .cy = 0.6f, .flex = 5, .team = true, .color = GOSTEK_COLOR_MAIN},
    {.file = "ranny/reka", .p1 = 13, .p2 = 16, .cx = 0, .cy = 0.6f, .flex = 5, .flip = true, .team = true, .blood = true},
    {.file = "dlon", .p1 = 16, .p2 = 20, .cx = 0, .cy = 0.5f, .flip = true, .team = true, .color = GOSTEK_COLOR_SKIN},
};

_Static_assert(sizeof(GOSTEK_PARTS) / sizeof(GOSTEK_PARTS[0]) == GOSTEK_PART_COUNT, "GOSTEK_PART_COUNT");

// Held weapons: the primary in the hands (skeleton 16 -> 15), the secondary slung across
// the back (5 -> 10). Mirrored images are "<stem>-2.png" under weapons-gfx.
typedef struct WeaponArt {
    const char *stem;
    float cx, cy;     // in the hands
    float bx, by;     // on the back
    const char *fire; // the muzzle flash, drawn the tick a shot goes off
    float fx, fy;     // its anchor, past the muzzle so fx is negative
} WeaponArt;

static const WeaponArt WEAPON_ART[WEAPON_COUNT] = {
    [WEAPON_EAGLE] = {"deserteagle", 0.1f, 0.8f, 0.3f, 0.5f, "eagles-fire", -0.5f, 1.0f},
    [WEAPON_MP5] = {"mp5", 0.15f, 0.6f, 0.3f, 0.3f, "mp5-fire", -0.65f, 0.85f},
    [WEAPON_AK74] = {"ak74", 0.15f, 0.5f, 0.3f, 0.25f, "ak74-fire", -0.37f, 0.8f},
    [WEAPON_STEYR] = {"steyraug", 0.2f, 0.6f, 0.3f, 0.5f, "steyraug-fire", -0.24f, 0.75f},
    [WEAPON_SPAS] = {"spas12", 0.1f, 0.6f, 0.3f, 0.3f, "spas12-fire", -0.2f, 0.9f},
    [WEAPON_RUGER] = {"ruger77", 0.1f, 0.7f, 0.3f, 0.3f, "ruger77-fire", -0.35f, 0.85f},
    [WEAPON_M79] = {"m79", 0.1f, 0.7f, 0.3f, 0.35f, "m79-fire", -0.4f, 0.8f},
    [WEAPON_BARRETT] = {"barretm82", 0.15f, 0.7f, 0.3f, 0.35f, "barret-fire", -0.15f, 0.8f},
    [WEAPON_M249] = {"m249", 0.15f, 0.6f, 0.3f, 0.35f, "m249-fire", -0.2f, 0.9f},
    [WEAPON_MINIGUN] = {"minigun", 0.05f, 0.5f, 0.2f, 0.5f, "minigun-fire", -0.2f, 0.45f},
    [WEAPON_COLT] = {"colt1911", 0.2f, 0.55f, 0.3f, 0.5f, "colt1911-fire", -0.24f, 0.85f},
    [WEAPON_CHAINSAW] = {"chainsaw", 0.25f, 0.5f, 0.25f, 0.5f, "chainsaw-fire", -0.2f, 0.5f},
    [WEAPON_LAW] = {"law", 0.1f, 0.6f, 0.3f, 0.45f, "law-fire", -0.2f, 0.8f},
    [WEAPON_FLAMER] = {"flamer", 0.3f, 0.6f, 0.3f, 0.3f, "flamer-fire", -0.2f, 0.5f},
    [WEAPON_BOW] = {"bow", 0.2f, 0.5f, 0.3f, 0.5f, "bow-fire", -0.2f, 0.5f},
    [WEAPON_BOW2] = {"bow", 0.2f, 0.5f, 0.3f, 0.5f, "bow-fire", -0.2f, 0.5f},
    [WEAPON_KNIFE] = {"knife", 0.2f, 0.5f, 0.3f, 0.5f, NULL, 0, 0},
};

void gostek_load(Gostek *g, const char *base)
{
    char path[512];
    *g = (Gostek){0};

    for (int id = 0; id < WEAPON_COUNT; id++) {
        const WeaponArt *art = &WEAPON_ART[id];
        if (!art->stem) continue;
        for (int mirrored = 0; mirrored < 2; mirrored++) {
            snprintf(path, sizeof(path), "%s/weapons-gfx/%s%s", base, art->stem, mirrored ? "-2.png" : ".png");
            sprite_load(&g->weapons[id][mirrored], path, NULL);
        }
        if (art->fire) {
            snprintf(path, sizeof(path), "%s/weapons-gfx/%s.png", base, art->fire);
            sprite_load(&g->flashes[id], path, NULL);
        }
    }

    for (int i = 0; i < GOSTEK_PART_COUNT; i++) {
        const GostekPart *part = &GOSTEK_PARTS[i];
        for (int team = 0; team < 2; team++) {
            for (int mirrored = 0; mirrored < 2; mirrored++) {
                if (mirrored && !part->flip) continue; // no mirrored image: the quad flips instead
                const char *dir = part->dir ? part->dir : (team == 1 && part->team ? "gostek-gfx/team2" : "gostek-gfx");
                snprintf(path, sizeof(path), "%s/%s/%s%s.png", base, dir, part->file, mirrored ? "2" : "");
                sprite_load(&g->parts[i][team][mirrored], path, NULL);
            }
        }
    }
    g->loaded = true;
}

void gostek_unload(Gostek *g)
{
    for (int i = 0; i < GOSTEK_PART_COUNT; i++) {
        for (int t = 0; t < 2; t++) {
            for (int m = 0; m < 2; m++) sprite_unload(&g->parts[i][t][m]);
        }
    }
    for (int id = 0; id < WEAPON_COUNT; id++) {
        sprite_unload(&g->weapons[id][0]);
        sprite_unload(&g->weapons[id][1]);
        sprite_unload(&g->flashes[id]);
    }
    *g = (Gostek){0};
}

// How strongly the wounds show (GostekGraphics.pas): none above 90 health, then stronger
// the lower it goes (a corpse's is its health at death).
static uint8_t blood_alpha(const RenderSoldier *s)
{
    if (s->health > 90.0f) return 0;
    return (uint8_t)clampf(200.0f - roundf(s->health), 0.0f, 255.0f);
}

// Shirt, pants and skin. The original takes these from each player's profile; fixed per
// team until the roster carries them. Faded while the spawn protection lasts.
static Color gostek_color(GostekColor c, const RenderSoldier *s)
{
    uint8_t alpha = s->spawn_protected ? 153 : 255;
    switch (c) {
    case GOSTEK_COLOR_SKIN: return (Color){222, 181, 140, alpha};
    case GOSTEK_COLOR_HAIR: return (Color){64, 46, 31, alpha};
    case GOSTEK_COLOR_HEAD_BLOOD: return (Color){172, 169, 168, alpha};
    case GOSTEK_COLOR_PANTS: return (Color){56, 61, 71, alpha};
    case GOSTEK_COLOR_MAIN:
        switch (s->team) {
        case TEAM_ALPHA: return (Color){199, 56, 51, alpha};
        case TEAM_BRAVO: return (Color){64, 107, 204, alpha};
        case TEAM_CHARLIE: return (Color){230, 199, 64, alpha};
        case TEAM_DELTA: return (Color){77, 179, 89, alpha};
        default: return (Color){140, 140, 148, alpha};
        }
    default: return (Color){255, 255, 255, alpha};
    }
}

static float angle_between(Vec2 p1, Vec2 p2)
{
    return atan2f(p2.y - p1.y, p2.x - p1.x);
}

// A weapon pinned between two skeleton points, the same way a limb is.
static void draw_weapon(const Gostek *g, const Pose *pose, WeaponId id, int p1_index, int p2_index, float cx, float cy,
                        bool facing_left)
{
    bool mirrored = facing_left;
    Sprite sprite = g->weapons[id][mirrored ? 1 : 0];
    if (sprite.tex.id == 0) {
        sprite = g->weapons[id][0];
        if (sprite.tex.id == 0) return;
        mirrored = false;
    }

    Vec2 p1 = pose->p[p1_index - 1], p2 = pose->p[p2_index - 1];
    float anchor_y = cy, sy = 1.0f;
    if (facing_left) {
        if (mirrored) anchor_y = 1.0f - cy;
        else sy = -1.0f;
    }
    draw_sprite(sprite, vec2_add(p1, vec2(0, 1)), vec2(cx * sprite.width, anchor_y * sprite.height), vec2(1, sy),
                angle_between(p1, p2), WHITE);
}

static void draw_held_weapon(const Gostek *g, const RenderSoldier *s)
{
    const WeaponArt *art = &WEAPON_ART[s->weapon];
    if (!art->stem) return;
    draw_weapon(g, &s->pose, s->weapon, 16, 15, art->cx, art->cy, s->facing_left);

    if (!s->fired) return;
    Sprite flash = g->flashes[s->weapon];
    if (flash.tex.id == 0) return;
    Vec2 p1 = s->pose.p[16 - 1], p2 = s->pose.p[15 - 1];
    draw_sprite(flash, vec2_add(p1, vec2(0, 1)), vec2(art->fx * flash.width, art->fy * flash.height),
                vec2(1, s->facing_left ? -1.0f : 1.0f), angle_between(p1, p2), WHITE);
}

void gostek_draw(const Gostek *g, const RenderSoldier *s, bool corpse)
{
    if (!g->loaded) return;

    const Pose *pose = &s->pose;
    bool jetting = s->jetting && !corpse;
    int team = s->team == TEAM_BRAVO || s->team == TEAM_DELTA ? 1 : 0;
    bool facing_left = s->facing_left;

    // slung across the back, so before the body
    const WeaponArt *back = &WEAPON_ART[s->secondary];
    if (back->stem) draw_weapon(g, pose, s->secondary, 5, 10, back->bx, back->by, facing_left);

    uint8_t bleeding = blood_alpha(s);
    // The grenades on the belt: what is carried, less the one already in the hand while a
    // throw runs.
    int carried = s->grenades - (s->body_anim == ANIM_THROW ? 1 : 0);
    bool bow = s->weapon == WEAPON_BOW || s->weapon == WEAPON_BOW2;

    for (int i = 0; i < GOSTEK_PART_COUNT; i++) {
        const GostekPart *part = &GOSTEK_PARTS[i];
        if (part->grip) draw_held_weapon(g, s);
        if (part->jets && !jetting) continue;
        if (part->foot && jetting) continue;
        if (part->blood && bleeding == 0) continue;
        if (part->vest && s->vest <= 0.0f) continue;
        if (part->badge && !bow) continue;
        if (part->nade > 0 && part->nade > carried) continue; // a body keeps its belt, as the original leaves it

        bool mirrored = facing_left && part->flip;
        Sprite sprite = g->parts[i][part->team ? team : 0][mirrored ? 1 : 0];
        if (sprite.tex.id == 0) continue;

        Vec2 p1 = pose->p[part->p1 - 1];
        Vec2 p2 = pose->p[part->p2 - 1];
        Vec2 along = vec2_sub(p2, p1);
        float angle = atan2f(along.y, along.x);

        float cx = part->cx, cy = part->cy;
        if (corpse && part->p2 == 12) {
            p1 = p2;
            cx = 1.0f;
        }
        float sx = 1.0f, sy = 1.0f;
        if (facing_left) {
            if (part->flip) cy = 1.0f - part->cy;
            else sy = -1.0f;
        }
        if (part->flex > 0.0f) sx = minf(1.5f, vec2_length(along) / part->flex);

        Color tint = gostek_color(part->color, s);
        if (part->blood) tint.a = bleeding;
        if (part->nade > 0) tint.a = (uint8_t)(0.75f * (float)tint.a); // ALPHA_NADES
        draw_sprite(sprite, vec2_add(p1, vec2(0, 1)), vec2(cx * sprite.width, cy * sprite.height), vec2(sx, sy), angle, tint);
    }
    rlSetTexture(0);
}
