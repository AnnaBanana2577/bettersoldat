#include "render/sprite.h"

#include <stdlib.h>

bool sprite_load(Sprite *s, const char *path, const Rgba *color_key)
{
    *s = (Sprite){0};
    if (!gfx_texture_load(&s->tex, path, color_key)) return false;
    s->width = (float)s->tex.width * GOSTEK_SCALE;
    s->height = (float)s->tex.height * GOSTEK_SCALE;
    return true;
}

bool sprite_load_colorizable(Sprite *s, const char *path)
{
    *s = (Sprite){0};
    GfxImage image;
    if (!gfx_image_load(&image, path)) return false;

    size_t size = (size_t)image.width * (size_t)image.height * 4;
    uint8_t *mask = malloc(size);
    if (!mask) {
        gfx_image_free(&image);
        return false;
    }
    for (size_t i = 0; i < size; i += 4) {
        mask[i] = mask[i + 1] = mask[i + 2] = 255;
        mask[i + 3] = image.rgba[i + 3];
    }

    s->tex = gfx_texture_create(image.width, image.height, image.rgba);
    s->colorized_tex = gfx_texture_create(image.width, image.height, mask);
    free(mask);
    gfx_image_free(&image);
    if (!s->tex.handle || !s->colorized_tex.handle) {
        sprite_unload(s);
        return false;
    }
    s->width = (float)s->tex.width * GOSTEK_SCALE;
    s->height = (float)s->tex.height * GOSTEK_SCALE;
    return true;
}

void sprite_unload(Sprite *s)
{
    gfx_texture_delete(&s->tex);
    gfx_texture_delete(&s->colorized_tex);
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

void draw_sprite_colorized(Sprite sprite, Vec2 at, Vec2 center, Vec2 scale, float angle, Rgba color)
{
    if (sprite.colorized_tex.handle && (color.r != 255 || color.g != 255 || color.b != 255)) {
        sprite.tex = sprite.colorized_tex;
        scale = vec2_scale(scale, 0.87f);
    }
    draw_sprite(sprite, at, center, scale, angle, color);
}

void draw_quad(GfxTexture tex, const Vec2 p[4], const Vec2 uv[4], const Rgba colors[4])
{
    if (tex.handle == 0) return;
    GfxVertex v[4];
    for (int i = 0; i < 4; i++) v[i] = gfx_vertex(p[i].x, p[i].y, uv[i].x, uv[i].y, colors[i]);
    gfx_draw_quad(tex, v);
}
