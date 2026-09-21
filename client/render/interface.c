#include "render/interface.h"

#include <stdio.h>

#include "gfx/font.h"
#include "render/textures.h"

#define DEFAULT_WIDTH 640.0f // what the layout was drawn for
#define START_HEALTH 150.0f  // the original's STARTHEALTH
#define DEFAULT_VEST 100.0f
#define STATUS_TRANSPARENCY 200 // ui_status_transparency: the crosshair's alpha

typedef enum BarPos { BAR_HORIZONTAL, BAR_VERTICAL, BAR_TEXT } BarPos;

// The original's TInterface, filled as LoadDefaultInterfaceData fills it. Anchors are
// the icons; each bar's position is an offset from its icon's ("Rel").
static const struct {
    uint8_t alpha;
    float health_ico_x, health_ico_y, health_ico_rotate;
    float health_bar_x, health_bar_y, health_bar_rotate;
    float ammo_ico_x, ammo_ico_y, ammo_ico_rotate;
    float ammo_bar_x, ammo_bar_y, ammo_bar_rotate;
    float jet_ico_x, jet_ico_y, jet_ico_rotate;
    float jet_bar_x, jet_bar_y, jet_bar_rotate;
    float vest_bar_x, vest_bar_y, vest_bar_rotate;
    float nades_x, nades_y;
    float bullets_x, bullets_y;
    float weapon_x, weapon_y;
    float fire_ico_x, fire_ico_y, fire_ico_rotate;
    float fire_bar_x, fire_bar_y, fire_bar_rotate;
    BarPos health_bar_pos, ammo_bar_pos, jet_bar_pos, vest_bar_pos, fire_bar_pos, nades_pos;
    bool weapon_right, bullets_right; // IntAlign: 1 is right
    bool health_bar_left, ammo_bar_left, reload_bar_left, fire_bar_left, jet_bar_left, vest_bar_left;
} INT = {
    .alpha = 255,
    .health_ico_x = 5, .health_ico_y = 445 - 6,
    .ammo_ico_x = 285 - 10, .ammo_ico_y = 445 - 6,
    .jet_ico_x = 480, .jet_ico_y = 445 - 6,
    .health_bar_x = 45, .health_bar_y = 455 - 6,
    .ammo_bar_x = 352, .ammo_bar_y = 455 - 6,
    .bullets_x = 348, .bullets_y = 451,
    .jet_bar_x = 520, .jet_bar_y = 455 - 6,
    .fire_bar_x = 402, .fire_bar_y = 464,
    .fire_ico_x = 409, .fire_ico_y = 464,
    .nades_x = 305 - 7 + 10, .nades_y = 468 - 6,
    .vest_bar_x = 45, .vest_bar_y = 465 - 6,
    .weapon_x = 285, .weapon_y = 454,
    .health_bar_pos = BAR_HORIZONTAL, .ammo_bar_pos = BAR_HORIZONTAL, .jet_bar_pos = BAR_HORIZONTAL,
    .vest_bar_pos = BAR_HORIZONTAL, .fire_bar_pos = BAR_HORIZONTAL, .nades_pos = BAR_HORIZONTAL,
    .weapon_right = true, .bullets_right = true,
    .health_bar_left = true, .ammo_bar_left = true, .reload_bar_left = true, .fire_bar_left = false,
    .jet_bar_left = true, .vest_bar_left = true,
};

// What a frame's drawing is relative to: the original's globals for one RenderInterface.
typedef struct Frame {
    float game_width;  // the view's width in units; the height is GAME_HEIGHT
    float iscale_x;    // game_width / 640, how anchors stretch; y is 1
    float pixel;       // one window pixel in units
} Frame;

// --- loading -----------------------------------------------------------------------

static void hud_sprite_load(HudSprite *s, const char *base, const ScaleData *scales, const char *name)
{
    *s = (HudSprite){0};
    char dir[512], path[512], rel[256];
    snprintf(dir, sizeof(dir), "%s/interface-gfx", base);
    if (!find_image(dir, name, path, sizeof(path))) {
        fprintf(stderr, "interface image '%s' not found in %s\n", name, dir);
        return;
    }
    const Rgba green = {0, 255, 0, 255};
    if (!gfx_texture_load(&s->tex, path, &green)) return;
    snprintf(rel, sizeof(rel), "interface-gfx/%s", name);
    float scale = scale_data_get(scales, rel);
    s->width = (float)s->tex.width / scale;
    s->height = (float)s->tex.height / scale;
}

void interface_load(Interface *hud, const char *base, const ScaleData *scales)
{
    *hud = (Interface){0};
    hud_sprite_load(&hud->health, base, scales, "health.png");
    hud_sprite_load(&hud->ammo, base, scales, "ammo.png");
    hud_sprite_load(&hud->jet, base, scales, "jet.png");
    hud_sprite_load(&hud->health_bar, base, scales, "health-bar.png");
    hud_sprite_load(&hud->jet_bar, base, scales, "jet-bar.png");
    hud_sprite_load(&hud->reload_bar, base, scales, "reload-bar.png");
    hud_sprite_load(&hud->vest_bar, base, scales, "vest-bar.png");
    hud_sprite_load(&hud->fire_bar, base, scales, "fire-bar.png");
    hud_sprite_load(&hud->fire_bar_r, base, scales, "fire-bar-r.png");
    hud_sprite_load(&hud->nade, base, scales, "nade.png");
    hud_sprite_load(&hud->cluster_nade, base, scales, "cluster-nade.png");
    hud_sprite_load(&hud->dot, base, scales, "dot.png");
    hud_sprite_load(&hud->cursor, base, scales, "cursor.png");
}

void interface_unload(Interface *hud)
{
    HudSprite *all[] = {&hud->health,   &hud->ammo,       &hud->jet,  &hud->health_bar,   &hud->jet_bar,
                        &hud->reload_bar, &hud->vest_bar, &hud->fire_bar, &hud->fire_bar_r, &hud->nade,
                        &hud->cluster_nade, &hud->dot,    &hud->cursor};
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) gfx_texture_delete(&all[i]->tex);
    *hud = (Interface){0};
}

// --- drawing -----------------------------------------------------------------------

static float pixel_align(const Frame *f, float v)
{
    return f->pixel * floorf(v / f->pixel);
}

// A sprite's part `rect` (in its image's pixels) at x, y, rotated about its top-left:
// the original's GfxDrawSprite with a rect.
static void draw_part(const HudSprite *s, float x, float y, float rotation, Rgba color, float left, float top,
                      float right, float bottom)
{
    if (s->tex.handle == 0) return;
    float scale = s->width / (float)s->tex.width; // units per image pixel
    float w = (right - left) * scale, h = (bottom - top) * scale;
    float u0 = left / (float)s->tex.width, u1 = right / (float)s->tex.width;
    float v0 = top / (float)s->tex.height, v1 = bottom / (float)s->tex.height;

    Mat3 m = mat3_transform(x, y, 1, 1, 0, 0, rotation);
    Vec2 p0 = mat3_apply(m, vec2(0, 0)), p1 = mat3_apply(m, vec2(w, 0));
    Vec2 p2 = mat3_apply(m, vec2(w, h)), p3 = mat3_apply(m, vec2(0, h));
    GfxVertex v[4] = {
        gfx_vertex(p0.x, p0.y, u0, v0, color),
        gfx_vertex(p1.x, p1.y, u1, v0, color),
        gfx_vertex(p2.x, p2.y, u1, v1, color),
        gfx_vertex(p3.x, p3.y, u0, v1, color),
    };
    gfx_draw_quad(s->tex, v);
}

static void draw_sprite(const HudSprite *s, float x, float y, float rotation, Rgba color)
{
    draw_part(s, x, y, rotation, color, 0, 0, (float)s->tex.width, (float)s->tex.height);
}

static float deg_to_rad(float deg)
{
    return deg * 3.14159265f / 180.0f;
}

// The original's RenderBar: the bar's image cut to the share `p`, growing from its left
// (or its bottom), or shrinking toward its right (or its top) when not left-aligned.
static void draw_bar(const Frame *f, const HudSprite *bar, BarPos pos, float x, float rx, float y, float ry,
                     float rotation, float p, bool left_align)
{
    if (pos == BAR_TEXT || bar->tex.handle == 0) return;
    p = clampf(p, 0.0f, 1.0f);
    float w = (float)bar->tex.width, h = (float)bar->tex.height;
    float scale = bar->width / w;

    float px = pixel_align(f, rx * f->iscale_x) + (x - rx);
    float py = pixel_align(f, ry) + (y - ry);
    float left = 0, top = 0, right = w, bottom = h;
    if (left_align) {
        right = w * p;
        if (pos == BAR_VERTICAL) {
            right = w;
            top = h * (1 - p);
            py += top * scale;
        }
    } else {
        left = w * (1 - p);
        if (pos == BAR_VERTICAL) {
            left = 0;
            bottom = h * p;
            py += h * (1 - p) * scale;
        }
    }
    draw_part(bar, px, py, deg_to_rad(rotation), (Rgba){255, 255, 255, INT.alpha}, left, top, right, bottom);
}

// The bars and their icons: the original's RenderInterface, the IsInteractiveInterface
// part.
static void draw_bars(const Interface *hud, const Frame *f, const RenderSoldier *me, const Context *ctx)
{
    const Rgba color = {255, 255, 255, INT.alpha};
    const WeaponInfo *info = &ctx->weapons.info[me->weapon];
    const Weapon *gun = &me->gun;

    // health
    float x = pixel_align(f, INT.health_ico_x * f->iscale_x);
    float y = pixel_align(f, INT.health_ico_y);
    draw_sprite(&hud->health, x, y, deg_to_rad(INT.health_ico_rotate), color);
    draw_bar(f, &hud->health_bar, INT.health_bar_pos, INT.health_bar_x, INT.health_ico_x, INT.health_bar_y,
             INT.health_ico_y, INT.health_bar_rotate, me->health / START_HEALTH, INT.health_bar_left);

    if (me->vest > 0) {
        draw_bar(f, &hud->vest_bar, INT.vest_bar_pos, INT.vest_bar_x, INT.health_ico_x, INT.vest_bar_y,
                 INT.health_ico_y, INT.vest_bar_rotate, me->vest / DEFAULT_VEST, INT.vest_bar_left);
    }

    // ammo: the reload's progress while the gun is empty, else what is left in it
    x = pixel_align(f, INT.ammo_ico_x * f->iscale_x);
    y = pixel_align(f, INT.ammo_ico_y);
    draw_sprite(&hud->ammo, x, y, deg_to_rad(INT.ammo_ico_rotate), color);
    if (gun->ammo == 0 && me->weapon != WEAPON_SPAS) {
        float reload = info->stats.reload_time > 0 ? (float)gun->reload_count / (float)info->stats.reload_time : 0;
        draw_bar(f, &hud->reload_bar, INT.ammo_bar_pos, INT.ammo_bar_x, INT.ammo_ico_x, INT.ammo_bar_y,
                 INT.ammo_ico_y, INT.ammo_bar_rotate, 1.0f - reload, INT.reload_bar_left);
    } else if (gun->ammo > 0) {
        float share = info->stats.ammo > 0 ? (float)gun->ammo / (float)info->stats.ammo : 0;
        draw_bar(f, &hud->reload_bar, INT.ammo_bar_pos, INT.ammo_bar_x, INT.ammo_ico_x, INT.ammo_bar_y,
                 INT.ammo_ico_y, INT.ammo_bar_rotate, share, INT.ammo_bar_left);
    }

    // fire: the frame, then the interval's share left
    x = pixel_align(f, INT.ammo_ico_x * f->iscale_x + (INT.fire_bar_x - INT.ammo_ico_x));
    y = pixel_align(f, INT.ammo_ico_y + (INT.fire_bar_y - INT.ammo_ico_y));
    draw_sprite(&hud->fire_bar_r, x, y, deg_to_rad(INT.fire_bar_rotate), color);
    float fire = info->stats.fire_interval > 0 ? (float)gun->fire_count / (float)info->stats.fire_interval : 0;
    draw_bar(f, &hud->fire_bar, INT.fire_bar_pos, INT.fire_ico_x, INT.ammo_ico_x, INT.fire_ico_y, INT.ammo_ico_y,
             INT.fire_ico_rotate, fire, INT.fire_bar_left);

    // jets
    x = pixel_align(f, INT.jet_ico_x * f->iscale_x);
    y = pixel_align(f, INT.jet_ico_y);
    draw_sprite(&hud->jet, x, y, deg_to_rad(INT.jet_ico_rotate), color);
    if (ctx->map->start_jet > 0) {
        draw_bar(f, &hud->jet_bar, INT.jet_bar_pos, INT.jet_bar_x, INT.jet_ico_x, INT.jet_bar_y, INT.jet_ico_y,
                 INT.jet_bar_rotate, (float)me->jets / (float)ctx->map->start_jet, INT.jet_bar_left);
    }

    // the grenades on the belt, one image each
    const HudSprite *nade = &hud->nade; // the cluster's once the belt can carry them
    if (INT.nades_pos != BAR_TEXT && nade->tex.handle != 0) {
        float dx = nade->width, dy = nade->height;
        for (int j = 1; j <= me->grenades; j++) {
            if (INT.nades_pos == BAR_HORIZONTAL) {
                x = pixel_align(f, INT.ammo_ico_x * f->iscale_x + dx * (float)j + (INT.nades_x - INT.ammo_ico_x));
                y = pixel_align(f, INT.ammo_ico_y + (INT.nades_y - INT.ammo_ico_y));
            } else {
                x = pixel_align(f, INT.ammo_ico_x * f->iscale_x + (INT.nades_x - INT.ammo_ico_x));
                y = pixel_align(f, INT.ammo_ico_y + (INT.nades_y - INT.ammo_ico_y) - dy * (float)j + dy * 6);
            }
            draw_sprite(nade, x, y, 0, color);
        }
    }
}

static bool has_display_name(WeaponId id)
{
    switch (id) {
    case WEAPON_NONE:
    case WEAPON_FLAMER:
    case WEAPON_M2:
    case WEAPON_FRAG:
    case WEAPON_CLUSTER_NADE:
    case WEAPON_CLUSTER:
    case WEAPON_THROWN_KNIFE: return false;
    default: return true;
    }
}

// The numbers and names: the original's RenderPlayerInterfaceTexts, the living part.
static void draw_texts(const Frame *f, const RenderSoldier *me, const Context *ctx)
{
    if (me->dead) return;
    char str[64];

    // bullets
    text_style(FONT_MENU);
    text_color((Rgba){242, 244, 40, INT.alpha});
    float x = INT.ammo_ico_x * f->iscale_x + (INT.bullets_x - INT.ammo_ico_x);
    float y = INT.ammo_ico_y + (INT.bullets_y - INT.ammo_ico_y);
    snprintf(str, sizeof(str), "%d", me->gun.ammo);
    text_draw(str, INT.bullets_right ? x - text_width(str) : x, y);

    // the weapon
    if (has_display_name(me->weapon)) {
        x = INT.ammo_ico_x * f->iscale_x + (INT.weapon_x - INT.ammo_ico_x);
        y = INT.ammo_ico_y + (INT.weapon_y - INT.ammo_ico_y);
        text_style(FONT_WEAPONS_MENU);
        text_color((Rgba){255, 245, 177, INT.alpha});
        const char *name = ctx->weapons.info[me->weapon].name;
        text_draw(name, INT.weapon_right ? x - text_width(name) : x, y);
    }
}

// The crosshair: the original's, less the bink and the sniper line.
static void draw_cursor(const Interface *hud, const Frame *f, Vec2 cursor)
{
    const HudSprite *s = &hud->cursor;
    if (s->tex.handle == 0) return;
    float x = pixel_align(f, cursor.x - s->width / 2);
    float y = pixel_align(f, cursor.y - s->height / 2);
    draw_sprite(s, x, y, 0, (Rgba){255, 255, 255, STATUS_TRANSPARENCY});
}

void interface_draw(const Interface *hud, const RenderSoldier *me, const Context *ctx, Vec2 cursor, int fps,
                    Rect viewport)
{
    Frame f = {
        .game_width = GAME_HEIGHT * viewport.width / viewport.height,
        .pixel = GAME_HEIGHT / viewport.height,
    };
    f.iscale_x = f.game_width / DEFAULT_WIDTH;

    gfx_transform(mat3_ortho(0, f.game_width, 0, GAME_HEIGHT));
    text_pixel_ratio(vec2(f.pixel, f.pixel));
    text_shadow(0, 0, (Rgba){0});
    text_align(TEXT_TOP);
    text_scale(1.0f);

    if (me->active) {
        draw_bars(hud, &f, me, ctx);
        if (!me->dead) draw_cursor(hud, &f, cursor);
        draw_texts(&f, me, ctx);
    }

    if (hud->show_info) {
        char str[32];
        text_style(FONT_SMALL);
        text_color((Rgba){239, 170, 200, 255});
        snprintf(str, sizeof(str), "FPS: %d", fps);
        text_draw(str, 460 * f.iscale_x, 10);
    }
}
