#include "render/camera.h"

#include "game/entities.h"

#define CAMERA_SPEED 0.14f   // the share of the distance to the target closed per tick
#define CAMERA_AIM_DIST 7.0f // the cursor's lead: the view slides toward where you aim

Rectangle camera_viewport(const GameCamera *c)
{
    if (c->viewport.width > 0 && c->viewport.height > 0) return c->viewport;
    return (Rectangle){0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()};
}

Vec2 camera_view_size(const GameCamera *c)
{
    Rectangle vp = camera_viewport(c);
    float h = GAME_HEIGHT * c->zoom;
    return (Vec2){h * vp.width / vp.height, h};
}

void camera_follow(GameCamera *c, Vec2 target, Vec2 cursor, double dt)
{
    Rectangle vp = camera_viewport(c);
    float w = vp.width, h = vp.height;
    float game_w = GAME_HEIGHT * w / h, game_h = GAME_HEIGHT;
    Vec2 off = {
        clampf((cursor.x - w / 2) * game_w / w, -game_w / 2, game_w / 2),
        clampf((cursor.y - h / 2) * game_h / h, -game_h / 2, game_h / 2),
    };
    float factor = 2.0f * 640.0f / game_w - 1.0f; // the original's wide-screen term
    float ticks = (float)dt * TICK_RATE;
    float k = 1.0f - powf(1.0f - CAMERA_SPEED, ticks);
    c->pos.x += (target.x - c->pos.x) * k + c->zoom * off.x / CAMERA_AIM_DIST * factor * ticks;
    c->pos.y += (target.y - c->pos.y) * k + c->zoom * off.y / CAMERA_AIM_DIST * ticks;
}

Vec2 screen_to_world(const GameCamera *c, Vec2 p)
{
    Rectangle vp = camera_viewport(c);
    Vec2 view = camera_view_size(c);
    return (Vec2){
        c->pos.x - view.x / 2 + (p.x - vp.x) * view.x / vp.width,
        c->pos.y - view.y / 2 + (p.y - vp.y) * view.y / vp.height,
    };
}

Vec2 world_to_screen(const GameCamera *c, Vec2 p)
{
    Rectangle vp = camera_viewport(c);
    Vec2 view = camera_view_size(c);
    return (Vec2){
        vp.x + (p.x - c->pos.x + view.x / 2) * vp.width / view.x,
        vp.y + (p.y - c->pos.y + view.y / 2) * vp.height / view.y,
    };
}

float pixels_per_unit(const GameCamera *c)
{
    return camera_viewport(c).height / (GAME_HEIGHT * c->zoom);
}

Camera2D camera_rl(const GameCamera *c)
{
    Rectangle vp = camera_viewport(c);
    return (Camera2D){
        .offset = {vp.x + vp.width / 2, vp.y + vp.height / 2},
        .target = {c->pos.x, c->pos.y},
        .zoom = vp.height / (GAME_HEIGHT * c->zoom),
    };
}

void camera_zoom_at(GameCamera *c, float factor, Vec2 screen)
{
    // A smaller zoom shows less of the world, so the factor is inverted.
    Vec2 before = screen_to_world(c, screen);
    c->zoom = clampf(c->zoom / factor, CAMERA_MIN_ZOOM, CAMERA_MAX_ZOOM);
    Vec2 after = screen_to_world(c, screen);
    c->pos = vec2_add(c->pos, vec2_sub(before, after));
}
