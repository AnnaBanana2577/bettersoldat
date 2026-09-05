#include "render/sprite.h"

#include <rlgl.h>

bool sprite_load(Sprite *s, const char *path, const Color *color_key)
{
    *s = (Sprite){0};
    if (!FileExists(path)) return false;

    Texture2D tex;
    if (!color_key) {
        tex = LoadTexture(path);
    } else {
        Image img = LoadImage(path);
        if (!img.data) return false;
        ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        Color *px = img.data;
        for (int i = 0; i < img.width * img.height; i++) {
            if (px[i].r == color_key->r && px[i].g == color_key->g && px[i].b == color_key->b && px[i].a == color_key->a) {
                px[i] = (Color){0};
            }
        }
        tex = LoadTextureFromImage(img);
        UnloadImage(img);
    }
    if (tex.id == 0) return false;

    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
    *s = (Sprite){tex, (float)tex.width * GOSTEK_SCALE, (float)tex.height * GOSTEK_SCALE};
    return true;
}

void sprite_unload(Sprite *s)
{
    if (s->tex.id != 0) UnloadTexture(s->tex);
    *s = (Sprite){0};
}

void draw_sprite(Sprite sprite, Vec2 at, Vec2 center, Vec2 scale, float angle, Color color)
{
    if (sprite.tex.id == 0) return;

    float c = cosf(angle), s = sinf(angle);
    float ax = c * scale.x, ay = s * scale.x;
    float bx = -s * scale.y, by = c * scale.y;
    Vec2 origin = {at.x - center.y * bx - center.x * ax, at.y - center.y * by - center.x * ay};
    float w = sprite.width, h = sprite.height;

    Vec2 p0 = origin;
    Vec2 p1 = {origin.x + w * ax, origin.y + w * ay};
    Vec2 p2 = {origin.x + w * ax + h * bx, origin.y + w * ay + h * by};
    Vec2 p3 = {origin.x + h * bx, origin.y + h * by};

    rlSetTexture(sprite.tex.id);
    rlBegin(RL_QUADS);
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlTexCoord2f(0, 0);
    rlVertex2f(p0.x, p0.y);
    rlTexCoord2f(0, 1);
    rlVertex2f(p3.x, p3.y);
    rlTexCoord2f(1, 1);
    rlVertex2f(p2.x, p2.y);
    rlTexCoord2f(1, 0);
    rlVertex2f(p1.x, p1.y);
    rlEnd();
}

void draw_quad(Texture2D tex, const Vec2 p[4], const Vec2 uv[4], const Color colors[4])
{
    if (tex.id == 0) return;
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    for (int i = 0; i < 4; i++) {
        rlColor4ub(colors[i].r, colors[i].g, colors[i].b, colors[i].a);
        rlTexCoord2f(uv[i].x, uv[i].y);
        rlVertex2f(p[i].x, p[i].y);
    }
    rlEnd();
}

uint8_t alpha8(float v)
{
    return (uint8_t)clampf(v, 0.0f, 255.0f);
}

Color tinted(Color tint, float alpha)
{
    return (Color){tint.r, tint.g, tint.b, alpha8((float)tint.a * alpha / 255.0f)};
}
