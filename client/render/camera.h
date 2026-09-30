#pragma once

// The view: where it looks, and the conversions between the screen and the world. The
// app owns one; input and render read it. Ported from soldat-odin's
// client/render/camera.odin.
//
// The view is always GAME_HEIGHT units tall, as the original's is, so every player sees
// the same amount of the world whatever their window: there is no zoom. A camera
// carries the rectangle it draws into, in window pixels, so every conversion is in
// terms of that rather than the window. The app sets it each frame from the window's
// size (or a part of it, for an editor's panes).

#include "gfx/gfx.h"
#include "utils/utils.h"

#define GAME_HEIGHT 480.0f // the original's view: 480 units tall, the width follows the window

typedef struct Rect {
    float x, y, width, height;
} Rect;

typedef struct GameCamera {
    Vec2 pos;
    Rect viewport; // where on screen it draws, in pixels
} GameCamera;

// What the camera shows, in world units.
Vec2 camera_view_size(const GameCamera *c);

// The camera chases a target and leads toward the cursor (in screen pixels), as the
// original does, per frame at the frame's dt so it feels the same at any frame rate.
void camera_follow(GameCamera *c, Vec2 target, Vec2 cursor, double dt);

Vec2 screen_to_world(const GameCamera *c, Vec2 p);
Vec2 world_to_screen(const GameCamera *c, Vec2 p);
float pixels_per_unit(const GameCamera *c);

// The transform from the world to the viewport, for gfx_transform: the original's
// GfxMat3Ortho over the rectangle the camera shows.
Mat3 camera_transform(const GameCamera *c);
