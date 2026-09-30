#include "render/camera.h"

#include "game/systems/systems.h"

#define CAMERA_SPEED 0.14f // the share of the distance to the target closed per tick (CAMSPEED)

Vec2 camera_view_size(const GameCamera *c)
{
    return (Vec2){GAME_HEIGHT * c->viewport.width / c->viewport.height, GAME_HEIGHT};
}

void camera_follow(GameCamera *c, Vec2 target, Vec2 cursor, float aim_dist, double dt)
{
    if (aim_dist < 1.0f) aim_dist = DEFAULT_AIM_DIST;
    float w = c->viewport.width, h = c->viewport.height;
    float game_w = GAME_HEIGHT * w / h, game_h = GAME_HEIGHT;
    Vec2 off = {
        clampf((cursor.x - c->viewport.x - w / 2) * game_w / w, -game_w / 2, game_w / 2),
        clampf((cursor.y - c->viewport.y - h / 2) * game_h / h, -game_h / 2, game_h / 2),
    };
    // UpdateFrame.pas: the lead is the offset over the aim distance, with the original's
    // wide-screen term, and its correction for a scoped aim distance on a wide view
    float factor = (2.0f * 640.0f / game_w - 1.0f) + (game_w - 640.0f) / game_w * (DEFAULT_AIM_DIST - aim_dist) / 6.8f;
    float ticks = (float)dt * TICK_RATE;
    float k = 1.0f - powf(1.0f - CAMERA_SPEED, ticks);
    c->pos.x += (target.x - c->pos.x) * k + off.x / aim_dist * factor * ticks;
    c->pos.y += (target.y - c->pos.y) * k + off.y / aim_dist * ticks;
}

Vec2 screen_to_world(const GameCamera *c, Vec2 p)
{
    Rect vp = c->viewport;
    Vec2 view = camera_view_size(c);
    return (Vec2){
        c->pos.x - view.x / 2 + (p.x - vp.x) * view.x / vp.width,
        c->pos.y - view.y / 2 + (p.y - vp.y) * view.y / vp.height,
    };
}

Vec2 world_to_screen(const GameCamera *c, Vec2 p)
{
    Rect vp = c->viewport;
    Vec2 view = camera_view_size(c);
    return (Vec2){
        vp.x + (p.x - c->pos.x + view.x / 2) * vp.width / view.x,
        vp.y + (p.y - c->pos.y + view.y / 2) * vp.height / view.y,
    };
}

float pixels_per_unit(const GameCamera *c)
{
    return c->viewport.height / GAME_HEIGHT;
}

Mat3 camera_transform(const GameCamera *c)
{
    Vec2 view = camera_view_size(c);
    float dx = c->pos.x - view.x / 2, dy = c->pos.y - view.y / 2;
    return mat3_ortho(dx, dx + view.x, dy, dy + view.y);
}
