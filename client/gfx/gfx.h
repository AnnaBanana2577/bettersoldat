#pragma once

// Everything drawn goes through here: the GL context on the SDL window, textures from
// image files, and vertices batched into one buffer and drawn as triangles. Ported from
// opensoldat's client/Gfx.pas, which this keeps the shape of: a GLSL 1.20 shader over one
// interleaved vertex layout, premultiplied alpha throughout (images are premultiplied as
// they load, the shader premultiplies the vertex colour, and the blend is ONE,
// ONE_MINUS_SRC_ALPHA), and a mat3 transform from whatever units the caller draws in to
// the viewport.
//
// Drawing queues into a batch, split into runs by texture; the batch is flushed at the
// present, and before anything that would draw out of order (a static buffer, a new
// transform, a clear). Nothing here knows about a game, a camera or a map.

#include <SDL.h>

#include "gfx/gl.h"
#include "utils/utils.h"

// The vertex layout, as the original's TGfxVertex: 20 bytes, position, uv, colour.
typedef struct GfxVertex {
    float x, y;
    float u, v;
    Rgba color;
} GfxVertex;

static inline GfxVertex gfx_vertex(float x, float y, float u, float v, Rgba color)
{
    return (GfxVertex){x, y, u, v, color};
}

typedef struct GfxTexture {
    GLuint handle; // 0 is no texture
    int width, height;
} GfxTexture;

// Pixels in memory, straight alpha, RGBA8, top row first.
typedef struct GfxImage {
    int width, height;
    uint8_t *rgba;
} GfxImage;

// Vertices that live on the GPU: the map's polygons, built once and drawn every frame.
typedef struct GfxBuffer {
    GLuint handle;
    int count;
} GfxBuffer;

// A 3x3 matrix in column-major order, the original's TGfxMat3.
typedef struct Mat3 {
    float m[9];
} Mat3;

// --- the context -------------------------------------------------------------------

// The GL context on the window, the shader and the batch. False, with the reason on
// stderr, when the driver can't do OpenGL 2.1. Call before any other gfx_ function.
bool gfx_init(SDL_Window *window);
void gfx_destroy(void);

void gfx_viewport(int x, int y, int width, int height);

// From then on vertices are in the units this maps to the viewport.
void gfx_transform(Mat3 transform);

void gfx_clear(Rgba color);

// Flushes what is queued and swaps the window's buffers.
void gfx_present(SDL_Window *window);

// Vsync on or off, as SDL allows it.
void gfx_vsync(bool on);

// --- images and textures -----------------------------------------------------------

bool gfx_image_load(GfxImage *img, const char *path);

// Pixels of exactly `key` become transparent: the original's ApplyColorKey, for the
// scenery and the sparks keyed on pure green.
void gfx_image_color_key(GfxImage *img, Rgba key);
void gfx_image_free(GfxImage *img);

// A texture from straight-alpha RGBA8 pixels (premultiplied on the way up), linear
// filtered and clamped. Handle 0 when it can't be made.
GfxTexture gfx_texture_create(int width, int height, const uint8_t *rgba);
void gfx_texture_delete(GfxTexture *tex);

// An image file straight to a texture; false when missing. With `key`, colour-keyed.
bool gfx_texture_load(GfxTexture *tex, const char *path, const Rgba *key);

void gfx_texture_wrap(GfxTexture tex, bool repeat);
void gfx_texture_filter(GfxTexture tex, bool linear);
void gfx_texture_mipmap(GfxTexture tex); // builds mipmaps and filters through them
void gfx_texture_lod_bias(GfxTexture tex, float bias); // negative picks sharper mipmaps (r_mipmapbias)

// A 1x1 white texture, so untextured shapes draw through the same shader.
GfxTexture gfx_white(void);

// --- drawing -----------------------------------------------------------------------

// Into the batch, in the order called.
void gfx_draw_quad(GfxTexture tex, const GfxVertex v[4]);
void gfx_draw_triangles(GfxTexture tex, const GfxVertex *v, int count); // count a multiple of 3

// A line as a thin quad, `thickness` in the transform's units.
void gfx_draw_line(Vec2 a, Vec2 b, float thickness, Rgba color);

// Uploads what the batch holds and draws it. The present does this itself.
void gfx_flush(void);

GfxBuffer gfx_buffer_create(const GfxVertex *v, int count);
void gfx_buffer_delete(GfxBuffer *buf);

// `count` vertices from `offset` in a static buffer, drawn now (after a flush, so the
// order holds).
void gfx_draw_buffer(GfxBuffer buf, GfxTexture tex, int offset, int count);

// --- matrices ----------------------------------------------------------------------

// Maps the rectangle l..r, t..b onto the viewport, t at the top of the screen.
Mat3 mat3_ortho(float l, float r, float t, float b);

// The original's GfxMat3Transform: T(tx,ty) * T(cx,cy) * R(r) * T(-cx,-cy) * S(sx,sy).
Mat3 mat3_transform(float tx, float ty, float sx, float sy, float cx, float cy, float r);
Vec2 mat3_apply(Mat3 m, Vec2 p);

// An alpha in the original's 0..255 terms, clamped, and a colour with its alpha scaled.
uint8_t alpha8(float v);
Rgba rgba_faded(Rgba color, float alpha);

#define RGBA_WHITE ((Rgba){255, 255, 255, 255})
