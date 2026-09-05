#pragma once

// Everything drawn: the art every map shares (the gostek), the map's part (its texture,
// scenery and polygon meshes), and the soldiers slotted between its layers. Draws a
// RenderState (render_state.h) and reads nothing else of the game. Ported from
// soldat-odin's client/render/render.odin, minus what has nothing to draw yet:
// bullets, things, sparks and corpses.

#include "game/game.h"
#include "render/camera.h"
#include "render/gostek.h"
#include "render/map_view.h"
#include "render/render_state.h"

typedef struct RenderOptions {
    bool wireframe;
    bool debug; // spawn points, colliders, special polys, the soldiers' bones
} RenderOptions;

typedef struct Render {
    Gostek gostek;
    MapView map_view;
    const ParticleObject *bones; // the gostek's skeleton, for the debug overlay; borrowed
} Render;

// The gostek's art and the map's, from `base`. The window must be open; the context
// must outlive the renderer.
void render_init(Render *r, const char *base, const Context *ctx);
void render_destroy(Render *r);

// The world's part of the frame, in the original's layer order: the sky, the
// background polys, scenery behind, everything alive, scenery in front of it, the
// terrain, scenery in front of the players. Between BeginDrawing and EndDrawing.
void render_draw(const Render *r, const RenderState *state, const GameCamera *camera, RenderOptions options);
