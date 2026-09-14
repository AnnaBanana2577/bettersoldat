#pragma once

// The gostek: layered sprites pinned to the skeleton pose, from GostekGraphics.pas by way
// of soldat-odin's client/render/gostek.odin.
//
// Each part is a quad pinned between two skeleton points: it sits at p1, rotates to face
// p2, and is offset so the sprite's normalized point (cx, cy) lands on p1. Some parts
// stretch along their length (flex), most have a mirrored image for facing left, most
// have a team-2 variant. The table's order is the draw order. The wounds (ranny/)
// follow the part each covers and show as health runs low. The weapons are more
// entries of the same shape.

#include "render/render_state.h"
#include "render/sprite.h"

#define GOSTEK_PART_COUNT 35

// One sprite per (part, team 2, mirrored), plus the weapons and their muzzle flashes.
typedef struct Gostek {
    Sprite parts[GOSTEK_PART_COUNT][2][2];
    Sprite weapons[WEAPON_COUNT][2]; // [mirrored]
    Sprite flashes[WEAPON_COUNT];
    bool loaded;
} Gostek;

// The gostek's and the weapons' art from <base>/gostek-gfx and <base>/weapons-gfx.
void gostek_load(Gostek *g, const char *base);
void gostek_unload(Gostek *g);

// The soldier's sprites on its pose. For a corpse (once ragdolls exist) the face hangs
// from the head point rather than the neck, so a cut head rolls off with it. Under the
// camera's transform.
void gostek_draw(const Gostek *g, const RenderSoldier *s, bool corpse);
