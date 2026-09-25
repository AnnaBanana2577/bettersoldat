#pragma once

// Text, the original's way (Gfx.pas's font half, FreeType swapped for stb_truetype):
// a face is a .ttf; a style is a face at a size in points with a horizontal stretch.
// Each size in use has a glyph table, its glyphs rasterized the first time they are
// drawn into pages of one texture size, so a size costs only the glyphs it shows (the
// big messages and the world text run to hundreds of pixels). Text is placed in pixels
// and drawn in whatever units the transform uses, through the pixel ratio, so it stays
// crisp however the interface is scaled. One current style, colour, shadow and scale,
// as the original keeps them in its context.
//
// The styles are the original's FontStyles: two faces (font_1, font_2) and the sizes
// from the font_* cvars, at their defaults.

#include "gfx/gfx.h"

typedef enum FontStyleId {
    FONT_SMALL,        // the console, the status: face 2, 9pt
    FONT_SMALL_BOLD,   // face 2, 9pt
    FONT_SMALLEST,     // face 2, 7pt
    FONT_BIG,          // the big messages: face 1, 28pt, unscaled
    FONT_MENU,         // the menus, the HUD numbers: face 1, 12pt
    FONT_WEAPONS_MENU, // the weapon names: face 2, 8pt
    FONT_WORLD,        // text in the world: face 1, 128px
    FONT_STYLE_COUNT,
} FontStyleId;

typedef enum TextAlign { TEXT_TOP, TEXT_BOTTOM, TEXT_BASELINE } TextAlign;

// The faces from <base> (play-regular.ttf for both, as the defaults), sized for a
// window `render_height` pixels tall; the interface's 480-unit view scales to it.
// Call again when the window's height changes. False when a face is missing.
bool fonts_load(const char *base, float render_height);
void fonts_unload(void);

// What the next text draws with: the original's SetFontStyle (with its size scale),
// GfxTextColor, GfxTextShadow, GfxTextScale, GfxTextPixelRatio, GfxTextVerticalAlign.
void text_style(FontStyleId style);
void text_style_scaled(FontStyleId style, float scale); // the style at `scale` times its size
void text_color(Rgba color);
void text_shadow(float dx, float dy, Rgba color); // in pixels; alpha 0 for none
void text_scale(float scale);
void text_pixel_ratio(Vec2 units_per_pixel); // the transform's units one pixel spans
void text_align(TextAlign align);

// `x, y` in the transform's units, snapped to a pixel; the text's top-left by default.
void text_draw(const char *utf8, float x, float y);

// The text's size in the transform's units, as GfxTextMetrics is used.
float text_width(const char *utf8);
float text_height(const char *utf8);

// The style's size in points, as the original's FontStyleSize.
float text_style_size(FontStyleId style);
