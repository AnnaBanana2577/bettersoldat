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
#define PAGE_SIZE 1024 // pixels square; a glyph taller than a page draws nothing
#define MAX_PAGES 8
#define MAX_TABLES 24 // sizes in use at once: the styles and the big messages' scales
#define POINTS_TO_PIXELS (96.0f / 72.0f) // the original's RequestFontSize
#define GAME_HEIGHT_UNITS 480.0f
#define MAX_PLACED 512

typedef struct Face {
    uint8_t *data;
    stbtt_fontinfo info;
} Face;

typedef struct Glyph {
    bool baked;
    int page;             // -1 when it has no image (a space)
    float u0, v0, u1, v1; // in the page
    float w, h;           // pixels
    float xoff, yoff;     // from the pen to the bitmap's top-left, pixels
    float advance;
} Glyph;

// A texture glyphs are packed into, row by row.
typedef struct Page {
    GfxTexture tex;
    int pen_x, pen_y, row_height;
} Page;

// A face at a pixel size and stretch, with the glyphs it has drawn so far: the
// original's glyph table.
typedef struct Table {
    int face;
    float pixels, stretch; // what it was made for
    float scale_x, scale_y; // font units to pixels
    float ascent, descent;  // pixels
    Glyph glyphs[GLYPH_COUNT];
    Page pages[MAX_PAGES];
    int page_count;
} Table;

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
    float style_points[FONT_STYLE_COUNT]; // each style's size for this window, the original's Size
    float style_pixels[FONT_STYLE_COUNT];
    Table tables[MAX_TABLES];
    int table_count;
    bool loaded;

    Table *table;
    Rgba color;
    Vec2 shadow_offset;
    Rgba shadow_color;
    float scale;
    Vec2 pixel_ratio;
    TextAlign align;
} text;

// --- faces and tables --------------------------------------------------------------

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

static void table_init(Table *t, int face, float pixels, float stretch)
{
    memset(t, 0, sizeof(*t));
    const Face *f = &text.faces[face];
    t->face = face;
    t->pixels = pixels;
    t->stretch = stretch;
    t->scale_y = stbtt_ScaleForMappingEmToPixels(&f->info, pixels);
    t->scale_x = t->scale_y * stretch;
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&f->info, &ascent, &descent, &line_gap);
    t->ascent = (float)ascent * t->scale_y;
    t->descent = (float)abs(descent) * t->scale_y;
}

static void table_free(Table *t)
{
    for (int i = 0; i < t->page_count; i++) gfx_texture_delete(&t->pages[i].tex);
    memset(t, 0, sizeof(*t));
}

// The table for a face at a size, made on first use; the oldest goes when there are
// too many (the sizes the big messages pass through).
static Table *table_find(int face, float pixels, float stretch)
{
    for (int i = 0; i < text.table_count; i++) {
        Table *t = &text.tables[i];
        if (t->face == face && fabsf(t->pixels - pixels) < 0.01f && t->stretch == stretch) return t;
    }
    Table *t;
    if (text.table_count < MAX_TABLES) {
        t = &text.tables[text.table_count++];
    } else {
        t = &text.tables[FONT_STYLE_COUNT]; // the styles' own tables stay; the scaled ones cycle
        table_free(t);
        memmove(&text.tables[FONT_STYLE_COUNT], &text.tables[FONT_STYLE_COUNT + 1],
                (size_t)(MAX_TABLES - FONT_STYLE_COUNT - 1) * sizeof(Table));
        t = &text.tables[MAX_TABLES - 1];
    }
    table_init(t, face, pixels, stretch);
    return t;
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
        text.style_points[i] = points;
        text.style_pixels[i] = points * POINTS_TO_PIXELS;
        table_find(STYLE_SPECS[i].face, text.style_pixels[i], STYLE_SPECS[i].stretch); // the styles come first
    }

    text.loaded = true;
    text.table = &text.tables[FONT_MENU];
    text.color = RGBA_WHITE;
    text.scale = 1.0f;
    text.pixel_ratio = vec2(1, 1);
    text.align = TEXT_TOP;
    return true;
}

void fonts_unload(void)
{
    for (int i = 0; i < text.table_count; i++) table_free(&text.tables[i]);
    for (int i = 0; i < FACE_COUNT; i++) free(text.faces[i].data);
    memset(&text, 0, sizeof(text));
}

// --- glyphs ------------------------------------------------------------------------

static Page *page_with_room(Table *t, int w, int h)
{
    if (w + 2 > PAGE_SIZE || h + 2 > PAGE_SIZE) return NULL;
    Page *p = t->page_count ? &t->pages[t->page_count - 1] : NULL;
    if (p && p->pen_x + w + 1 > PAGE_SIZE) {
        p->pen_x = 1;
        p->pen_y += p->row_height + 1;
        p->row_height = 0;
    }
    if (!p || p->pen_y + h + 1 > PAGE_SIZE) {
        if (t->page_count == MAX_PAGES) return NULL;
        p = &t->pages[t->page_count++];
        *p = (Page){.pen_x = 1, .pen_y = 1};
        uint8_t *blank = calloc((size_t)PAGE_SIZE * PAGE_SIZE * 4, 1);
        if (!blank) return NULL;
        p->tex = gfx_texture_create(PAGE_SIZE, PAGE_SIZE, blank);
        gfx_texture_filter(p->tex, false); // drawn pixel for pixel, as the original's pages
        free(blank);
    }
    return p;
}

// Rasterizes one glyph into the table's pages the first time it is asked for.
static const Glyph *glyph_get(Table *t, int c)
{
    Glyph *g = &t->glyphs[c - FIRST_GLYPH];
    if (g->baked) return g;
    g->baked = true;
    g->page = -1;

    const Face *face = &text.faces[t->face];
    int index = stbtt_FindGlyphIndex(&face->info, c);
    int advance, lsb, x0, y0, x1, y1;
    stbtt_GetGlyphHMetrics(&face->info, index, &advance, &lsb);
    stbtt_GetGlyphBitmapBox(&face->info, index, t->scale_x, t->scale_y, &x0, &y0, &x1, &y1);
    int w = x1 - x0, h = y1 - y0;
    g->advance = (float)advance * t->scale_x;
    g->xoff = (float)x0;
    g->yoff = (float)y0;
    g->w = (float)w;
    g->h = (float)h;
    if (w <= 0 || h <= 0) return g;

    Page *p = page_with_room(t, w, h);
    if (!p) return g;
    uint8_t *alpha = malloc((size_t)w * (size_t)h);
    uint8_t *rgba = malloc((size_t)w * (size_t)h * 4);
    if (!alpha || !rgba) {
        free(alpha);
        free(rgba);
        return g;
    }
    stbtt_MakeGlyphBitmap(&face->info, alpha, w, h, w, t->scale_x, t->scale_y, index);
    for (int i = 0; i < w * h; i++) {
        rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = alpha[i];
    }
    gfx_texture_update(p->tex, p->pen_x, p->pen_y, w, h, rgba);
    free(alpha);
    free(rgba);

    g->page = (int)(p - t->pages);
    g->u0 = (float)p->pen_x / PAGE_SIZE;
    g->v0 = (float)p->pen_y / PAGE_SIZE;
    g->u1 = (float)(p->pen_x + w) / PAGE_SIZE;
    g->v1 = (float)(p->pen_y + h) / PAGE_SIZE;
    p->pen_x += w + 1;
    if (h > p->row_height) p->row_height = h;
    return g;
}

// --- state -------------------------------------------------------------------------

void text_style(FontStyleId style)
{
    text_style_scaled(style, 1.0f);
}

void text_style_scaled(FontStyleId style, float scale)
{
    if (!text.loaded) return;
    text.table = table_find(STYLE_SPECS[style].face, text.style_pixels[style] * scale, STYLE_SPECS[style].stretch);
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
    return text.style_points[style];
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
    if (c == '\n') return c;
    if (c < FIRST_GLYPH || c > LAST_GLYPH) c = '?';
    return c;
}

// Where each glyph goes, in pixels from the pen's start: the original's ComputeGlyphs,
// with the face's kerning. Returns the count.
typedef struct Placed {
    const Glyph *glyph;
    float x, y;
} Placed;

static int layout(Table *t, const char *utf8, Placed *out, int max)
{
    const Face *face = &text.faces[t->face];
    float x = 0, y = 0;
    int n = 0, prev = 0;
    float line = t->ascent + t->descent;
    for (int c; n < max && (c = next_codepoint(&utf8)) != 0;) {
        if (c == '\n') {
            x = 0;
            y += line;
            prev = 0;
            continue;
        }
        if (prev) x += (float)stbtt_GetCodepointKernAdvance(&face->info, prev, c) * t->scale_x;
        const Glyph *g = glyph_get(t, c);
        out[n++] = (Placed){g, x, y};
        x += g->advance;
        prev = c;
    }
    return n;
}

static void draw_glyph(const Table *t, const Glyph *g, float x, float y, Vec2 px, Rgba color)
{
    if (g->page < 0) return;
    float w = g->w * px.x, h = g->h * px.y;
    GfxVertex v[4] = {
        gfx_vertex(x, y, g->u0, g->v0, color),
        gfx_vertex(x + w, y, g->u1, g->v0, color),
        gfx_vertex(x + w, y + h, g->u1, g->v1, color),
        gfx_vertex(x, y + h, g->u0, g->v1, color),
    };
    gfx_draw_quad(t->pages[g->page].tex, v);
}

void text_draw(const char *utf8, float x, float y)
{
    if (!text.loaded) return;
    Table *t = text.table;
    Placed placed[MAX_PLACED];
    int n = layout(t, utf8, placed, MAX_PLACED);

    Vec2 pxl = text.pixel_ratio;
    float s = text.scale;
    Rgba shadow = text.shadow_color;
    shadow.a = (uint8_t)(shadow.a * (text.color.a / 255.0f));
    float dx = text.shadow_offset.x * pxl.x, dy = text.shadow_offset.y * pxl.y;

    switch (text.align) {
    case TEXT_TOP: y += pxl.y * t->ascent * s; break;
    case TEXT_BOTTOM: y -= pxl.y * t->descent * s; break;
    case TEXT_BASELINE: break;
    }
    x = pxl.x * floorf(x / pxl.x);
    y = pxl.y * floorf(y / pxl.y);
    pxl = vec2_scale(pxl, s);

    for (int i = 0; i < n; i++) {
        const Glyph *g = placed[i].glyph;
        float gx = x + pxl.x * (placed[i].x + g->xoff);
        float gy = y + pxl.y * (placed[i].y + g->yoff);
        if (shadow.a > 0) draw_glyph(t, g, gx + dx, gy + dy, pxl, shadow);
        draw_glyph(t, g, gx, gy, pxl, text.color);
    }
}

float text_width(const char *utf8)
{
    if (!text.loaded) return 0;
    Placed placed[MAX_PLACED];
    int n = layout(text.table, utf8, placed, MAX_PLACED);
    float right = 0;
    for (int i = 0; i < n; i++) {
        const Glyph *g = placed[i].glyph;
        float r = placed[i].x + (g->w > 0 ? g->xoff + g->w : g->advance);
        if (r > right) right = r;
    }
    return right * text.pixel_ratio.x * text.scale;
}

float text_height(const char *utf8)
{
    if (!text.loaded) return 0;
    Table *t = text.table;
    int lines = 1;
    for (const char *p = utf8; *p; p++) lines += *p == '\n';
    return (float)lines * (t->ascent + t->descent) * text.pixel_ratio.y * text.scale;
}
