#pragma once

// The view: where it looks, how far it is zoomed, and the conversions between the screen
// and the world. The app owns one; input and render read it. Ported from soldat-odin's
// client/render/camera.odin. (Named GameCamera: raylib already has a Camera.)
//
// A camera carries the rectangle it draws into, so every conversion is in terms of
// that rather than the window. Left zero, it is the whole window.

#include <raylib.h>

#include "utils/utils.h"

#define GAME_HEIGHT 480.0f // the original's view: 480 units tall, the width follows the window
#define CAMERA_MIN_ZOOM 0.05f
#define CAMERA_MAX_ZOOM 40.0f

typedef struct GameCamera {
    Vec2 pos;
    float zoom; // the view's height in GAME_HEIGHT units: 1 is the original's view
    Rectangle viewport; // where on screen it draws; zero for the whole window
} GameCamera;

Rectangle camera_viewport(const GameCamera *c);

// What the camera shows, in world units.
Vec2 camera_view_size(const GameCamera *c);

// The camera chases a target and leads toward the cursor (in screen pixels), as the
// original does, per frame at the frame's dt so it feels the same at any frame rate.
void camera_follow(GameCamera *c, Vec2 target, Vec2 cursor, double dt);

Vec2 screen_to_world(const GameCamera *c, Vec2 p);
Vec2 world_to_screen(const GameCamera *c, Vec2 p);
float pixels_per_unit(const GameCamera *c);

// The raylib camera this one describes, for BeginMode2D.
Camera2D camera_rl(const GameCamera *c);

// Zoom about a point on screen, so whatever is under it stays under it.
void camera_zoom_at(GameCamera *c, float factor, Vec2 screen);
