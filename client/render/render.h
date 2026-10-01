#pragma once

// Everything drawn: the art every map shares (the gostek, the bullets, the things),
// the map's part (its texture, scenery and polygon meshes), the soldiers and corpses,
// bullets and things slotted between its layers, and the sparks, the one part with a
// life of its own, fed each tick from the tick's events. Draws a RenderState
// (render_state.h). Ported from soldat-odin's client/render.

#include "game/game.h"
#include "render/bullet_art.h"
#include "render/camera.h"
#include "render/gostek.h"
#include "render/map_view.h"
#include "render/render_state.h"
#include "render/sparks.h"
#include "render/things_art.h"

typedef struct RenderOptions {
    bool wireframe;
    bool debug; // spawn points, colliders, special polys, the soldiers' bones
} RenderOptions;

typedef struct Render {
    Gostek gostek;
    BulletArt bullet_art;
    ThingsArt things_art;
    Sparks sparks;
    MapView map_view;
    const ParticleObject *bones; // the gostek's skeleton, for the debug overlay; borrowed
} Render;

// The gostek's art and the map's, from `base`. The window must be open; the context
// must outlive the renderer.
void render_init(Render *r, const char *base, const Context *ctx);
void render_destroy(Render *r);

// Once per tick, after the game's: the tick's events become sparks, and the sparks
// step. `w` lends the jets' flames and the corpses' bleeding their places.
void render_tick(Render *r, const Context *ctx, const World *w, const Events *events);

// The world's part of the frame, in the original's layer order: the sky, the
// background polys, scenery behind, the bullets, everyone alive and dead, the things'
// sprites, the sparks, scenery in the middle, the things' quads (cloth and kits), the
// terrain, scenery in front. Sets the transform to the camera's. `seconds` drives what
// pulses and wobbles.
void render_draw(const Render *r, const RenderState *state, const GameCamera *camera, RenderOptions options, double seconds);
