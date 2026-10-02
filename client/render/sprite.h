#pragma once

// A sprite is a texture with its size in world units, drawn as a rotated, scaled quad.
// The gostek (and later the bullets and the sparks) all draw through draw_sprite.
// Ported from soldat-odin's client/render/sprite.odin.

#include "gfx/gfx.h"
#include "utils/utils.h"

#define GOSTEK_SCALE (1.0f / 4.5f) // sprite pixels per world unit, from mod.ini DefaultScale

typedef struct Sprite {
    GfxTexture tex;
    GfxTexture colorized_tex; // white mask for replacing the sprite's colour completely
    float width, height; // world units, already scaled
} Sprite;

// Loads an image as a sprite; false if it is missing. With `color_key`, pixels exactly
// that colour become transparent (the original keys scenery and sparks on pure green).
bool sprite_load(Sprite *s, const char *path, const Rgba *color_key);
// As sprite_load, with a white mask for flat colour replacement at draw time.
bool sprite_load_colorizable(Sprite *s, const char *path);
void sprite_unload(Sprite *s);

// A rotated, scaled quad whose `center` (world units from the sprite's top-left) lands
// on `at`: the original's DrawGostekSprite matrix, used for everything.
void draw_sprite(Sprite sprite, Vec2 at, Vec2 center, Vec2 scale, float angle, Rgba color);
// Every visible pixel in `color`, flat (the mask sprite_load_colorizable made, a little
// smaller), with the art's own silhouette; the art itself for a sprite without a mask.
// Whether to draw it so is the caller's: draw_sprite draws the art.
void draw_sprite_colorized(Sprite sprite, Vec2 at, Vec2 center, Vec2 scale, float angle, Rgba color);

// A textured quad over four arbitrary points with a colour per corner: the flags' cloth
// and the kits, stretched over their skeleton points.
void draw_quad(GfxTexture tex, const Vec2 p[4], const Vec2 uv[4], const Rgba colors[4]);
