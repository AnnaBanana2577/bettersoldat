#pragma once

// What every part of the game shares and that knows nothing of the game:
//
//   geometry.c  Vec2, the vector operations, the distances and the one rounding
//   color.c     Rgba
//   file.c      reading files and joining paths
//
// The arithmetic is single precision on purpose: every machine must compute the same
// numbers, so nothing here may widen to double.

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// --- geometry.c --------------------------------------------------------------------

typedef struct Vec2 {
    float x, y;
} Vec2;

static inline Vec2 vec2(float x, float y) { return (Vec2){x, y}; }
static inline Vec2 vec2_add(Vec2 a, Vec2 b) { return (Vec2){a.x + b.x, a.y + b.y}; }
static inline Vec2 vec2_sub(Vec2 a, Vec2 b) { return (Vec2){a.x - b.x, a.y - b.y}; }
static inline Vec2 vec2_scale(Vec2 v, float s) { return (Vec2){v.x * s, v.y * s}; }
static inline Vec2 vec2_mul(Vec2 a, Vec2 b) { return (Vec2){a.x * b.x, a.y * b.y}; }
static inline Vec2 vec2_div(Vec2 v, float s) { return (Vec2){v.x / s, v.y / s}; }
static inline float vec2_dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
static inline bool vec2_is_zero(Vec2 v) { return v.x == 0.0f && v.y == 0.0f; }
static inline float vec2_length(Vec2 v) { return sqrtf(v.x * v.x + v.y * v.y); }

// Near-zero vectors normalize to zero rather than NaN (matches OpenSoldat).
Vec2 vec2_normalize(Vec2 v);

// Distance from p3 to the infinite line through p1 and p2.
float point_line_distance(Vec2 p1, Vec2 p2, Vec2 p3);

// Pascal's Round() is banker's rounding; sector lookups depend on it.
int round_half_even(float x);

// First intersection of segment start-end with a circle (the start if already inside).
bool line_circle_collision(Vec2 start, Vec2 end, Vec2 center, float radius, Vec2 *point);

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int mini(int a, int b) { return a < b ? a : b; }
static inline int maxi(int a, int b) { return a > b ? a : b; }

// --- color.c -----------------------------------------------------------------------

typedef struct Rgba {
    uint8_t r, g, b, a;
} Rgba;

// Soldat's files store colors as BGRA.
Rgba rgba_from_bgra(const uint8_t bgra[4]);

// A colour written as "RRGGBB" in hex, with or without a '#' first: the config's form.
// Opaque. False, with `out` left alone, if the text is anything else.
bool rgba_parse_hex(const char *text, Rgba *out);

// --- file.c ------------------------------------------------------------------------

// Reads a whole file into a null-terminated heap buffer (the terminator is not counted
// in *size). Returns NULL if the file can't be read. Free with free().
uint8_t *file_read_all(const char *path, size_t *size);

// Joins two or three path parts with '/' into out (c may be NULL). Returns out.
char *path_join(char *out, size_t out_size, const char *a, const char *b, const char *c);

// Walks a mutable text buffer line by line: returns the next line with surrounding
// whitespace trimmed (terminated in place), or NULL at the end of the text.
char *text_next_line(char **cursor);

// The files in a directory, each by name (no path), until the visitor returns false.
// Directories are passed over. False if the directory can't be read.
typedef bool (*FileVisitor)(const char *name, void *user);
bool for_each_file(const char *dir, FileVisitor fn, void *user);

// The names of the files in `dir` ending in `ext` (case-insensitively), without it, at
// most `max` of them, sorted; how many there were.
int list_files(const char *dir, const char *ext, char (*names)[64], int max);
