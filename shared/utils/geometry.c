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

bool line_circle_collision(Vec2 start, Vec2 end, Vec2 center, float radius, Vec2 *point)
{
    float r2 = radius * radius;
    Vec2 sc = vec2_sub(start, center);
    Vec2 ec = vec2_sub(end, center);
    if (vec2_dot(sc, sc) <= r2) {
        *point = start;
        return true;
    }
    if (vec2_dot(ec, ec) <= r2) {
        *point = end;
        return true;
    }

    Vec2 d = vec2_sub(end, start);
    float a = vec2_dot(d, d);
    if (a < 1e-10f) return false;

    float b = 2.0f * vec2_dot(sc, d);
    float c = vec2_dot(sc, sc) - r2;
    float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return false;

    float sq = sqrtf(disc);
    float t1 = (-b - sq) / (2.0f * a);
    float t2 = (-b + sq) / (2.0f * a);
    if (t1 >= 0.0f && t1 <= 1.0f) {
        *point = vec2_add(start, vec2_scale(d, t1));
        return true;
    }
    if (t2 >= 0.0f && t2 <= 1.0f) {
        *point = vec2_add(start, vec2_scale(d, t2));
        return true;
    }
    return false;
}
