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
//
// The colours, the hair, the headgear and the chain come from the soldier's PlayerLook.
// The hairstyles are one list of six — 1 to 4 the male's, 5 to 6 the waifu's — and
// the headgear one of three (1-2 the male's, 3 the waifu's). They all live in their
// own shared folders under gostek-gfx, hair/ and headgear/, each file named by its
// style (hair1 to hair6, helm, kap, helm3), whichever gostek wears it; the waifu's
// files are 20.5-scale art, as mod.ini says. The rat and
// the furry wear only army, punk and Mr. T, and no headgear. The chains and the
// dreadlocks hang from the skeleton's points 21 to 24, which the
// simulation swings behind the neck and the head (Soldier.swing, the ragdoll's own
// points on a body) and the snapshot carries in RenderSoldier.swing. The cigar shows
// while one is in the mouth, and the helmet or hat sits in the hand while the brow is
// wiped or it is taken off (the antics).

#include "render/render_state.h"
#include "render/scale_data.h"
#include "render/sprite.h"

#define GOSTEK_PART_COUNT 59

// One sprite per (part, team 2, mirrored), plus the weapons and their muzzle flashes.
typedef struct Gostek {
    Sprite parts[GOSTEK_PART_COUNT][2][2];
    Sprite weapons[WEAPON_COUNT][2]; // [mirrored]
    Sprite flashes[WEAPON_COUNT];
    bool loaded;
} Gostek;

// The gostek's and the weapons' art from <base>/gostek-gfx/<style> and
// <base>/weapons-gfx, each sprite at the scale mod.ini gives its folder — except the
// hair, which comes from the hairstyle's own folder at its scale. A style with no art
// loads empty and draws nothing.
void gostek_load(Gostek *g, const char *base, const ScaleData *scales, GostekStyle style);
void gostek_unload(Gostek *g);

// The soldier's sprites on its pose. For a corpse (once ragdolls exist) the face hangs
// from the head point rather than the neck, so a cut head rolls off with it. Under the
// camera's transform.
void gostek_draw(const Gostek *g, const RenderSoldier *s, bool corpse);
