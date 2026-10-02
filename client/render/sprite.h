#pragma once

// A sprite is a texture with its size in world units, drawn as a rotated, scaled quad.
// The gostek (and later the bullets and the sparks) all draw through draw_sprite.
// Ported from soldat-odin's client/render/sprite.odin.

#include "gfx/gfx.h"
#include "utils/utils.h"

// The scale of an image with no mod.ini key of its own: DefaultScale, 4.5 pixels per
// world unit.
#define SPRITE_DEFAULT_SCALE 4.5f

typedef struct Sprite {
    GfxTexture tex;
    float width, height; // world units, already scaled
} Sprite;

// Loads an image as a sprite; false if it is missing. With `color_key`, pixels exactly
// that colour become transparent (the original keys scenery and sparks on pure green).
bool sprite_load(Sprite *s, const char *path, const Rgba *color_key); // at SPRITE_DEFAULT_SCALE
bool sprite_load_scaled(Sprite *s, const char *path, const Rgba *color_key, float scale); // at `scale` pixels per world unit
void sprite_unload(Sprite *s);

// A rotated, scaled quad whose `center` (world units from the sprite's top-left) lands
// on `at`: the original's DrawGostekSprite matrix, used for everything.
void draw_sprite(Sprite sprite, Vec2 at, Vec2 center, Vec2 scale, float angle, Rgba color);

// A textured quad over four arbitrary points with a colour per corner: the flags' cloth
// and the kits, stretched over their skeleton points.
void draw_quad(GfxTexture tex, const Vec2 p[4], const Vec2 uv[4], const Rgba colors[4]);
