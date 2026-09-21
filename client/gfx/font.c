#include "gfx/font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

#include "utils/utils.h"

#define FACE_COUNT 2
#define FIRST_GLYPH 32
#define LAST_GLYPH 255 // ASCII and Latin-1: what the interface prints
#define GLYPH_COUNT (LAST_GLYPH - FIRST_GLYPH + 1)
#define ATLAS_WIDTH 1024
#define ATLAS_MAX_HEIGHT 2048
#define POINTS_TO_PIXELS (96.0f / 72.0f) // the original's RequestFontSize
#define GAME_HEIGHT_UNITS 480.0f

typedef struct Face {
    uint8_t *data;
    stbtt_fontinfo info;
} Face;

typedef struct Glyph {
    float u0, v0, u1, v1; // in the atlas
    float w, h;           // pixels
    float xoff, yoff;     // from the pen to the bitmap's top-left, pixels
    float advance;
} Glyph;

// One style baked: a face at a pixel size and stretch, its glyphs in one texture.
typedef struct Style {
    int face;
    float scale_x, scale_y; // font units to pixels
    float ascent, descent;  // pixels
    float size;             // the em, in pixels
    GfxTexture atlas;
    Glyph glyphs[GLYPH_COUNT];
} Style;

// The original's FontStyles table: which face, the size in points, whether the size
// follows the window (s = RenderHeight / GameHeight), and the stretch (font_N_scalex).
static const struct {
    int face;
    float points;
    bool scaled;
    float stretch;
} STYLE_SPECS[FONT_STYLE_COUNT] = {
    [FONT_SMALL] = {1, 9, true, 1.25f},        [FONT_SMALL_BOLD] = {1, 9, true, 1.25f},
    [FONT_SMALLEST] = {1, 7, true, 1.25f},     [FONT_BIG] = {0, 28, false, 1.5f},
    [FONT_MENU] = {0, 12, true, 1.5f},         [FONT_WEAPONS_MENU] = {1, 8, true, 1.25f},
    [FONT_WORLD] = {0, 128, true, 1.5f},
};

static struct {
    Face faces[FACE_COUNT];
    Style styles[FONT_STYLE_COUNT];
    bool loaded;

    const Style *style;
    Rgba color;
    Vec2 shadow_offset;
    Rgba shadow_color;
    float scale;
    Vec2 pixel_ratio;
    TextAlign align;
} text;

// --- baking ------------------------------------------------------------------------

static bool face_load(Face *face, const char *path)
{
    size_t size;
    face->data = file_read_all(path, &size);
    if (!face->data) return false;
    if (!stbtt_InitFont(&face->info, face->data, stbtt_GetFontOffsetForIndex(face->data, 0))) {
        free(face->data);
        face->data = NULL;
        return false;
    }
    return true;
}

// Rasterizes every glyph into rows of an alpha bitmap and uploads it. False when the
// glyphs don't fit the largest atlas.
static bool style_bake(Style *st, const Face *face, float pixels, float stretch)
{
    st->scale_y = stbtt_ScaleForMappingEmToPixels(&face->info, pixels);
    st->scale_x = st->scale_y * stretch;
    st->size = pixels;
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&face->info, &ascent, &descent, &line_gap);
    st->ascent = (float)ascent * st->scale_y;
    st->descent = (float)abs(descent) * st->scale_y;

    uint8_t *bitmap = calloc(ATLAS_WIDTH * ATLAS_MAX_HEIGHT, 1);
    if (!bitmap) return false;

    int pen_x = 1, pen_y = 1, row_height = 0, used_height = 0;
    for (int c = FIRST_GLYPH; c <= LAST_GLYPH; c++) {
        Glyph *g = &st->glyphs[c - FIRST_GLYPH];
        int index = stbtt_FindGlyphIndex(&face->info, c);
        int advance, lsb, x0, y0, x1, y1;
        stbtt_GetGlyphHMetrics(&face->info, index, &advance, &lsb);
        stbtt_GetGlyphBitmapBox(&face->info, index, st->scale_x, st->scale_y, &x0, &y0, &x1, &y1);
        int w = x1 - x0, h = y1 - y0;

        if (pen_x + w + 1 > ATLAS_WIDTH) {
            pen_x = 1;
            pen_y += row_height + 1;
            row_height = 0;
        }
        if (pen_y + h + 1 > ATLAS_MAX_HEIGHT) {
            free(bitmap);
            return false;
        }
        if (w > 0 && h > 0) {
            stbtt_MakeGlyphBitmap(&face->info, bitmap + pen_y * ATLAS_WIDTH + pen_x, w, h, ATLAS_WIDTH, st->scale_x,
                                  st->scale_y, index);
        }
        *g = (Glyph){
            .u0 = (float)pen_x / ATLAS_WIDTH,
            .v0 = (float)pen_y,
            .u1 = (float)(pen_x + w) / ATLAS_WIDTH,
            .v1 = (float)(pen_y + h),
            .w = (float)w,
            .h = (float)h,
            .xoff = (float)x0,
            .yoff = (float)y0,
            .advance = (float)advance * st->scale_x,
        };
        pen_x += w + 1;
        if (h > row_height) row_height = h;
        used_height = pen_y + row_height + 1;
    }

    // the texture is only as tall as the glyphs needed; the v's were kept in pixels for it
    int height = 1;
    while (height < used_height) height *= 2;
    uint8_t *rgba = malloc((size_t)ATLAS_WIDTH * (size_t)height * 4);
    if (!rgba) {
        free(bitmap);
        return false;
    }
    for (int i = 0; i < ATLAS_WIDTH * height; i++) {
        rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = bitmap[i];
    }
    st->atlas = gfx_texture_create(ATLAS_WIDTH, height, rgba);
    gfx_texture_filter(st->atlas, false); // drawn pixel for pixel, as the original's pages
    free(rgba);
    free(bitmap);
    for (int i = 0; i < GLYPH_COUNT; i++) {
        st->glyphs[i].v0 /= (float)height;
        st->glyphs[i].v1 /= (float)height;
    }
    return st->atlas.handle != 0;
}

bool fonts_load(const char *base, float render_height)
{
    fonts_unload();

    // the defaults: font_1_filename and font_2_filename are both play-regular.ttf
    char path[512];
    snprintf(path, sizeof(path), "%s/play-regular.ttf", base);
    for (int i = 0; i < FACE_COUNT; i++) {
        if (!face_load(&text.faces[i], path)) {
            fprintf(stderr, "font not found: %s\n", path);
            fonts_unload();
            return false;
        }
    }

    float s = render_height / GAME_HEIGHT_UNITS;
    for (int i = 0; i < FONT_STYLE_COUNT; i++) {
        float points = STYLE_SPECS[i].points * (STYLE_SPECS[i].scaled ? s : 1.0f);
        Style *st = &text.styles[i];
        st->face = STYLE_SPECS[i].face;
        // A style too big for an atlas (the world text in a tall window) draws nothing
        // until glyphs are rasterized on demand, as the original does.
        if (!style_bake(st, &text.faces[st->face], points * POINTS_TO_PIXELS, STYLE_SPECS[i].stretch)) {
            fprintf(stderr, "font style %d too large to bake; its text is skipped\n", i);
        }
    }

    text.loaded = true;
    text.style = &text.styles[FONT_MENU];
    text.color = RGBA_WHITE;
    text.scale = 1.0f;
    text.pixel_ratio = vec2(1, 1);
    text.align = TEXT_TOP;
    return true;
}

void fonts_unload(void)
{
    for (int i = 0; i < FONT_STYLE_COUNT; i++) gfx_texture_delete(&text.styles[i].atlas);
    for (int i = 0; i < FACE_COUNT; i++) free(text.faces[i].data);
    memset(&text, 0, sizeof(text));
}

// --- state -------------------------------------------------------------------------

void text_style(FontStyleId style)
{
    text.style = &text.styles[style];
}

void text_color(Rgba color)
{
    text.color = color;
}

void text_shadow(float dx, float dy, Rgba color)
{
    text.shadow_offset = vec2(dx, dy);
    text.shadow_color = color;
}

void text_scale(float scale)
{
    text.scale = scale;
}

void text_pixel_ratio(Vec2 units_per_pixel)
{
    text.pixel_ratio = units_per_pixel;
}

void text_align(TextAlign align)
{
    text.align = align;
}

float text_style_size(FontStyleId style)
{
    return text.styles[style].size;
}

// --- drawing -----------------------------------------------------------------------

// The next code point of a UTF-8 string, or 0 at its end; what isn't ASCII or Latin-1
// draws as '?'.
static int next_codepoint(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    if (*p == 0) return 0;
    int c, n;
    if (*p < 0x80) c = *p, n = 1;
    else if ((*p & 0xE0) == 0xC0) c = *p & 0x1F, n = 2;
    else if ((*p & 0xF0) == 0xE0) c = *p & 0x0F, n = 3;
    else c = *p & 0x07, n = 4;
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            n = i;
            c = '?';
            break;
        }
        c = (c << 6) | (p[i] & 0x3F);
    }
    *s += n;
    if (c < FIRST_GLYPH || c > LAST_GLYPH) c = '?';
    return c;
}

// Where each glyph goes, in pixels from the pen's start: the original's ComputeGlyphs,
// with the face's kerning. Returns the count.
typedef struct Placed {
    const Glyph *glyph;
    float x, y;
} Placed;

static int layout(const Style *st, const char *utf8, Placed *out, int max)
{
    const Face *face = &text.faces[st->face];
    float x = 0, y = 0;
    int n = 0, prev = 0;
    float line = (st->ascent + st->descent) * 1.0f;
    for (int c; n < max && (c = next_codepoint(&utf8)) != 0;) {
        if (c == '\n') {
            x = 0;
            y += line;
            prev = 0;
            continue;
        }
        if (prev) x += (float)stbtt_GetCodepointKernAdvance(&face->info, prev, c) * st->scale_x;
        const Glyph *g = &st->glyphs[c - FIRST_GLYPH];
        out[n++] = (Placed){g, x, y};
        x += g->advance;
        prev = c;
    }
    return n;
}

static void draw_glyph(const Style *st, const Glyph *g, float x, float y, Vec2 px, Rgba color)
{
    if (g->w <= 0 || g->h <= 0) return;
    float w = g->w * px.x, h = g->h * px.y;
    GfxVertex v[4] = {
        gfx_vertex(x, y, g->u0, g->v0, color),
        gfx_vertex(x + w, y, g->u1, g->v0, color),
        gfx_vertex(x + w, y + h, g->u1, g->v1, color),
        gfx_vertex(x, y + h, g->u0, g->v1, color),
    };
    gfx_draw_quad(st->atlas, v);
}

#define MAX_PLACED 512

void text_draw(const char *utf8, float x, float y)
{
    if (!text.loaded || text.style->atlas.handle == 0) return;
    const Style *st = text.style;
    Placed placed[MAX_PLACED];
    int n = layout(st, utf8, placed, MAX_PLACED);

    Vec2 pxl = text.pixel_ratio;
    float s = text.scale;
    Rgba shadow = text.shadow_color;
    shadow.a = (uint8_t)(shadow.a * (text.color.a / 255.0f));
    float dx = text.shadow_offset.x * pxl.x, dy = text.shadow_offset.y * pxl.y;

    switch (text.align) {
    case TEXT_TOP: y += pxl.y * st->ascent * s; break;
    case TEXT_BOTTOM: y -= pxl.y * st->descent * s; break;
    case TEXT_BASELINE: break;
    }
    x = pxl.x * floorf(x / pxl.x);
    y = pxl.y * floorf(y / pxl.y);
    pxl = vec2_scale(pxl, s);

    for (int i = 0; i < n; i++) {
        const Glyph *g = placed[i].glyph;
        float gx = x + pxl.x * (placed[i].x + g->xoff);
        float gy = y + pxl.y * (placed[i].y + g->yoff);
        if (shadow.a > 0) draw_glyph(st, g, gx + dx, gy + dy, pxl, shadow);
        draw_glyph(st, g, gx, gy, pxl, text.color);
    }
}

float text_width(const char *utf8)
{
    if (!text.loaded) return 0;
    const Style *st = text.style;
    Placed placed[MAX_PLACED];
    int n = layout(st, utf8, placed, MAX_PLACED);
    float right = 0;
    for (int i = 0; i < n; i++) {
        const Glyph *g = placed[i].glyph;
        float r = placed[i].x + (g->w > 0 ? g->xoff + g->w : g->advance);
        if (r > right) right = r;
    }
    return right * text.pixel_ratio.x * text.scale;
}
