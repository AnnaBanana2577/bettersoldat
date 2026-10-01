#include "render/gostek.h"

#include <math.h>
#include <stdio.h>

typedef enum GostekColor {
    GOSTEK_COLOR_NONE,
    GOSTEK_COLOR_MAIN,
    GOSTEK_COLOR_PANTS,
    GOSTEK_COLOR_SKIN,
    GOSTEK_COLOR_HAIR,
    GOSTEK_COLOR_HEAD_BLOOD,
    GOSTEK_COLOR_CIGAR, // white, or grey while unlit
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
    int hair;   // drawn for this hair style (PlayerLook.hair_style)
    int dread;  // the nth dreadlock, 1 to 5: offset from the head's top and hanging from there
    int head;   // drawn for this head style
    int chain;  // drawn for this chain style
    bool grabbed; // the headgear in the hand (a wipe or a take-off past its fourth frame) rather than on the head
    bool cigar;   // drawn while a cigar is in the mouth
} GostekPart;

// The chains' and the dreadlocks' points, past the pose's 20: the neck (21) with the
// pendant swinging below it (22), and the head's top (23) with the dreadlocks' end
// below it (24), as the simulation swings them (RenderSoldier.swing).
#define GOSTEK_POINTS 24

static const GostekPart GOSTEK_PARTS[] = {
    // The helmet or the hat in the left hand, while the brow is wiped or it comes off
    // (Grabbed_Helmet, Grabbed_Hat): behind everything, as the original draws them.
    {.file = "helm", .p1 = 15, .p2 = 19, .cx = 0, .cy = 0.5f, .flip = true, .team = true, .color = GOSTEK_COLOR_MAIN, .head = 1, .grabbed = true},
    {.file = "kap", .p1 = 15, .p2 = 19, .cx = 0.1f, .cy = 0.4f, .flip = true, .team = true, .color = GOSTEK_COLOR_MAIN, .head = 2, .grabbed = true},
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
    // The hair, the headgear and the chain, in the original's order. A helmet or a hat
    // covers every hair style but Mr. T's; the bow's band replaces them all.
    {.file = "hair3", .p1 = 9, .p2 = 12, .cx = 0, .cy = 0.5f, .flip = true, .team = true, .color = GOSTEK_COLOR_HAIR, .hair = 3},
    {.file = "helm", .p1 = 9, .p2 = 12, .cx = 0, .cy = 0.5f, .flip = true, .team = true, .color = GOSTEK_COLOR_MAIN, .head = 1},
    {.file = "kap", .p1 = 9, .p2 = 12, .cx = 0, .cy = 0.5f, .flip = true, .team = true, .color = GOSTEK_COLOR_MAIN, .head = 2},
    {.file = "badge", .p1 = 9, .p2 = 12, .cx = 0, .cy = 0.5f, .flip = true, .team = true, .badge = true},
    {.file = "hair1", .p1 = 9, .p2 = 12, .cx = 0, .cy = 0.5f, .flip = true, .team = true, .color = GOSTEK_COLOR_HAIR, .hair = 1},
    {.file = "dred", .p1 = 23, .p2 = 24, .cx = 0, .cy = 1.22f, .team = true, .color = GOSTEK_COLOR_HAIR, .hair = 1, .dread = 1},
    {.file = "dred", .p1 = 23, .p2 = 24, .cx = 0.1f, .cy = 0.5f, .team = true, .color = GOSTEK_COLOR_HAIR, .hair = 1, .dread = 2},
    {.file = "dred", .p1 = 23, .p2 = 24, .cx = 0.04f, .cy = -0.3f, .team = true, .color = GOSTEK_COLOR_HAIR, .hair = 1, .dread = 3},
    {.file = "dred", .p1 = 23, .p2 = 24, .cx = 0, .cy = -0.9f, .team = true, .color = GOSTEK_COLOR_HAIR, .hair = 1, .dread = 4},
    {.file = "dred", .p1 = 23, .p2 = 24, .cx = -0.2f, .cy = -1.35f, .team = true, .color = GOSTEK_COLOR_HAIR, .hair = 1, .dread = 5},
    {.file = "hair2", .p1 = 9, .p2 = 12, .cx = 0, .cy = 0.5f, .flip = true, .team = true, .color = GOSTEK_COLOR_HAIR, .hair = 2},
    {.file = "hair4", .p1 = 9, .p2 = 12, .cx = 0, .cy = 0.5f, .flip = true, .team = true, .color = GOSTEK_COLOR_HAIR, .hair = 4},
    {.file = "lancuch", .p1 = 10, .p2 = 22, .cx = 0.1f, .cy = 0.5f, .team = true, .chain = 1},
    {.file = "lancuch", .p1 = 11, .p2 = 22, .cx = 0.1f, .cy = 0.5f, .team = true, .chain = 1},
    {.file = "metal", .p1 = 22, .p2 = 21, .cx = 0.5f, .cy = 0.7f, .flip = true, .team = true, .chain = 1},
    {.file = "zlotylancuch", .p1 = 10, .p2 = 22, .cx = 0.1f, .cy = 0.5f, .team = true, .chain = 2},
    {.file = "zlotylancuch", .p1 = 11, .p2 = 22, .cx = 0.1f, .cy = 0.5f, .team = true, .chain = 2},
    {.file = "zloto", .p1 = 22, .p2 = 21, .cx = 0.5f, .cy = 0.5f, .flip = true, .team = true, .chain = 2},
    // The cigar, while one is in the mouth (the antics): grey until it is lit
    {.file = "cygaro", .p1 = 9, .p2 = 12, .cx = -0.125f, .cy = 0.4f, .flip = true, .team = true, .color = GOSTEK_COLOR_CIGAR, .cigar = true},
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

// Held weapons: the primary in the hands (skeleton 16 -> 15; the knife 16 -> 20, along
// the forearm, as the original's Primary_Knife), the secondary slung across the back
// (5 -> 10; the knife is not shown there, as the original's Secondary_Knife has no
// image). Mirrored images are "<stem>-2.png" under weapons-gfx.
typedef struct WeaponArt {
    const char *stem;
    float cx, cy;     // in the hands
    float bx, by;     // on the back
    const char *fire; // the muzzle flash, drawn the tick a shot goes off
    float fx, fy;     // its anchor, past the muzzle so fx is negative
    int hand_p2;      // the second point the primary is pinned to; 0 for the usual 15
    bool unslung;     // never drawn on the back
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
    [WEAPON_KNIFE] = {"knife", -0.1f, 0.6f, 0, 0, NULL, 0, 0, .hand_p2 = 20, .unslung = true},
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

// Shirt, pants, skin and hair: the player's own (PlayerLook; the shirt is the team's in
// a team game, set where the look is). Faded while the spawn protection lasts.
static Rgba gostek_color(GostekColor c, const RenderSoldier *s)
{
    Rgba color;
    switch (c) {
    case GOSTEK_COLOR_MAIN: color = s->look.shirt; break;
    case GOSTEK_COLOR_PANTS: color = s->look.pants; break;
    case GOSTEK_COLOR_SKIN: color = s->look.skin; break;
    case GOSTEK_COLOR_HAIR: color = s->look.hair; break;
    case GOSTEK_COLOR_HEAD_BLOOD: color = (Rgba){172, 169, 168, 255}; break;
    case GOSTEK_COLOR_CIGAR: color = s->has_cigar == 5 ? (Rgba){97, 97, 97, 255} : RGBA_WHITE; break;
    default: color = RGBA_WHITE; break;
    }
    color.a = s->spawn_protected ? 153 : 255;
    return color;
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
    if (sprite.tex.handle == 0) {
        sprite = g->weapons[id][0];
        if (sprite.tex.handle == 0) return;
        mirrored = false;
    }

    Vec2 p1 = pose->p[p1_index - 1], p2 = pose->p[p2_index - 1];
    float anchor_y = cy, sy = 1.0f;
    if (facing_left) {
        if (mirrored) anchor_y = 1.0f - cy;
        else sy = -1.0f;
    }
    draw_sprite(sprite, vec2_add(p1, vec2(0, 1)), vec2(cx * sprite.width, anchor_y * sprite.height), vec2(1, sy),
                angle_between(p1, p2), RGBA_WHITE);
}

static void draw_held_weapon(const Gostek *g, const RenderSoldier *s)
{
    const WeaponArt *art = &WEAPON_ART[s->weapon];
    if (!art->stem) return;
    draw_weapon(g, &s->pose, s->weapon, 16, art->hand_p2 ? art->hand_p2 : 15, art->cx, art->cy, s->facing_left);

    if (!s->fired) return;
    Sprite flash = g->flashes[s->weapon];
    if (flash.tex.handle == 0) return;
    Vec2 p1 = s->pose.p[16 - 1], p2 = s->pose.p[15 - 1];
    draw_sprite(flash, vec2_add(p1, vec2(0, 1)), vec2(art->fx * flash.width, art->fy * flash.height),
                vec2(1, s->facing_left ? -1.0f : 1.0f), angle_between(p1, p2), RGBA_WHITE);
}

void gostek_draw(const Gostek *g, const RenderSoldier *s, bool corpse)
{
    if (!g->loaded) return;

    const Pose *pose = &s->pose;
    bool jetting = s->jetting && !corpse;
    int team = s->team == TEAM_BRAVO || s->team == TEAM_DELTA ? 1 : 0;
    bool facing_left = s->facing_left;
    const PlayerLook *look = &s->look;

    // the pose's points, and the four the chains and the dreadlocks hang from
    Vec2 p[GOSTEK_POINTS];
    for (int i = 0; i < POSE_POINTS; i++) p[i] = pose->p[i];
    for (int k = 0; k < 4; k++) p[POSE_POINTS + k] = s->swing[k];

    // slung across the back, so before the body
    const WeaponArt *back = &WEAPON_ART[s->secondary];
    if (back->stem && !back->unslung) draw_weapon(g, pose, s->secondary, 5, 10, back->bx, back->by, facing_left);

    uint8_t bleeding = blood_alpha(s);
    // The grenades on the belt: what is carried, less the one already in the hand while a
    // throw runs.
    int carried = s->grenades - (s->body_anim == ANIM_THROW ? 1 : 0);
    bool bow = s->weapon == WEAPON_BOW || s->weapon == WEAPON_BOW2;
    // the headgear: on the head, or in the hand past the fourth frame of a wipe or a
    // take-off; the hair shows under neither but Mr. T's, and once the helmet is off
    bool grabbed = (s->body_anim == ANIM_WIPE || s->body_anim == ANIM_TAKE_OFF) && s->body_frame > 4;
    bool capped = look->head_style != 0 && s->wear_helmet == 1;
    bool hair_shown = !bow && (grabbed || !capped || look->hair_style == 3);

    for (int i = 0; i < GOSTEK_PART_COUNT; i++) {
        const GostekPart *part = &GOSTEK_PARTS[i];
        if (part->grip) draw_held_weapon(g, s);
        if (part->jets && !jetting) continue;
        if (part->foot && jetting) continue;
        if (part->blood && bleeding == 0) continue;
        if (part->vest && s->vest <= 0.0f) continue;
        if (part->badge && !bow) continue;
        if (part->nade > 0 && part->nade > carried) continue; // a body keeps its belt, as the original leaves it
        if (part->hair && (part->hair != look->hair_style || !hair_shown)) continue;
        if (part->head && (part->head != look->head_style || bow || !capped || part->grabbed != grabbed)) continue;
        if (part->chain && part->chain != look->chain_style) continue;
        if (part->cigar && s->has_cigar != 5 && s->has_cigar != 10) continue;

        bool mirrored = facing_left && part->flip;
        Sprite sprite = g->parts[i][part->team ? team : 0][mirrored ? 1 : 0];
        if (sprite.tex.handle == 0) continue;

        Vec2 p1 = p[part->p1 - 1];
        Vec2 p2 = p[part->p2 - 1];
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
        if (part->dread) {
            // Each dreadlock's root is its (cx, cy), in its own pixels, turned with the head;
            // from there it hangs toward point 24, each a little longer than the last.
            Vec2 head = vec2_sub(p[12 - 1], p[9 - 1]);
            float turn = atan2f(head.y, head.x) - 3.14159265f / 2;
            float dir = facing_left ? -1.0f : 1.0f;
            Vec2 root = {-part->cy * sprite.height * dir, part->cx * sprite.width};
            p1 = vec2_add(p1, vec2(root.x * cosf(turn) - root.y * sinf(turn), root.x * sinf(turn) + root.y * cosf(turn)));
            cx = 0.0f;
            cy = 0.5f;
            sx = 0.75f + 0.25f / 5 * (float)(part->dread - 1);
        } else if (part->flex > 0.0f) {
            sx = minf(1.5f, vec2_length(along) / part->flex);
        }

        Rgba tint = gostek_color(part->color, s);
        if (part->blood) tint.a = bleeding;
        if (part->nade > 0) tint.a = (uint8_t)(0.75f * (float)tint.a); // ALPHA_NADES
        draw_sprite(sprite, vec2_add(p1, vec2(0, 1)), vec2(cx * sprite.width, cy * sprite.height), vec2(sx, sy), angle, tint);
    }
}
