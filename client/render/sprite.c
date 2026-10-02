#include "render/sprite.h"

bool sprite_load_scaled(Sprite *s, const char *path, const Rgba *color_key, float scale)
{
    *s = (Sprite){0};
    if (!gfx_texture_load(&s->tex, path, color_key)) return false;
    s->width = (float)s->tex.width / scale;
    s->height = (float)s->tex.height / scale;
    return true;
}

bool sprite_load(Sprite *s, const char *path, const Rgba *color_key)
{
    return sprite_load_scaled(s, path, color_key, SPRITE_DEFAULT_SCALE);
}

void sprite_unload(Sprite *s)
{
    gfx_texture_delete(&s->tex);
    *s = (Sprite){0};
}

void draw_sprite(Sprite sprite, Vec2 at, Vec2 center, Vec2 scale, float angle, Rgba color)
{
    if (sprite.tex.handle == 0) return;

    float c = cosf(angle), s = sinf(angle);
    float ax = c * scale.x, ay = s * scale.x;
    float bx = -s * scale.y, by = c * scale.y;
    Vec2 origin = {at.x - center.y * bx - center.x * ax, at.y - center.y * by - center.x * ay};
    float w = sprite.width, h = sprite.height;

    GfxVertex v[4] = {
        gfx_vertex(origin.x, origin.y, 0, 0, color),
        gfx_vertex(origin.x + w * ax, origin.y + w * ay, 1, 0, color),
        gfx_vertex(origin.x + w * ax + h * bx, origin.y + w * ay + h * by, 1, 1, color),
        gfx_vertex(origin.x + h * bx, origin.y + h * by, 0, 1, color),
    };
    gfx_draw_quad(sprite.tex, v);
}

void draw_quad(GfxTexture tex, const Vec2 p[4], const Vec2 uv[4], const Rgba colors[4])
{
    if (tex.handle == 0) return;
    GfxVertex v[4];
    for (int i = 0; i < 4; i++) v[i] = gfx_vertex(p[i].x, p[i].y, uv[i].x, uv[i].y, colors[i]);
    gfx_draw_quad(tex, v);
}
