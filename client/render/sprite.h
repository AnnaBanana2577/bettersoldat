#pragma once

// A sprite is a texture with its size in world units, drawn as a rotated, scaled quad.
// The gostek (and later the bullets and the sparks) all draw through draw_sprite.
// Ported from soldat-odin's client/render/sprite.odin.

#include <raylib.h>

#include "utils/utils.h"

#define GOSTEK_SCALE (1.0f / 4.5f) // sprite pixels per world unit, from mod.ini DefaultScale

typedef struct Sprite {
    Texture2D tex;
    float width, height; // world units, already scaled
} Sprite;

// Loads an image as a sprite; false if it is missing. With `color_key`, pixels exactly
// that colour become transparent (the original keys scenery and sparks on pure green).
bool sprite_load(Sprite *s, const char *path, const Color *color_key);
void sprite_unload(Sprite *s);

// A rotated, scaled quad whose `center` (world units from the sprite's top-left) lands
// on `at`: the original's DrawGostekSprite matrix, used for everything.
void draw_sprite(Sprite sprite, Vec2 at, Vec2 center, Vec2 scale, float angle, Color color);

// A textured quad over four arbitrary points with a colour per corner: the flags' cloth
// and the kits, stretched over their skeleton points.
void draw_quad(Texture2D tex, const Vec2 p[4], const Vec2 uv[4], const Color colors[4]);

// An alpha in the original's 0..255 terms, clamped.
uint8_t alpha8(float v);
Color tinted(Color tint, float alpha);
