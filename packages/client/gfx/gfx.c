#include "gfx/gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO_WRITE
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_JPEG
#include <stb_image.h>

#define BATCH_MIN_VERTICES (6 * 1024)
#define BATCH_MIN_COMMANDS 256

// A run of vertices in the batch that share a texture.
typedef struct DrawCommand {
    GLuint texture;
    int offset, count;
} DrawCommand;

// One of everything: the client has one window.
static struct {
    SDL_GLContext context;
    GLuint program;
    GLint matrix_location;
    GfxTexture white;

    GLuint dither; // the 8x8 pattern, on unit 1
    GLuint vbo;    // the batch's, refilled every flush
    int vbo_capacity;
    GfxVertex *vertices;
    int vertex_count, vertex_capacity;
    DrawCommand *commands;
    int command_count, command_capacity;
} gfx;

// --- the shader --------------------------------------------------------------------

// The original's, less the dithering. The colour is premultiplied here, the textures
// as they upload, so one blend function serves everything.
static const char *VERTEX_SOURCE = "#version 120\n"
                                   "uniform mat3 mvp;\n"
                                   "attribute vec4 in_position;\n"
                                   "attribute vec2 in_texcoords;\n"
                                   "attribute vec4 in_color;\n"
                                   "varying vec2 texcoords;\n"
                                   "varying vec4 color;\n"
                                   "void main() {\n"
                                   "  color = vec4(in_color.rgb * in_color.a, in_color.a);\n"
                                   "  texcoords = in_texcoords;\n"
                                   "  gl_Position.xyw = mvp * in_position.xyw;\n"
                                   "  gl_Position.z = 0.0;\n"
                                   "}\n";

// The original dithers (r_dithering, on by default): an 8x8 ordered pattern on texture
// unit 1, tiled over the screen, breaks up the banding in the sky gradient.
static const char *FRAGMENT_SOURCE = "#version 120\n"
                                     "varying vec2 texcoords;\n"
                                     "varying vec4 color;\n"
                                     "uniform sampler2D sampler;\n"
                                     "uniform sampler2D dither;\n"
                                     "void main() {\n"
                                     "  gl_FragColor = texture2D(sampler, texcoords) * color;\n"
                                     "  gl_FragColor.rgb += vec3(texture2D(dither, gl_FragCoord.xy / 8.0).a / 32.0 - 1.0/128.0);\n"
                                     "}\n";

static const uint8_t DITHER[8 * 8] = {
    0,  32, 8,  40, 2,  34, 10, 42, 48, 16, 56, 24, 50, 18, 58, 26, 12, 44, 4,  36, 14, 46,
    6,  38, 60, 28, 52, 20, 62, 30, 54, 22, 3,  35, 11, 43, 1,  33, 9,  41, 51, 19, 59, 27,
    49, 17, 57, 25, 15, 47, 7,  39, 13, 45, 5,  37, 63, 31, 55, 23, 61, 29, 53, 21,
};

static void dither_texture_create(GLuint *handle)
{
    glActiveTexture(GL_TEXTURE1);
    glGenTextures(1, handle);
    glBindTexture(GL_TEXTURE_2D, *handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 8, 8, 0, GL_ALPHA, GL_UNSIGNED_BYTE, DITHER);
    glActiveTexture(GL_TEXTURE0); // everything else binds on unit 0
}

static GLuint shader_compile(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        char info[1024] = {0};
        glGetShaderInfoLog(shader, sizeof(info) - 1, NULL, info);
        fprintf(stderr, "shader compilation failed:\n%s\n", info);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool program_build(void)
{
    GLuint vs = shader_compile(GL_VERTEX_SHADER, VERTEX_SOURCE);
    GLuint fs = shader_compile(GL_FRAGMENT_SHADER, FRAGMENT_SOURCE);
    if (vs == 0 || fs == 0) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    GLuint program = glCreateProgram();
    glBindAttribLocation(program, 0, "in_position");
    glBindAttribLocation(program, 1, "in_texcoords");
    glBindAttribLocation(program, 2, "in_color");
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        char info[1024] = {0};
        glGetProgramInfoLog(program, sizeof(info) - 1, NULL, info);
        fprintf(stderr, "shader linking failed:\n%s\n", info);
    }
    glDetachShader(program, vs);
    glDetachShader(program, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (status == GL_FALSE) {
        glDeleteProgram(program);
        return false;
    }

    gfx.program = program;
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "sampler"), 0);
    glUniform1i(glGetUniformLocation(program, "dither"), 1);
    gfx.matrix_location = glGetUniformLocation(program, "mvp");
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    return true;
}

// The vertex layout on whichever buffer is bound.
static void vertex_layout(GLuint buffer)
{
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GfxVertex), (const void *)offsetof(GfxVertex, x));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GfxVertex), (const void *)offsetof(GfxVertex, u));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GfxVertex), (const void *)offsetof(GfxVertex, color));
}

// --- the context -------------------------------------------------------------------

bool gfx_init(SDL_Window *window)
{
    memset(&gfx, 0, sizeof(gfx));

    gfx.context = SDL_GL_CreateContext(window);
    if (!gfx.context) {
        fprintf(stderr, "could not create an OpenGL context: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(window, gfx.context);
    if (!gl_load() || !program_build()) {
        gfx_destroy();
        return false;
    }
    fprintf(stderr, "OpenGL %s\n", (const char *)glGetString(GL_VERSION));

    const uint8_t white[4] = {255, 255, 255, 255};
    gfx.white = gfx_texture_create(1, 1, white);
    glGenBuffers(1, &gfx.vbo);
    dither_texture_create(&gfx.dither);

    // the original's state: blending premultiplied, nothing culled or depth tested
    glEnable(GL_BLEND);
    glEnable(GL_MULTISAMPLE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    return true;
}

void gfx_destroy(void)
{
    if (!gfx.context) return;
    if (gfx.vbo) glDeleteBuffers(1, &gfx.vbo);
    gfx_texture_delete(&gfx.white);
    if (gfx.dither) glDeleteTextures(1, &gfx.dither);
    if (gfx.program) glDeleteProgram(gfx.program);
    free(gfx.vertices);
    free(gfx.commands);
    SDL_GL_DeleteContext(gfx.context);
    memset(&gfx, 0, sizeof(gfx));
}

void gfx_viewport(int x, int y, int width, int height)
{
    gfx_flush();
    glViewport(x, y, width, height);
}

void gfx_transform(Mat3 transform)
{
    gfx_flush();
    glUniformMatrix3fv(gfx.matrix_location, 1, GL_FALSE, transform.m);
}

void gfx_clear(Rgba c)
{
    gfx_flush();
    glClearColor(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void gfx_present(SDL_Window *window)
{
    gfx_flush();
    SDL_GL_SwapWindow(window);
}

void gfx_vsync(bool on)
{
    if (SDL_GL_SetSwapInterval(on ? 1 : 0) != 0) fprintf(stderr, "vsync: %s\n", SDL_GetError());
}

// --- images and textures -----------------------------------------------------------

bool gfx_image_load(GfxImage *img, const char *path)
{
    *img = (GfxImage){0};
    int channels = 0;
    img->rgba = stbi_load(path, &img->width, &img->height, &channels, 4);
    return img->rgba != NULL;
}

void gfx_image_color_key(GfxImage *img, Rgba key)
{
    uint8_t *px = img->rgba;
    for (int i = 0; i < img->width * img->height; i++, px += 4) {
        if (px[0] == key.r && px[1] == key.g && px[2] == key.b && px[3] == key.a) px[0] = px[1] = px[2] = px[3] = 0;
    }
}

void gfx_image_free(GfxImage *img)
{
    stbi_image_free(img->rgba);
    *img = (GfxImage){0};
}

GfxTexture gfx_texture_create(int width, int height, const uint8_t *rgba)
{
    GfxTexture tex = {0, width, height};
    if (width <= 0 || height <= 0 || !rgba) return tex;

    // premultiplied on a copy, so the caller's pixels stay straight
    size_t size = (size_t)width * (size_t)height * 4;
    uint8_t *pre = malloc(size);
    if (!pre) return tex;
    for (size_t i = 0; i < size; i += 4) {
        unsigned a = rgba[i + 3];
        pre[i + 0] = (uint8_t)(rgba[i + 0] * a / 255);
        pre[i + 1] = (uint8_t)(rgba[i + 1] * a / 255);
        pre[i + 2] = (uint8_t)(rgba[i + 2] * a / 255);
        pre[i + 3] = (uint8_t)a;
    }

    glGenTextures(1, &tex.handle);
    glBindTexture(GL_TEXTURE_2D, tex.handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pre);
    free(pre);
    return tex;
}

void gfx_texture_delete(GfxTexture *tex)
{
    if (tex->framebuffer) glDeleteFramebuffers(1, &tex->framebuffer);
    if (tex->handle) glDeleteTextures(1, &tex->handle);
    *tex = (GfxTexture){0};
}

bool gfx_texture_load(GfxTexture *tex, const char *path, const Rgba *key)
{
    *tex = (GfxTexture){0};
    GfxImage img;
    if (!gfx_image_load(&img, path)) return false;
    if (key) gfx_image_color_key(&img, *key);
    *tex = gfx_texture_create(img.width, img.height, img.rgba);
    gfx_image_free(&img);
    return tex->handle != 0;
}

void gfx_texture_wrap(GfxTexture tex, bool repeat)
{
    if (!tex.handle) return;
    glBindTexture(GL_TEXTURE_2D, tex.handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
}

void gfx_texture_filter(GfxTexture tex, bool linear)
{
    if (!tex.handle) return;
    glBindTexture(GL_TEXTURE_2D, tex.handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
}

void gfx_texture_mipmap(GfxTexture tex)
{
    if (!tex.handle) return;
    glBindTexture(GL_TEXTURE_2D, tex.handle);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

void gfx_texture_lod_bias(GfxTexture tex, float bias)
{
    if (!tex.handle) return;
    glBindTexture(GL_TEXTURE_2D, tex.handle);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, bias);
}

GfxTexture gfx_white(void)
{
    return gfx.white;
}

// --- drawing -----------------------------------------------------------------------

static bool grow(void **data, int *capacity, int needed, int minimum, size_t item)
{
    if (needed <= *capacity) return true;
    int n = *capacity ? *capacity : minimum;
    while (n < needed) n *= 2;
    void *p = realloc(*data, (size_t)n * item);
    if (!p) return false;
    *data = p;
    *capacity = n;
    return true;
}

static GfxVertex *batch_push(GfxTexture tex, int count)
{
    if (!grow((void **)&gfx.vertices, &gfx.vertex_capacity, gfx.vertex_count + count, BATCH_MIN_VERTICES,
              sizeof(GfxVertex))) {
        return NULL;
    }
    GLuint handle = tex.handle ? tex.handle : gfx.white.handle;
    DrawCommand *last = gfx.command_count ? &gfx.commands[gfx.command_count - 1] : NULL;
    if (last && last->texture == handle) {
        last->count += count;
    } else {
        if (!grow((void **)&gfx.commands, &gfx.command_capacity, gfx.command_count + 1, BATCH_MIN_COMMANDS,
                  sizeof(DrawCommand))) {
            return NULL;
        }
        gfx.commands[gfx.command_count++] = (DrawCommand){handle, gfx.vertex_count, count};
    }
    GfxVertex *out = &gfx.vertices[gfx.vertex_count];
    gfx.vertex_count += count;
    return out;
}

void gfx_draw_quad(GfxTexture tex, const GfxVertex v[4])
{
    GfxVertex *out = batch_push(tex, 6);
    if (!out) return;
    out[0] = v[0];
    out[1] = v[1];
    out[2] = v[2];
    out[3] = v[0];
    out[4] = v[2];
    out[5] = v[3];
}

void gfx_draw_triangles(GfxTexture tex, const GfxVertex *v, int count)
{
    if (count <= 0) return;
    GfxVertex *out = batch_push(tex, count);
    if (out) memcpy(out, v, (size_t)count * sizeof(GfxVertex));
}

void gfx_draw_line(Vec2 a, Vec2 b, float thickness, Rgba color)
{
    Vec2 d = vec2_normalize(vec2_sub(b, a));
    Vec2 n = vec2_scale(vec2(-d.y, d.x), thickness / 2);
    GfxVertex v[4] = {
        gfx_vertex(a.x - n.x, a.y - n.y, 0, 0, color),
        gfx_vertex(b.x - n.x, b.y - n.y, 0, 0, color),
        gfx_vertex(b.x + n.x, b.y + n.y, 0, 0, color),
        gfx_vertex(a.x + n.x, a.y + n.y, 0, 0, color),
    };
    gfx_draw_quad(gfx.white, v);
}

void gfx_flush(void)
{
    if (gfx.vertex_count == 0) return;

    vertex_layout(gfx.vbo);
    if (gfx.vertex_count > gfx.vbo_capacity) {
        int n = gfx.vbo_capacity ? gfx.vbo_capacity : BATCH_MIN_VERTICES;
        while (n < gfx.vertex_count) n *= 2;
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)n * (GLsizeiptr)sizeof(GfxVertex), NULL, GL_DYNAMIC_DRAW);
        gfx.vbo_capacity = n;
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)gfx.vertex_count * (GLsizeiptr)sizeof(GfxVertex), gfx.vertices);

    for (int i = 0; i < gfx.command_count; i++) {
        const DrawCommand *cmd = &gfx.commands[i];
        glBindTexture(GL_TEXTURE_2D, cmd->texture);
        glDrawArrays(GL_TRIANGLES, cmd->offset, cmd->count);
    }
    gfx.vertex_count = 0;
    gfx.command_count = 0;
}

GfxBuffer gfx_buffer_create(const GfxVertex *v, int count)
{
    GfxBuffer buf = {0, count};
    if (count <= 0) return buf;
    glGenBuffers(1, &buf.handle);
    glBindBuffer(GL_ARRAY_BUFFER, buf.handle);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count * (GLsizeiptr)sizeof(GfxVertex), v, GL_STATIC_DRAW);
    return buf;
}

void gfx_buffer_delete(GfxBuffer *buf)
{
    if (buf->handle) glDeleteBuffers(1, &buf->handle);
    *buf = (GfxBuffer){0};
}

void gfx_draw_buffer(GfxBuffer buf, GfxTexture tex, int offset, int count)
{
    if (!buf.handle || count <= 0) return;
    gfx_flush();
    vertex_layout(buf.handle);
    glBindTexture(GL_TEXTURE_2D, tex.handle ? tex.handle : gfx.white.handle);
    glDrawArrays(GL_TRIANGLES, offset, count);
}

// --- matrices ----------------------------------------------------------------------

Mat3 mat3_ortho(float l, float r, float t, float b)
{
    float w = r - l, h = t - b;
    Mat3 m;
    m.m[0] = 2 / w; m.m[3] = 0;     m.m[6] = -(r + l) / w;
    m.m[1] = 0;     m.m[4] = 2 / h; m.m[7] = -(t + b) / h;
    m.m[2] = 0;     m.m[5] = 0;     m.m[8] = 1;
    return m;
}

Mat3 mat3_transform(float tx, float ty, float sx, float sy, float cx, float cy, float r)
{
    float c = cosf(r), s = sinf(r);
    Mat3 m;
    m.m[0] = c * sx; m.m[3] = -s * sy; m.m[6] = tx + cy * s - c * cx + cx;
    m.m[1] = s * sx; m.m[4] = c * sy;  m.m[7] = ty - cx * s - c * cy + cy;
    m.m[2] = 0;      m.m[5] = 0;       m.m[8] = 1;
    return m;
}

Vec2 mat3_apply(Mat3 m, Vec2 p)
{
    return (Vec2){m.m[0] * p.x + m.m[3] * p.y + m.m[6], m.m[1] * p.x + m.m[4] * p.y + m.m[7]};
}

uint8_t alpha8(float v)
{
    return (uint8_t)clampf(v, 0.0f, 255.0f);
}

Rgba rgba_faded(Rgba color, float alpha)
{
    return (Rgba){color.r, color.g, color.b, alpha8((float)color.a * alpha / 255.0f)};
}

void gfx_texture_update(GfxTexture tex, int x, int y, int width, int height, const uint8_t *rgba)
{
    if (!tex.handle || width <= 0 || height <= 0) return;
    size_t size = (size_t)width * (size_t)height * 4;
    uint8_t *pre = malloc(size);
    if (!pre) return;
    for (size_t i = 0; i < size; i += 4) {
        unsigned a = rgba[i + 3];
        pre[i + 0] = (uint8_t)(rgba[i + 0] * a / 255);
        pre[i + 1] = (uint8_t)(rgba[i + 1] * a / 255);
        pre[i + 2] = (uint8_t)(rgba[i + 2] * a / 255);
        pre[i + 3] = (uint8_t)a;
    }
    glBindTexture(GL_TEXTURE_2D, tex.handle);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pre);
    free(pre);
}

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

bool gfx_save_screen(const char *path, int width, int height)
{
    gfx_flush();
    size_t stride = (size_t)width * 4;
    uint8_t *px = malloc(stride * (size_t)height);
    if (!px) return false;
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, px);
    // GL reads bottom row first; the file wants the top
    uint8_t *row = malloc(stride);
    if (!row) {
        free(px);
        return false;
    }
    for (int y = 0; y < height / 2; y++) {
        uint8_t *a = px + (size_t)y * stride, *b = px + (size_t)(height - 1 - y) * stride;
        memcpy(row, a, stride);
        memcpy(a, b, stride);
        memcpy(b, row, stride);
    }
    free(row);
    bool ok = stbi_write_png(path, width, height, 4, px, (int)stride) != 0;
    free(px);
    return ok;
}

// --- render targets ----------------------------------------------------------------

GfxTexture gfx_render_target_create(int width, int height)
{
    GfxTexture tex = {0, width, height, 0};
    if (width <= 0 || height <= 0) return tex;
    glGenTextures(1, &tex.handle);
    glBindTexture(GL_TEXTURE_2D, tex.handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glGenFramebuffers(1, &tex.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, tex.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex.handle, 0);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) {
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
        fprintf(stderr, "render target %dx%d is incomplete\n", width, height);
        gfx_texture_delete(&tex);
    }
    return tex;
}

void gfx_target(const GfxTexture *target)
{
    gfx_flush();
    glBindFramebuffer(GL_FRAMEBUFFER, target ? target->framebuffer : 0);
}
