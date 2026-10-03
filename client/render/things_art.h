#pragma once

// The things as drawn, from TThing.Render and TThing.PolygonsRender by way of
// soldat-odin's r_things_art.odin: the flag's cloth is a quad stretched over its
// skeleton points so it flutters with the physics, with a handle along the pole and a
// pulsing glow in base; a kit is a quad over its box; a dropped gun hangs from its grip
// end along the grip-to-muzzle line; a parachute is three ropes and a canopy in its
// owner's shirt colour.

#include "game/game.h"
#include "render/render_state.h"
#include "render/sprite.h"
#include "mod.h"

typedef struct ThingsArt {
    Sprite cloth; // textures/objects/flag.bmp, grey, tinted per team
    Sprite kits[THING_STYLE_COUNT];
    Sprite handle;                 // objects-gfx/flag.png, along the pole
    Sprite glow;                   // objects-gfx/ilum.png, the in-base pulse
    Sprite guns[WEAPON_COUNT][2];  // the loose art of dropped guns, [flipped]
    Sprite para[2];                // gostek-gfx/para.png, para2.png
    Sprite rope;
    Sprite m2_base;
    Sprite m2[2]; // [flipped]
    bool loaded;
} ThingsArt;

void things_art_load(ThingsArt *a, const Mod *mod);
void things_art_unload(ThingsArt *a);

// The things are drawn in two passes at two depths, as the original's TThing.Render and
// TThing.PolygonsRender are: the sprites (the flag's pole and glow, the dropped guns,
// the parachute, the stat gun) just in front of the soldiers, and the quads (the flags'
// cloth, the kits) in front of the sparks and the middle scenery too.
typedef enum ThingsPass { THINGS_SPRITES, THINGS_QUADS } ThingsPass;

// One pass over every thing, its points `alpha` of the way from its last tick to this
// one; a thing about to go blinks. `soldiers` lend a parachute its owner's shirt.
// `seconds` drives the in-base glow. Under the camera's transform.
void things_draw(const ThingsArt *a, ThingsPass pass, const Thing *things, const RenderSoldier *soldiers, float alpha, double seconds);
