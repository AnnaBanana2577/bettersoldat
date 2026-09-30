#include "utils/utils.h"

#include <float.h>

Vec2 vec2_normalize(Vec2 v)
{
    float l = vec2_length(v);
    if (l < 0.001f && l > -0.001f) return (Vec2){0};
    return vec2_div(v, l);
}

float point_line_distance(Vec2 p1, Vec2 p2, Vec2 p3)
{
    Vec2 d = vec2_sub(p2, p1);
    float u = ((p3.x - p1.x) * d.x + (p3.y - p1.y) * d.y) / maxf(FLT_MIN, d.x * d.x + d.y * d.y);
    Vec2 closest = vec2_add(p1, vec2_scale(d, u));
    return vec2_length(vec2_sub(closest, p3));
}

int round_half_even(float x)
{
    float f = floorf(x);
    float diff = x - f;
    if (diff > 0.5f) return (int)f + 1;
    if (diff < 0.5f) return (int)f;
    int i = (int)f;
    return i % 2 == 0 ? i : i + 1;
}

static float sqr_dist(Vec2 a, Vec2 b)
{
    Vec2 d = vec2_sub(b, a);
    return d.x * d.x + d.y * d.y;
}

static bool in_range(float v, float lo, float hi) { return v >= lo && v <= hi; }

// Calc.pas IsLineIntersectingCircle: the line as y = ax + b (flipped to x = ay + b when
// steeper than 45 degrees), the circle solved against it, and the roots kept that lie on
// the segment. Up to two points; returns how many.
static int line_circle_intersections(Vec2 p1, Vec2 p2, Vec2 center, float radius, Vec2 points[2])
{
    float dx = p2.x - p1.x, dy = p2.y - p1.y;
    if (fabsf(dx) < 0.00001f && fabsf(dy) < 0.00001f) return 0;

    bool flipped = fabsf(dy) > fabsf(dx);
    if (flipped) {
        p1 = vec2(p1.y, p1.x);
        p2 = vec2(p2.y, p2.x);
        center = vec2(center.y, center.x);
        float t = dx;
        dx = dy;
        dy = t;
    }

    float a = dy / dx;
    float b = p1.y - a * p1.x;
    float a1 = a * a + 1.0f;
    float b1 = 2.0f * (a * b - a * center.y - center.x);
    float c1 = center.y * center.y - radius * radius + center.x * center.x - 2.0f * b * center.y + b * b;
    float delta = b1 * b1 - 4.0f * a1 * c1;
    if (delta < 0.0f) return 0;

    float min_x = minf(p1.x, p2.x), max_x = maxf(p1.x, p2.x);
    float min_y = minf(p1.y, p2.y), max_y = maxf(p1.y, p2.y);
    float root = sqrtf(delta), a2 = 2.0f * a1;
    int n = 0;
    for (int k = 0; k < 2; k++) {
        float x = (-b1 + (k == 0 ? -root : root)) / a2;
        float y = a * x + b;
        if (in_range(x, min_x, max_x) && in_range(y, min_y, max_y)) points[n++] = flipped ? vec2(y, x) : vec2(x, y);
    }
    return n;
}

// Calc.pas LineCircleCollision: an end inside the circle is the point; otherwise the
// crossing nearer the start.
bool line_circle_collision(Vec2 start, Vec2 end, Vec2 center, float radius, Vec2 *point)
{
    float r2 = radius * radius;
    if (sqr_dist(start, center) <= r2) {
        *point = start;
        return true;
    }
    if (sqr_dist(end, center) <= r2) {
        *point = end;
        return true;
    }

    Vec2 points[2];
    int n = line_circle_intersections(start, end, center, radius, points);
    if (n == 0) return false;
    *point = points[0];
    if (n == 2 && sqr_dist(points[0], start) > sqr_dist(points[1], start)) *point = points[1];
    return true;
}
