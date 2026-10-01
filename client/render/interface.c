#include "render/interface.h"

#include <stdio.h>
#include <string.h>

#include "gfx/font.h"
#include "render/textures.h"

#define DEFAULT_WIDTH 640.0f // what the layout was drawn for
#define START_HEALTH 150.0f  // the original's STARTHEALTH
#define DEFAULT_VEST 100.0f
#define STATUS_TRANSPARENCY 200 // ui_status_transparency
#define BACKGROUND_WIDTH 64.0f  // the original's back image, in units
#define FRAGSMENU_PLAYER_HEIGHT 15
#define KILLCONSOLE_SEPARATE_HEIGHT 8
#define FONT_WEAPONMENUSIZE 8         // font_weaponmenusize
#define FONT_CONSOLELINEHEIGHT 1.5f   // font_consolelineheight
#define MORECHATTEXT 60               // chat longer than this doesn't show above the head
#define TICK_RATE_HUD 60.0f

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
    float team_box_x, team_box_y;
    float ping_x, ping_y;
    float status_x, status_y;
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
    .team_box_x = 575, .team_box_y = 330,
    .status_x = 575, .status_y = 421,
    .ping_x = 600, .ping_y = 18,
    .weapon_x = 285, .weapon_y = 454,
    .health_bar_pos = BAR_HORIZONTAL, .ammo_bar_pos = BAR_HORIZONTAL, .jet_bar_pos = BAR_HORIZONTAL,
    .vest_bar_pos = BAR_HORIZONTAL, .fire_bar_pos = BAR_HORIZONTAL, .nades_pos = BAR_HORIZONTAL,
    .weapon_right = true, .bullets_right = true,
    .health_bar_left = true, .ammo_bar_left = true, .reload_bar_left = true, .fire_bar_left = false,
    .jet_bar_left = true, .vest_bar_left = true,
};

// The original's message colours (Constants.pas), $AARRGGBB there.
#define COLOR_ENTER HUD_COLOR_ENTER
#define COLOR_GAME HUD_COLOR_GAME
static const Rgba COLOR_ABOVECHAT = {0xFD, 0xFD, 0xF9, 0xFF};
#define COLOR_CHAT HUD_COLOR_CHAT
#define COLOR_TEAMCHAT HUD_COLOR_TEAMCHAT
static const Rgba COLOR_CHARLIEJ = {0xDF, 0xDF, 0x53, 0xFF};
static const Rgba COLOR_DELTAJ = {0x53, 0xDF, 0x53, 0xFF}; // DELTAJ_MESSAGE_COLOR
static const Rgba COLOR_OUTOFSCREEN = {0xFF, 0xFF, 0xFF, 0xFF};      // white
static const Rgba COLOR_OUTOFSCREEN_FLAG = {0xF8, 0xF8, 0x35, 0xFF}; // the carrier
static const Rgba COLOR_OUTOFSCREEN_DEAD = {0xFF, 0x2E, 0x2E, 0xFF};

// What a frame's drawing is relative to: the original's globals for one RenderInterface.
typedef struct Frame {
    float game_width; // the view's width in units; the height is GAME_HEIGHT
    float iscale_x;   // game_width / 640, how anchors stretch; y is 1
    float pixel;      // one window pixel in units
    float fragx;      // where the frags menu starts
    const GameCamera *camera;
} Frame;

// --- loading -----------------------------------------------------------------------

static void hud_sprite_load(HudSprite *s, const char *base, const ScaleData *scales, const char *name)
{
    *s = (HudSprite){0};
    char dir[512], path[512], rel[256], file[128];
    snprintf(rel, sizeof(rel), "interface-gfx/%s", name);
    const char *slash = strrchr(name, '/');
    snprintf(dir, sizeof(dir), "%s/interface-gfx%s%.*s", base, slash ? "/" : "", slash ? (int)(slash - name) : 0, name);
    snprintf(file, sizeof(file), "%s", slash ? slash + 1 : name);
    if (!find_image(dir, file, path, sizeof(path))) {
        fprintf(stderr, "interface image '%s' not found in %s\n", file, dir);
        return;
    }
    const Rgba green = {0, 255, 0, 255};
    if (!gfx_texture_load(&s->tex, path, &green)) return;
    float scale = scale_data_get(scales, rel);
    s->width = (float)s->tex.width / scale;
    s->height = (float)s->tex.height / scale;
}

// The kill console's icon for each weapon: the original's GFX_INTERFACE_GUNS_ files.
static const char *GUN_ICONS[WEAPON_COUNT] = {
    [WEAPON_NONE] = "guns/fist.png",       [WEAPON_EAGLE] = "guns/1.png",     [WEAPON_MP5] = "guns/2.png",
    [WEAPON_AK74] = "guns/3.png",          [WEAPON_STEYR] = "guns/4.png",     [WEAPON_SPAS] = "guns/5.png",
    [WEAPON_RUGER] = "guns/6.png",         [WEAPON_M79] = "guns/7.png",       [WEAPON_BARRETT] = "guns/8.png",
    [WEAPON_M249] = "guns/9.png",          [WEAPON_MINIGUN] = "guns/0.png",   [WEAPON_COLT] = "guns/10.png",
    [WEAPON_KNIFE] = "guns/knife.png",     [WEAPON_CHAINSAW] = "guns/chainsaw.png", [WEAPON_LAW] = "guns/law.png",
    [WEAPON_FLAMER] = "guns/flamer.png",   [WEAPON_BOW] = "guns/bow.png",     [WEAPON_BOW2] = "guns/bow.png",
    [WEAPON_M2] = "guns/m2.png",           [WEAPON_THROWN_KNIFE] = "guns/knife.png",
};

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
    hud_sprite_load(&hud->back, base, scales, "back.png");
    hud_sprite_load(&hud->noflag, base, scales, "noflag.png");
    hud_sprite_load(&hud->arrow, base, scales, "arrow.png");
    hud_sprite_load(&hud->scroll, base, scales, "scroll.png");
    hud_sprite_load(&hud->menucursor, base, scales, "menucursor.png");
    hud_sprite_load(&hud->smalldot, base, scales, "smalldot.png");
    hud_sprite_load(&hud->overlay, base, scales, "overlay.png");
    hud_sprite_load(&hud->sight, base, scales, "sight.png");
    hud_sprite_load(&hud->deaddot, base, scales, "deaddot.png");
    hud_sprite_load(&hud->flag, base, scales, "flag.png");
    hud_sprite_load(&hud->bot, base, scales, "bot.png");
    hud_sprite_load(&hud->connection, base, scales, "connection.png");
    for (int i = 0; i < WEAPON_COUNT; i++) {
        if (GUN_ICONS[i]) hud_sprite_load(&hud->guns[i], base, scales, GUN_ICONS[i]);
    }
}

void interface_unload(Interface *hud)
{
    HudSprite *all[] = {&hud->health,   &hud->ammo,       &hud->jet,      &hud->health_bar, &hud->jet_bar,
                        &hud->reload_bar, &hud->vest_bar, &hud->fire_bar, &hud->fire_bar_r, &hud->nade,
                        &hud->cluster_nade, &hud->dot,    &hud->cursor,   &hud->back,       &hud->noflag,
                        &hud->arrow,    &hud->scroll,   &hud->menucursor, &hud->smalldot, &hud->overlay,
                        &hud->sight,    &hud->deaddot,  &hud->flag,       &hud->bot,      &hud->connection};
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) gfx_texture_delete(&all[i]->tex);
    for (int i = 0; i < WEAPON_COUNT; i++) gfx_texture_delete(&hud->guns[i].tex);
    *hud = (Interface){0};
}

// --- primitives --------------------------------------------------------------------

static float pixel_align(const Frame *f, float v)
{
    return f->pixel * floorf(v / f->pixel);
}

static float deg_to_rad(float deg)
{
    return deg * 3.14159265f / 180.0f;
}

// A point in the world, on the interface: the original's WorldToInterface about the
// camera, from the view's middle.
static Vec2 world_to_interface(const Frame *f, Vec2 p)
{
    const GameCamera *c = f->camera;
    return (Vec2){p.x - c->pos.x + f->game_width / 2, p.y - c->pos.y + GAME_HEIGHT / 2};
}

// A sprite's part `rect` (in its image's pixels) at x, y, scaled and rotated about its
// top-left: the original's GfxDrawSprite with a rect.
static void draw_part(const HudSprite *s, float x, float y, float sx, float sy, float rotation, Rgba color,
                      float left, float top, float right, float bottom)
{
    if (s->tex.handle == 0) return;
    float scale = s->width / (float)s->tex.width; // units per image pixel
    float w = (right - left) * scale, h = (bottom - top) * scale;
    float u0 = left / (float)s->tex.width, u1 = right / (float)s->tex.width;
    float v0 = top / (float)s->tex.height, v1 = bottom / (float)s->tex.height;

    Mat3 m = mat3_transform(x, y, sx, sy, 0, 0, rotation);
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
    draw_part(s, x, y, 1, 1, rotation, color, 0, 0, (float)s->tex.width, (float)s->tex.height);
}

static void draw_sprite_scaled(const HudSprite *s, float x, float y, float sx, float sy, Rgba color)
{
    draw_part(s, x, y, sx, sy, 0, color, 0, 0, (float)s->tex.width, (float)s->tex.height);
}

static void draw_rect(float x0, float y0, float x1, float y1, Rgba color)
{
    GfxVertex v[4] = {
        gfx_vertex(x0, y0, 0, 0, color),
        gfx_vertex(x1, y0, 0, 0, color),
        gfx_vertex(x1, y1, 0, 0, color),
        gfx_vertex(x0, y1, 0, 0, color),
    };
    gfx_draw_quad(gfx_white(), v);
}

// A pixel-tall line `w` units long: the original's DrawLine.
static void draw_line(const Frame *f, float x, float y, float w, Rgba color)
{
    float x0 = pixel_align(f, x), y0 = pixel_align(f, y);
    draw_rect(x0, y0, pixel_align(f, x0 + w), y0 + f->pixel, color);
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
    draw_part(bar, px, py, 1, 1, deg_to_rad(rotation), (Rgba){255, 255, 255, INT.alpha}, left, top, right, bottom);
}

static Rgba with_alpha(Rgba c, int a)
{
    c.a = (uint8_t)clampi(a, 0, 255);
    return c;
}

static Rgba team_message_color(Team team)
{
    switch (team) {
    case TEAM_ALPHA: return (Rgba){255, 0, 0, 255};
    case TEAM_BRAVO: return (Rgba){0, 0, 255, 255};
    case TEAM_CHARLIE: return COLOR_CHARLIEJ;
    case TEAM_DELTA: return COLOR_DELTAJ;
    default: return COLOR_ENTER;
    }
}

// --- the players, sorted -----------------------------------------------------------

// The original keeps SortedPlayers by kills; here the ranking is made where it is read.
static int rank_players(const HudData *d, int out[MAX_PLAYERS])
{
    int n = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (d->players[i].active) out[n++] = i;
    }
    for (int i = 1; i < n; i++) { // insertion sort, by kills descending, then by deaths
        int p = out[i], j = i - 1;
        while (j >= 0 && (d->players[out[j]].kills < d->players[p].kills ||
                          (d->players[out[j]].kills == d->players[p].kills &&
                           d->players[out[j]].deaths > d->players[p].deaths))) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = p;
    }
    return n;
}

static int count_spectators(const HudData *d)
{
    int n = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) n += d->players[i].active && d->players[i].spectator;
    return n;
}

static int count_team(const HudData *d, Team team)
{
    int n = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        n += d->players[i].active && !d->players[i].spectator && d->players[i].team == team;
    }
    return n;
}

// --- the sections, in the original's order -----------------------------------------

// The big messages, centred where they were posted, shrinking to fit the window.
static void draw_big_messages(const Frame *f, const HudData *d, Rect viewport)
{
    int npot = 1;
    while (npot < (int)viewport.height / 2) npot *= 2;
    float max_size = 0.8f * (float)npot;

    for (int i = 0; i < d->big_count; i++) {
        const HudBigMessage *m = &d->big[i];
        if (m->delay <= 0) continue;
        int alpha = m->color.a ? m->color.a : 255;
        alpha = clampi(3 * m->delay + 25, 0, alpha);
        float scale = m->scale * (viewport.height / GAME_HEIGHT) * 4.8f;
        float x = m->x;
        if (m->centered) { // the original's BigMessage: no wider than 0.7 of the view, in the middle
            text_style_scaled(FONT_BIG, scale);
            float w = text_width(m->text);
            if (w > 0.7f * f->game_width) scale *= 0.7f * f->game_width / w;
            text_style_scaled(FONT_BIG, scale);
            x = (f->game_width - text_width(m->text)) / 2;
        }
        float extra = 1.0f;
        if (scale * text_style_size(FONT_BIG) > max_size) {
            extra = scale;
            scale = max_size / text_style_size(FONT_BIG);
            extra /= scale;
        }
        text_align(i == 1 ? TEXT_BASELINE : TEXT_TOP); // the original: the message layer sits on its baseline
        text_scale(extra);
        text_style_scaled(FONT_BIG, scale);
        text_color(with_alpha(m->color, alpha));
        float a = alpha / 255.0f;
        text_shadow(1, 1, (Rgba){0, 0, 0, (uint8_t)(a * a * a * a * alpha)});
        text_draw(m->text, x, m->y);
        text_align(TEXT_TOP);
        text_scale(1.0f);
    }
    text_shadow(0, 0, (Rgba){0});
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

// The crosshair, bigger with the bink, coloured by whoever is under it: the original's,
// less the sniper line.
static void draw_cursor(const Interface *hud, const Frame *f, const HudData *d, const RenderSoldier *me, Vec2 cursor)
{
    const HudSprite *s = &hud->cursor;
    if (s->tex.handle == 0) return;

    float scale = 1.0f;
    float inaccuracy = (float)me->hit_spray + me->move_acc * 100.0f;
    if (inaccuracy > 0) scale += powf(inaccuracy, 0.6f) / 20.0f * scale;

    int alpha = STATUS_TRANSPARENCY;
    Rgba color = {255, 255, 255, 255};
    if (d->cursor_text[0]) {
        alpha = STATUS_TRANSPARENCY - 50;
        color = d->cursor_friendly ? (Rgba){0x33, 0xFF, 0x33, 255} : (Rgba){0xFF, 0x33, 0x33, 255};
    }
    float x = pixel_align(f, cursor.x - s->width / 2 * scale);
    float y = pixel_align(f, cursor.y - s->height / 2 * scale);
    draw_sprite_scaled(s, x, y, scale, scale, with_alpha(color, alpha));
}

// The arrow over my head, bobbing; steadier and fainter while spawn protection lasts.
static void draw_player_indicator(const Interface *hud, const Frame *f, const HudData *d, const RenderSoldier *me)
{
    const HudSprite *s = &hud->arrow;
    if (s->tex.handle == 0 || !me->active) return;
    Vec2 at = world_to_interface(f, me->pose.p[12 - 1]);
    float x = at.x - s->width / 2, y = at.y - s->height / 2 - 15;
    int alpha;
    if (me->spawn_protected && !d->survival) {
        alpha = d->cease_fire_counter * 2 + 75;
    } else {
        alpha = 100;
        y += 2 * sinf(5.1f * (float)d->time);
    }
    draw_sprite(s, x, y, 0, (Rgba){255, 255, 255, (uint8_t)clampi(alpha, 0, 255)});
}

// The ping dot, greener and smaller the lower the ping.
static void draw_ping_dot(const Interface *hud, const Frame *f, const HudData *d)
{
    if (!d->player_names || hud->dot.tex.handle == 0) return;
    int ping = d->ping;
    float x = INT.ping_x * f->iscale_x, y = INT.ping_y;
    float sx = 0.5f + ping / 600.0f, sy = 0.45f + ping / 600.0f;
    Rgba c;
    if (ping <= 50) c = (Rgba){0x00, 0xFF, 0x00};
    else if (ping <= 100) c = (Rgba){0x22, 0xFF, 0x00};
    else if (ping <= 150) c = (Rgba){0x54, 0xC7, 0x00};
    else if (ping <= 200) c = (Rgba){0x76, 0xA7, 0x00};
    else if (ping <= 250) c = (Rgba){0x93, 0x88, 0x00};
    else if (ping <= 300) c = (Rgba){0xA1, 0x77, 0x00};
    else if (ping <= 350) c = (Rgba){0xCC, 0x48, 0x00};
    else c = (Rgba){0xFF, 0x00, 0x00};
    draw_sprite_scaled(&hud->dot, x, y, sx, sy, with_alpha(c, ping > 255 ? 255 : ping));
}

// The kill console's weapon icons, beside the lines drawn later with the texts.
static void draw_kill_console_icons(const Interface *hud, const Frame *f, const HudData *d, Rect viewport)
{
    int alpha = 255;
    if (viewport.width < 1024) {
        if (d->frags_menu) alpha = 50;
        if (d->chat_type != HUD_CHAT_NONE) alpha = 150;
    }
    float l2 = 0;
    for (int j = 0; j < d->kill_count; j++) {
        const HudKillLine *k = &d->kills[j];
        if (!k->text[0] || !k->has_icon) continue;
        l2 += KILLCONSOLE_SEPARATE_HEIGHT;
        float x = 605 * f->iscale_x;
        float y = (float)j * (FONT_WEAPONMENUSIZE + 2) + 59 + l2;
        draw_sprite_scaled(&hud->guns[k->weapon], x, y, 0.8f, 0.8f, (Rgba){255, 255, 255, (uint8_t)alpha});
    }
}

// The frags menu's box: the original's "Background For Frags Stats". Returns where its
// bottom is, for the texts.
static float draw_frags_background(const Interface *hud, const Frame *f, const HudData *d, Rect viewport)
{
    float x = 25 + f->fragx, y = 5;
    int groups = 0;
    for (Team t = TEAM_ALPHA; t <= TEAM_DELTA; t++) groups += count_team(d, t) > 0;
    groups += count_team(d, TEAM_NONE) > 0;
    int spectators = count_spectators(d);
    groups += spectators > 0;
    int players = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) players += d->players[i].active;

    float bottom = 70 + (float)((players + 1) * FRAGSMENU_PLAYER_HEIGHT) + (float)groups * 15;
    float sx = 590 / BACKGROUND_WIDTH, sy = bottom / BACKGROUND_WIDTH;
    draw_sprite_scaled(&hud->back, x, y, sx, sy, (Rgba){255, 255, 255, (uint8_t)(STATUS_TRANSPARENCY * 0.56f)});

    if (sy * BACKGROUND_WIDTH > GAME_HEIGHT - 80) { // more than fits: the scroll hint blinks
        float bx = 580 + f->fragx, by = GAME_HEIGHT / 2;
        draw_sprite(&hud->scroll, bx, by, 0, (Rgba){255, 255, 255, (uint8_t)fabsf(sinf(5.1f * (float)d->time) * 255)});
    }

    // Each row's marks, under its text: the dead dot, the small dot by me, the flag
    // carried, the bot, and the line's quality from red to green. The rows sit where
    // draw_frags_texts puts them: by rank, and in a team game grouped alpha, bravo,
    // charlie, delta, none, spectators, each group 20 under the last.
    int ranked[MAX_PLAYERS];
    int n = rank_players(d, ranked);
    const int order[6] = {TEAM_ALPHA, TEAM_BRAVO, TEAM_CHARLIE, TEAM_DELTA, TEAM_NONE, 5};
    int step[6] = {0}, before[6] = {0}, ids[6] = {0};
    int next_step = 0, above = 0;
    for (int g = 0; g < 6; g++) {
        int slot = order[g];
        int count = slot == 5 ? spectators : count_team(d, (Team)slot);
        before[slot] = above;
        if (count <= 0) continue;
        step[slot] = next_step;
        next_step += 20;
        above += count;
    }
    Rgba mark = {255, 255, 255, STATUS_TRANSPARENCY};
    for (int j = 0; j < n; j++) {
        const HudPlayer *p = &d->players[ranked[j]];
        float row = 61 + (float)((j + 1) * FRAGSMENU_PLAYER_HEIGHT);
        if (d->team_game) {
            int k = p->spectator ? 5 : (int)p->team;
            row = 70 + (float)(ids[k] * FRAGSMENU_PLAYER_HEIGHT + step[k] + before[k] * FRAGSMENU_PLAYER_HEIGHT);
            ids[k]++;
        }
        if (p->dead && !p->spectator) draw_sprite(&hud->deaddot, pixel_align(f, 32 + f->fragx), pixel_align(f, row + 1), 0, mark);
        if (ranked[j] == d->me) draw_sprite(&hud->smalldot, pixel_align(f, 31 + f->fragx), pixel_align(f, row + 1), 0, mark);
        if (p->flags > 0 && !p->spectator) draw_sprite(&hud->flag, pixel_align(f, f->fragx + 337), pixel_align(f, row - 1), 0, mark);
        if (p->bot) draw_sprite(&hud->bot, pixel_align(f, f->fragx + 534), pixel_align(f, row), 0, mark);
        // the original colours it by its ConnectionQuality, which no packet carries here;
        // the ping stands in: whole up to 50 ms, gone by 350
        int quality = clampi(100 - (p->ping - 50) / 3, 0, 100);
        Rgba line = {(uint8_t)(255 * (100 - quality) / 100), (uint8_t)(255 * quality / 100), 0, STATUS_TRANSPARENCY};
        draw_sprite(&hud->connection, pixel_align(f, f->fragx + 520), pixel_align(f, row + 2), 0, line);
    }
    return bottom;
}

// The team box with the flags away from home: the original's "Team Box".
static void draw_team_box(const Interface *hud, const Frame *f, const HudData *d)
{
    if (!d->team_game) return;
    float x = INT.team_box_x * f->iscale_x, y = INT.team_box_y;
    draw_sprite_scaled(&hud->back, x, y, 57 / BACKGROUND_WIDTH, 88 / BACKGROUND_WIDTH,
                       (Rgba){255, 255, 255, (uint8_t)(INT.alpha * 0.56f)});

    if (!d->flags_known) return;
    if (d->mode == HUD_MODE_CTF) {
        x = pixel_align(f, (INT.team_box_x + 4) * f->iscale_x);
        y = pixel_align(f, INT.team_box_y + 5);
        if (!d->flag_in_base[TEAM_ALPHA]) draw_sprite(&hud->noflag, x, y, 0, (Rgba){255, 0, 0, INT.alpha});
        x = pixel_align(f, x + 31);
        if (!d->flag_in_base[TEAM_BRAVO]) draw_sprite(&hud->noflag, x, y, 0, (Rgba){0, 0, 255, INT.alpha});
    } else if (d->mode == HUD_MODE_INF && !d->flag_in_base[TEAM_BRAVO]) {
        x = pixel_align(f, (INT.team_box_x + 19) * f->iscale_x);
        y = pixel_align(f, INT.team_box_y + 3);
        draw_sprite(&hud->noflag, x, y, 0, (Rgba){0, 0, 255, INT.alpha});
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

// The numbers and names: the original's RenderPlayerInterfaceTexts.
static void draw_player_texts(const Frame *f, const HudData *d, const RenderSoldier *me, const Context *ctx)
{
    char str[64];
    if (!me->dead) {
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

    // my place, my kills against the leader's, the kill limit
    int ranked[MAX_PLAYERS];
    int n = rank_players(d, ranked);
    int spectators = count_spectators(d);
    int playing = n - spectators;
    int pos = 0;
    for (int i = 0; i < n; i++) {
        if (ranked[i] == d->me) pos = i + 1;
    }
    text_style(FONT_SMALL);
    float x = INT.status_x * f->iscale_x, y = INT.status_y;
    if (pos > 0 && pos <= playing) {
        snprintf(str, sizeof(str), "%d/%d", pos, playing);
        text_color((Rgba){88, 255, 90, INT.alpha});
        text_draw(str, x, y);
    }
    const HudPlayer *mine = &d->players[d->me];
    text_color((Rgba){255, 55, 50, INT.alpha});
    if (pos == 1 && playing > 1) {
        int lead = mine->kills - d->players[ranked[1]].kills;
        snprintf(str, sizeof(str), "%d (%s%d)", mine->kills, lead > 0 ? "+" : "", lead);
    } else {
        int behind = n > 0 ? mine->kills - d->players[ranked[0]].kills : 0;
        snprintf(str, sizeof(str), "%d (%d)", mine->kills, behind);
    }
    text_draw(str, x, y + 10);
    text_color((Rgba){114, 120, 255, INT.alpha});
    snprintf(str, sizeof(str), "%d", d->kill_limit);
    text_draw(str, x, y + 20);
}

// The teams' scores in the team box, best first.
static void draw_team_scores(const Frame *f, const HudData *d)
{
    if (!d->team_game) return;
    text_style(FONT_MENU);
    float x = INT.team_box_x * f->iscale_x + 2, y = INT.team_box_y + 25;
    int count = 2, spacing = 40;
    if (d->mode == HUD_MODE_TEAMMATCH) {
        count = 4;
        spacing = 24;
        y -= 25;
    }
    Team order[4] = {TEAM_ALPHA, TEAM_BRAVO, TEAM_CHARLIE, TEAM_DELTA};
    for (int i = 1; i < count; i++) { // by kills, the original's SortedTeamScore
        Team t = order[i];
        int j = i - 1;
        while (j >= 0 && d->team_kills[order[j]] < d->team_kills[t]) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = t;
    }
    for (int i = 0; i < count; i++) {
        char str[16];
        snprintf(str, sizeof(str), "%d", d->team_kills[order[i]]);
        text_color(team_message_color(order[i]));
        text_draw(str, x, y + (float)(spacing * i));
    }
}

// The scoreboard: the original's RenderFragsMenuTexts.
static void draw_frags_texts(const Frame *f, const HudData *d, float menu_bottom)
{
    float x = f->fragx, y = 0;
    char str[96];
    int spectators = count_spectators(d);
    int ranked[MAX_PLAYERS];
    int n = rank_players(d, ranked);

    // where each group's rows start; the order is alpha, bravo, charlie, delta, none,
    // then the spectators (index 5)
    Vec2 lines[6] = {0};
    const int groups[6] = {TEAM_ALPHA, TEAM_BRAVO, TEAM_CHARLIE, TEAM_DELTA, TEAM_NONE, 5};
    int counts[6];
    for (int g = 0; g < 6; g++) counts[g] = g == 5 ? spectators : count_team(d, (Team)groups[g]);
    if (d->team_game) {
        int step = 0, k = 0;
        for (int g = 0; g < 6; g++) {
            if (counts[g] <= 0) continue;
            int slot = groups[g];
            lines[slot] = vec2(x + 35, y + 50 + (float)step + (float)(k * FRAGSMENU_PLAYER_HEIGHT));
            step += 20;
            k += counts[g];
            Rgba c = slot == 5 ? (Rgba){129, 52, 118, 255} : team_message_color((Team)slot);
            draw_line(f, lines[slot].x, lines[slot].y + 15, 565, c);
        }
    } else {
        lines[0].y = y + 40 + FRAGSMENU_PLAYER_HEIGHT;
        lines[5].y = y + 40 + (float)((n - spectators + 1) * FRAGSMENU_PLAYER_HEIGHT);
    }

    // columns
    const char *points = d->mode == HUD_MODE_DEATHMATCH || d->mode == HUD_MODE_TEAMMATCH ? "Kills:" : "Points:";
    text_style(FONT_MENU);
    text_color((Rgba){255, 255, 230, 255});
    text_draw(points, x + 280 - (strlen(points) > 7 ? 80 : 0), y + 40);
    text_draw("Deaths:", x + 390, y + 40);
    text_draw("Ping:", x + 530, y + 40);

    // the server
    text_style(FONT_SMALL_BOLD);
    text_color((Rgba){233, 180, 12, 255});
    text_draw(d->hostname, x + 30, y + 15);

    snprintf(str, sizeof(str), "Time %02d:%02d", d->time_left_min, d->time_left_sec);
    text_style(FONT_SMALL);
    text_color((Rgba){170, 160, 200, 230});
    text_draw(str, x + 485, y + 15);
    text_color((Rgba){200, 150, 0, 255});
    text_draw(d->info, x + 30, y + 30);

    // how many play
    text_color((Rgba){200, 190, 180, 240});
    text_draw("Players", x + 330, y + 15);
    if (d->team_game) {
        int teams = d->mode == HUD_MODE_TEAMMATCH ? 4 : 2;
        const Rgba colors[4] = {{233, 0, 0, 240}, {0, 0, 233, 240}, {233, 233, 0, 240}, {0, 233, 0, 240}};
        for (int i = 0; i < teams; i++) {
            text_color(colors[i]);
            snprintf(str, sizeof(str), "%d", count_team(d, (Team)(TEAM_ALPHA + i)));
            text_draw(str, x + 440 + (float)(20 * (i / 2)), y + 10 + (float)(10 * (i % 2)));
        }
    } else {
        snprintf(str, sizeof(str), "%d", n);
        text_draw(str, x + 450, y + 15);
    }

    // the players, each in its group
    int ids[6] = {0}, team_total[6] = {0};
    for (int j = 0; j < n; j++) {
        const HudPlayer *p = &d->players[ranked[j]];
        int k = p->spectator ? 5 : d->team_game ? (int)p->team : 0;
        text_color(k == 5 ? (Rgba){220, 50, 200, 113} : with_alpha(p->shirt, 255));
        float py = lines[k].y + 20 + (float)(FRAGSMENU_PLAYER_HEIGHT * ids[k]);
        text_draw(p->name, x + 44, py);
        snprintf(str, sizeof(str), "%d", p->kills);
        text_draw(str, x + 284, py);
        snprintf(str, sizeof(str), "%d", p->deaths);
        text_draw(str, x + 394, py);
        if (p->flags > 0) {
            snprintf(str, sizeof(str), "x%d", p->flags);
            text_draw(str, x + 348, py);
        }
        if (!p->bot) {
            snprintf(str, sizeof(str), "%d", p->ping);
            text_draw(str, x + 534, py);
        }
        if (d->chat_type != HUD_CHAT_NONE && d->chat_text[0] == '/') { // numbers, for the commands
            text_color((Rgba){245, 255, 230, 155});
            snprintf(str, sizeof(str), "%d", ranked[j] + 1);
            text_draw(str, x + 20 - text_width(str), py);
        }
        ids[k]++;
        team_total[k] += p->kills;
    }

    // the groups' captions
    if (d->team_game) {
        const char *captions[6] = {"Player", "Alpha", "Bravo", "Charlie", "Delta", "Spectator"};
        const Rgba totals[5] = {{0}, {0xD2, 0x0F, 0x05, 0xDD}, {0x15, 0x1F, 0xD9, 0xDD}, {0xD2, 0xD2, 0x05, 0xDD}, {0x05, 0xD2, 0x05, 0xDD}};
        for (int g = 0; g < 6; g++) {
            if (counts[g] <= 0) continue;
            int slot = groups[g];
            text_color(slot == 5 ? (Rgba){129, 52, 118, 255} : team_message_color((Team)slot));
            text_style(FONT_SMALL_BOLD);
            text_draw(captions[slot], lines[slot].x, lines[slot].y);
            if (g < 4) {
                text_color(totals[slot]);
                text_style(FONT_SMALL);
                snprintf(str, sizeof(str), "%d", team_total[slot]);
                text_draw(str, x + 284, lines[slot].y + 3);
            }
        }
    }
    (void)menu_bottom; // the demo's name goes above it, once demos are recorded
}

void interface_draw_box(const Interface *hud, float x, float y, float w, float h, Rgba color)
{
    draw_sprite_scaled(&hud->back, x, y, w / BACKGROUND_WIDTH, h / BACKGROUND_WIDTH, color);
}

void interface_draw_pointer(const Interface *hud, Vec2 at)
{
    draw_sprite(&hud->menucursor, at.x, at.y, 0, (Rgba){255, 255, 255, STATUS_TRANSPARENCY});
}

// The console in the corner, smaller when a line runs past the window.
static void draw_console(const Frame *f, const HudData *d, bool dim)
{
    text_style(FONT_SMALL);
    float line = FONT_CONSOLELINEHEIGHT * f->pixel * text_style_size(FONT_SMALL);
    int alpha = dim ? 60 : 255;
    bool tiny = false;
    for (int i = 0; i < d->console_count; i++) {
        const HudLine *l = &d->console[i];
        if (!l->text[0]) continue;
        text_color(with_alpha(l->color, alpha));
        if ((text_width(l->text) > f->game_width - 10) != tiny) {
            tiny = !tiny;
            text_style(tiny ? FONT_SMALLEST : FONT_SMALL);
        }
        text_draw(l->text, 5, 1 + (float)i * line);
    }
}

// The kill console's lines, right-aligned, smaller when long.
static void draw_kill_console(const Frame *f, const HudData *d, Rect viewport)
{
    int alpha = 245;
    if (viewport.width < 1024) {
        if (d->frags_menu) alpha = 80;
        if (d->chat_type != HUD_CHAT_NONE) alpha = 180;
    }
    text_style(FONT_WEAPONS_MENU);
    bool tiny = false;
    float dy = 0;
    for (int i = 0; i < d->kill_count; i++) {
        const HudKillLine *k = &d->kills[i];
        if (!k->text[0]) continue;
        if (k->has_icon) dy += KILLCONSOLE_SEPARATE_HEIGHT;
        if ((strlen(k->text) > 14) != tiny) {
            tiny = !tiny;
            text_style(tiny ? FONT_SMALLEST : FONT_WEAPONS_MENU);
        }
        float x = 595 * f->iscale_x - text_width(k->text);
        float y = 60 + (float)i * (FONT_WEAPONMENUSIZE + 2) + dy;
        text_color(with_alpha(k->color, alpha));
        text_draw(k->text, x, y);
    }
}

// "Respawn in..." and the survival round's state, in a box at the top.
static void draw_respawn_texts(const Interface *hud, const Frame *f, const HudData *d, const RenderSoldier *me)
{
    const HudPlayer *mine = &d->players[d->me];
    if (mine->spectator) return; // a spectator is dead only in name
    if (mine->spectator) {
        if (d->survival_round_over) {
            text_style(FONT_MENU);
            text_color((Rgba){115, 255, 100, 255});
            text_draw("End of round...", 240 * f->iscale_x, 400);
        }
        return;
    }
    if (me->dead || d->survival_round_over) {
        draw_sprite_scaled(&hud->back, 180 * f->iscale_x, 1, 300 / BACKGROUND_WIDTH, 22 / BACKGROUND_WIDTH,
                           (Rgba){255, 255, 255, (uint8_t)(STATUS_TRANSPARENCY * 0.56f)});
    }
    char str[96] = "";
    if (!d->survival && d->respawn_counter > 0) {
        snprintf(str, sizeof(str), "Respawn in... %.1f", d->respawn_counter / TICK_RATE_HUD);
        text_color((Rgba){255, 65, 55, 255});
    } else if (d->survival && me->dead && !d->survival_round_over) {
        text_color((Rgba){115, 255, 100, 255});
        if (!d->team_game) snprintf(str, sizeof(str), "%d players left", d->alive);
        else snprintf(str, sizeof(str), "%d team players left", d->team_alive[mine->team]);
    } else if (d->survival_round_over) {
        if (me->dead) {
            snprintf(str, sizeof(str), "End of round...%.1f", d->respawn_counter / TICK_RATE_HUD);
            text_color((Rgba){115, 255, 100, 255});
        } else {
            snprintf(str, sizeof(str), "You have survived");
            text_color((Rgba){155, 245, 100, 255});
        }
    }
    text_style(FONT_MENU);
    text_draw(str, 200 * f->iscale_x, 4);
}

// What I am typing, with its caret blinking.
static void draw_chat_input(const Frame *f, const HudData *d)
{
    const char *prefix;
    Rgba color;
    switch (d->chat_type) {
    case HUD_CHAT_PUBLIC: prefix = "Say:", color = COLOR_CHAT; break;
    case HUD_CHAT_TEAM: prefix = "Team Say:", color = COLOR_TEAMCHAT; break;
    case HUD_CHAT_COMMAND: prefix = "Cmd: ", color = COLOR_ENTER; break;
    default: return;
    }
    text_style(FONT_SMALL);
    text_color(color);

    char str[HUD_TEXT + 16];
    snprintf(str, sizeof(str), "%s%s", prefix, d->chat_text);
    float width = text_width(str), height = text_height(str);
    if (width >= f->game_width - 80) text_style(FONT_SMALLEST);
    text_align(TEXT_BASELINE);
    text_draw(str, 5, 420);

    double t = d->time - d->chat_changed_at;
    if (t - floor(t) <= 0.5) {
        char before[HUD_TEXT + 16];
        size_t n = strlen(prefix) + (size_t)clampi(d->chat_cursor, 0, (int)strlen(d->chat_text));
        snprintf(before, sizeof(before), "%.*s", (int)n, str);
        // a trailing space is measured by doubling it, as the original works around
        if (n > 0 && before[n - 1] == ' ') strncat(before, " ", sizeof(before) - strlen(before) - 1);
        float x = pixel_align(f, 5 + text_width(before)) + 2 * f->pixel;
        float y = pixel_align(f, 420 - height);
        draw_rect(x, y, x + f->pixel, y + pixel_align(f, 1.4f * height), (Rgba){255, 230, 170, 255});
    }
    text_align(TEXT_TOP);
}

// What each player says, over their head, and the dots while they type.
static void draw_chat_texts(const Frame *f, const HudData *d, const RenderState *state)
{
    text_style(FONT_SMALL);
    text_align(TEXT_BOTTOM);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const HudPlayer *p = &d->players[i];
        const RenderSoldier *s = &state->soldiers[i];
        if (!p->active || !s->active || (!p->typing && p->chat_delay <= 0)) continue;
        Vec2 at = world_to_interface(f, s->pose.p[12 - 1]);
        float dy = -25;
        if (p->typing) {
            const char *dots = "...";
            char str[4];
            snprintf(str, sizeof(str), "%.*s", d->tick / 30 % 3 + 1, dots);
            text_color(COLOR_ABOVECHAT);
            text_draw(str, at.x - text_width(dots) / 2, at.y + dy);
            dy -= 15;
        }
        if (p->chat_delay > 0 && strlen(p->chat) < MORECHATTEXT) {
            text_color(with_alpha(COLOR_ABOVECHAT, 9 * p->chat_delay));
            text_draw(p->chat, at.x - text_width(p->chat) / 2, at.y + dy);
        }
    }
    text_align(TEXT_TOP);
}

// A teammate's name, only when they are off the screen (or everyone's, watching).
static void draw_player_name(const Frame *f, const HudData *d, const RenderState *state, int i, bool only_offscreen)
{
    const HudPlayer *p = &d->players[i];
    const RenderSoldier *s = &state->soldiers[i];
    const RenderSoldier *me = &state->soldiers[d->me];
    float dy = (only_offscreen ? -10.0f : 5.0f) + 15.0f;
    float w = text_width(p->name), h = text_height(p->name);
    Vec2 at = world_to_interface(f, s->pose.p[7 - 1]);
    float x = at.x, y = at.y + dy;
    if (only_offscreen && x >= 0 && x <= f->game_width && y >= 0 && y <= GAME_HEIGHT) return;

    x = maxf(0, minf(f->game_width - w, x - w / 2));
    y = maxf(0, minf(GAME_HEIGHT - h, y - (only_offscreen ? 0 : h / 2)));
    float dx = maxf(fabsf(me->pose.p[7 - 1].x - s->pose.p[7 - 1].x), 1);
    float ddy = maxf(fabsf(me->pose.p[7 - 1].y - s->pose.p[7 - 1].y), 1);
    int alpha = mini(255, 50 + (int)roundf(100000.0f / (dx + ddy / 2)));
    Rgba c = p->holding_flag ? COLOR_OUTOFSCREEN_FLAG : p->dead ? COLOR_OUTOFSCREEN_DEAD : COLOR_OUTOFSCREEN;
    text_color(with_alpha(c, alpha));
    text_draw(p->name, x, y);
}

static void draw_player_names(const Frame *f, const HudData *d, const RenderState *state)
{
    text_style(FONT_WEAPONS_MENU);
    const HudPlayer *mine = &d->players[d->me];
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const HudPlayer *p = &d->players[i];
        if (!p->active || !state->soldiers[i].active || p->spectator) continue;
        if (mine->spectator) draw_player_name(f, d, state, i, false);
        else if (d->team_game && i != d->me && p->team == mine->team) draw_player_name(f, d, state, i, true);
    }
}

// The seconds of spawn protection left, over my head, in survival.
static void draw_cease_fire(const Frame *f, const HudData *d, const RenderSoldier *me)
{
    Vec2 at = world_to_interface(f, vec2_add(me->pose.p[9 - 1], vec2(-2, -15)));
    char str[16];
    snprintf(str, sizeof(str), "%d", d->cease_fire_counter / 60 + 1);
    text_style(FONT_SMALL);
    text_color(COLOR_GAME);
    text_draw(str, at.x, at.y);
}

// --- the rest of the original's RenderInterface --------------------------------------

// The bonus tint over everything: the original's "Bonus all colored".
static void draw_bonus_overlay(const Interface *hud, const Frame *f, const HudData *d)
{
    Rgba color = {0};
    switch (d->bonus) {
    case HUD_BONUS_FLAMEGOD: color = (Rgba){0xFF, 0xFF, 0x00, 62}; break;
    case HUD_BONUS_PREDATOR: color = (Rgba){0xFE, 0x00, 0xDC, 82}; break;
    case HUD_BONUS_BERSERKER: color = (Rgba){0xFE, 0x00, 0x00, 82}; break;
    default: return;
    }
    const HudSprite *s = &hud->overlay;
    if (s->tex.handle == 0) return;
    draw_sprite_scaled(s, 0, 0, f->game_width / s->width, GAME_HEIGHT / s->height, color);
}

// The line from my hand toward the cursor, when the server allows it.
static void draw_sniper_line(const Interface *hud, const Frame *f, const RenderSoldier *me, Vec2 cursor)
{
    if (hud->sight.tex.handle == 0) return;
    Vec2 hand = world_to_interface(f, me->pose.p[15 - 1]);
    float length = vec2_length(vec2_sub(cursor, hand));
    if (length >= 1200) return;
    float x = pixel_align(f, hand.x), y = pixel_align(f, hand.y);
    float angle = atan2f(cursor.y - y, cursor.x - x);
    Mat3 m = mat3_transform(x - 1, y - 1, length / 240, length / 480, 1, 1, angle);
    const HudSprite *s = &hud->sight;
    Rgba color = {255, 255, 255, (uint8_t)clampi((int)(length / 240 * 32), 0, 255)};
    Vec2 p0 = mat3_apply(m, vec2(0, 0)), p1 = mat3_apply(m, vec2(s->width, 0));
    Vec2 p2 = mat3_apply(m, vec2(s->width, s->height)), p3 = mat3_apply(m, vec2(0, s->height));
    GfxVertex v[4] = {gfx_vertex(p0.x, p0.y, 0, 0, color), gfx_vertex(p1.x, p1.y, 1, 0, color),
                      gfx_vertex(p2.x, p2.y, 1, 1, color), gfx_vertex(p3.x, p3.y, 0, 1, color)};
    gfx_draw_quad(s->tex, v);
}

#define MINIMAP_X 285 // ui_minimap_posx
#define MINIMAP_Y 5   // ui_minimap_posy
#define MINIMAP_TRANSPARENCY 230

// A point on the minimap, centred on a dot of `scale`: the original's ToMinimap.
static Vec2 to_minimap(const Interface *hud, const Frame *f, const MapView *mv, Vec2 world, float scale)
{
    Vec2 p = map_view_to_minimap(mv, world);
    return (Vec2){pixel_align(f, MINIMAP_X + p.x - scale * hud->smalldot.width / 2),
                  pixel_align(f, MINIMAP_Y + p.y - scale * hud->smalldot.height / 2)};
}

static void draw_minimap_dot(const Interface *hud, Vec2 at, float scale, Rgba color)
{
    draw_sprite_scaled(&hud->smalldot, at.x, at.y, scale, scale, color);
}

// The map in the corner with everyone on my side on it, and the view's box when I
// only watch: the original's "Minimap".
static void draw_minimap(const Interface *hud, const Frame *f, const HudData *d, const RenderState *state,
                         const MapView *mv)
{
    const Minimap *m = &mv->minimap;
    if (m->tex.handle == 0) return;
    float x = pixel_align(f, MINIMAP_X), y = pixel_align(f, MINIMAP_Y);
    Rgba c = {255, 255, 255, (uint8_t)(STATUS_TRANSPARENCY * 0.85f)};
    GfxVertex v[4] = {
        gfx_vertex(x, y, 0, 0, c),
        gfx_vertex(x + m->width, y, m->u1, 0, c),
        gfx_vertex(x + m->width, y + m->height, m->u1, m->v1, c),
        gfx_vertex(x, y + m->height, 0, m->v1, c),
    };
    gfx_draw_quad(m->tex, v);

    int alpha = MINIMAP_TRANSPARENCY;
    const HudPlayer *mine = &d->players[d->me];
    for (int j = 0; j < MAX_PLAYERS; j++) {
        const HudPlayer *p = &d->players[j];
        const RenderSoldier *s = &state->soldiers[j];
        if (!p->active || !s->active || p->spectator) continue;
        if (!mine->spectator && p->team != mine->team) continue;
        Vec2 head = s->pose.p[7 - 1];
        if (p->holding_flag) {
            draw_minimap_dot(hud, to_minimap(hud, f, mv, head, 1), 1, with_alpha((Rgba){0xFF, 0xFF, 0x00}, alpha));
        } else if (j == d->me || (mine->spectator && j == d->camera_follow)) {
            draw_minimap_dot(hud, to_minimap(hud, f, mv, head, 0.8f), 0.8f, with_alpha(RGBA_WHITE, alpha));
        } else {
            Rgba dot = {0};
            if (!p->dead) {
                switch (p->team) {
                case TEAM_ALPHA: dot = (Rgba){0xFF, 0x00, 0x00}; break;
                case TEAM_BRAVO: dot = (Rgba){0x13, 0x13, 0xFF}; break;
                case TEAM_CHARLIE: dot = (Rgba){0xFF, 0xFF, 0x00}; break;
                case TEAM_DELTA: dot = (Rgba){0x00, 0xFF, 0x00}; break;
                default: break;
                }
            }
            draw_minimap_dot(hud, to_minimap(hud, f, mv, head, 0.65f), 0.65f, with_alpha(dot, alpha));
            if (p->chat_delay > 0) { // a word over the dot
                Vec2 above = vec2_add(head, vec2(0, -40));
                draw_minimap_dot(hud, to_minimap(hud, f, mv, above, 0.5f), 0.5f, with_alpha(RGBA_WHITE, alpha));
            }
        }
    }

    if (mine->spectator) { // the view's box: the original's RenderMinimapSquare
        Vec2 view = camera_view_size(f->camera);
        Vec2 start = map_view_to_minimap(mv, vec2_sub(f->camera->pos, vec2_scale(view, 0.5f)));
        Vec2 end = map_view_to_minimap(mv, vec2_add(f->camera->pos, vec2_scale(view, 0.5f)));
        float min_x = x, min_y = y, max_x = x + m->width, max_y = y + m->height;
        float sx = maxf(min_x, pixel_align(f, MINIMAP_X + start.x));
        float sy = maxf(min_y, pixel_align(f, MINIMAP_Y + start.y));
        float ex = minf(max_x, pixel_align(f, MINIMAP_X + end.x));
        float ey = minf(max_y, pixel_align(f, MINIMAP_Y + end.y));
        Rgba box = {255, 255, 255, 127};
        float t = 0.5f; // the original's DrawBox, half a unit thick
        draw_rect(sx - t, sy - t, ex + t, sy, box);
        draw_rect(ex, sy, ex + t, ey, box);
        draw_rect(sx - t, ey, ex + t, ey + t, box);
        draw_rect(sx - t, sy, sx, ey, box);
    }
}

// The weapon stats (F2): the box, the icons and the numbers.
static void draw_weapon_stats(const Interface *hud, const Frame *f, const HudData *d)
{
    float x = f->fragx, y = 0;
    int n = 0;
    for (int i = 0; i < d->weapon_stat_count; i++) n += d->weapon_stats[i].shots > 0;

    if (!d->frags_menu) {
        draw_sprite_scaled(&hud->back, 25 + x, 5 + y, 590 / BACKGROUND_WIDTH, (float)(n * 20 + 85) / BACKGROUND_WIDTH,
                           (Rgba){255, 255, 255, (uint8_t)(STATUS_TRANSPARENCY * 0.56f)});
        int z = 0;
        for (int i = 0; i < d->weapon_stat_count; i++) {
            if (d->weapon_stats[i].shots <= 0) continue;
            z++;
            draw_sprite(&hud->guns[d->weapon_stats[i].weapon], 30 + x, (float)(z * 20 + 50) + y, 0, RGBA_WHITE);
        }
    }

    char str[64];
    text_style(FONT_SMALL);
    text_color((Rgba){170, 160, 200, 230});
    text_draw("% = Accuracy", x + 465, y + 15);
    text_draw("HS = Headshots", x + 465, y + 25);
    text_style(FONT_MENU);
    text_color((Rgba){255, 255, 230, 255});
    text_draw("Weapon:", x + 70, y + 40);
    text_draw(" %", x + 240, y + 40);
    text_draw("Shots:", x + 290, y + 40);
    text_draw("Hits:", x + 390, y + 40);
    text_draw("Kills (HS):", x + 470, y + 40);
    text_style(FONT_SMALL);
    text_color(RGBA_WHITE);
    int j = 0;
    for (int i = 0; i < d->weapon_stat_count; i++) {
        const HudWeaponStat *s = &d->weapon_stats[i];
        if (s->shots <= 0) continue;
        j++;
        float py = y + (float)(j * 20 + 50);
        text_draw(s->name, x + 90, py);
        snprintf(str, sizeof(str), "%d%%", (int)roundf(s->hits * 100.0f / (float)s->shots));
        text_draw(str, x + 245, py);
        snprintf(str, sizeof(str), "%d", s->shots);
        text_draw(str, x + 295, py);
        snprintf(str, sizeof(str), "%d", s->hits);
        text_draw(str, x + 395, py);
        snprintf(str, sizeof(str), "%d (%d)", s->kills, s->headshots);
        text_draw(str, x + 475, py);
    }
    text_color((Rgba){255, 255, 230, 100});
    text_draw("(Updated every 10 seconds)", x + 230, y + (float)((j + 1) * 20 + 50));
}

// The boxes behind the team and weapons menus, and the weapons' pictures.
static void draw_menu_boxes(const Interface *hud, const Frame *f, const HudData *d, const GameMenus *menus)
{
    const Rgba box = {255, 255, 255, (uint8_t)(STATUS_TRANSPARENCY * 0.56f)};
    if (menus->menus[MENU_TEAM].active) {
        draw_sprite_scaled(&hud->back, 45, 140, 262 / BACKGROUND_WIDTH, 250 / BACKGROUND_WIDTH, box);
    }
    if (menus->menus[MENU_LIMBO].active) {
        float sx = 252 / BACKGROUND_WIDTH;
        draw_sprite_scaled(&hud->back, 45, 140, sx, 210 / BACKGROUND_WIDTH, box);
        draw_sprite_scaled(&hud->back, 45, 350, sx, 80 / BACKGROUND_WIDTH, box);

        float x = pixel_align(f, 55), y = 157;
        for (int k = 1; k <= 10; k++) { // the primaries
            if (!menus->weapons_active[k]) continue;
            const HudSprite *s = &hud->guns[k];
            float dy = maxf(0, 18 - s->height) / 2;
            draw_sprite(s, x, pixel_align(f, y + (float)(18 * (k - 1)) + dy), 0,
                        (Rgba){255, 255, 255, STATUS_TRANSPARENCY});
        }
        for (int k = 11; k <= 14; k++) { // the secondaries, the chosen one bright
            if (!menus->weapons_active[k]) continue;
            const HudSprite *s = &hud->guns[k];
            float dy = maxf(0, 18 - s->height) / 2;
            int alpha = d->selected_secondary == (WeaponId)k ? STATUS_TRANSPARENCY : STATUS_TRANSPARENCY / 2;
            draw_sprite(s, x, pixel_align(f, y + (float)(k * 18) + dy), 0, (Rgba){255, 255, 255, (uint8_t)alpha});
        }
    }
}

// The vote's box and its texts.
static void draw_vote(const Interface *hud, const Frame *f, const HudData *d)
{
    if (d->vote != HUD_VOTE_NONE) {
        float x = 45 * f->iscale_x, y = 400;
        draw_sprite_scaled(&hud->back, x, y, 252 / BACKGROUND_WIDTH, 40 / BACKGROUND_WIDTH,
                           (Rgba){255, 255, 255, (uint8_t)(STATUS_TRANSPARENCY * 0.36f)});
        char str[HUD_TEXT + 16];
        text_style(FONT_WEAPONS_MENU);
        text_color((Rgba){254, 104, 104, 225});
        text_draw(d->vote == HUD_VOTE_KICK ? "Kick" : "Map", x + 30, y);
        text_color((Rgba){244, 244, 244, 225});
        text_draw(d->vote_target, x + 65, y);
        text_color((Rgba){224, 218, 244, 205});
        snprintf(str, sizeof(str), "Voter: %s", d->vote_starter);
        text_draw(str, x + 10, y + 11);
        snprintf(str, sizeof(str), "Reason:%s", d->vote_reason);
        text_draw(str, x + 10, y + 20);
        text_color((Rgba){234, 234, 114, 205});
        text_draw("F12 - Yes   F11 - No", x + 50, y + 31);
    }
    if (d->vote_reason_typing) {
        text_style(FONT_SMALL);
        text_color((Rgba){254, 124, 124, 255});
        text_draw("Type reason for vote:", 5, 390);
    }
}

// The radio menu's two columns.
static void draw_radio_menu(const Interface *hud, const HudData *d)
{
    if (d->mode != HUD_MODE_CTF && d->mode != HUD_MODE_INF && d->mode != HUD_MODE_HTF) return;
    const Rgba box = {255, 255, 255, (uint8_t)(STATUS_TRANSPARENCY * 0.56f)};
    float sx = 180 / BACKGROUND_WIDTH, sy = 80 / BACKGROUND_WIDTH;
    draw_sprite_scaled(&hud->back, 5, 250, sx, sy, box);
    if (d->radio_state) draw_sprite_scaled(&hud->back, 185, 250, sx, sy, box);

    int alpha = d->frags_menu || d->stats_menu ? 80 : 230;
    text_style(FONT_MENU);
    text_color((Rgba){255, 255, 255, (uint8_t)alpha});
    text_draw("Radio:", 10, 252);
    text_style(FONT_SMALL);
    const Rgba plain = {200, 200, 200, (uint8_t)alpha}, chosen = {210, 210, 5, (uint8_t)alpha};
    char str[HUD_NAME + 8];
    for (int i = 0; i < HUD_RADIO_LINES; i++) {
        text_color(d->radio_state == i + 1 ? chosen : plain);
        snprintf(str, sizeof(str), "%d: %s", i + 1, d->radio_first[i]);
        text_draw(str, 10, 270 + (float)(12 * i));
    }
    if (d->radio_state) {
        text_color(plain);
        for (int i = 0; i < HUD_RADIO_LINES; i++) {
            snprintf(str, sizeof(str), "%d: %s", i + 1, d->radio_second[i]);
            text_draw(str, 190, 270 + (float)(12 * i));
        }
    }
}

static bool hovered(const GameMenus *menus, MenuId id, int button)
{
    return menus->hovered_menu == (int)id && menus->hovered_button == button;
}

// The weapons menu's captions and the tips under the cursor.
static void draw_weapon_menu_texts(const HudData *d, const GameMenus *menus)
{
    const GameMenu *menu = &menus->menus[MENU_LIMBO];
    text_style(FONT_SMALL);
    text_shadow(1, 1, (Rgba){0, 0, 0, 255});
    text_color((Rgba){234, 234, 234, 255});
    text_draw("Primary Weapon:", 65, 142);
    text_align(TEXT_BASELINE);
    text_color((Rgba){214, 214, 214, 255});
    text_draw("Secondary Weapon:", 65, 349);
    text_align(TEXT_TOP);

    int cursor_on = 0;
    for (int i = 0; i < menu->button_count; i++) {
        const MenuButton *b = &menu->buttons[i];
        if (!b->active) continue;
        bool hover = hovered(menus, MENU_LIMBO, i);
        if (hover) cursor_on = i;
        float x = b->x1 + 85, y = b->y1 + (b->y2 - b->y1) / 2 - 2;
        text_color((Rgba){255, 255, 255, 230});
        if ((WeaponId)(i + 1) == d->selected_weapon || (i >= 10 && (WeaponId)(i + 1) == d->selected_secondary)) {
            text_color(hover ? (Rgba){85, 105, 55, 230} : (Rgba){55, 165, 55, 230});
        } else if (hover) {
            x += 1;
            y -= 1;
        }
        text_draw(b->caption, x, y);
    }

    // the tips for the first few runs (cl_runs < 4), for the weapons that need them
    text_style(FONT_WEAPONS_MENU);
    const MenuButton *b = &menu->buttons[cursor_on];
    float tip_y = b->y1 + (b->y2 - b->y1) / 2;
    const char *tip = NULL;
    switch (cursor_on + 1) {
    case 8: tip = "Hold fire to shoot, inaccurate while moving"; break;
    case 12: tip = "Can be thrown by holding throw weapon button"; break;
    case 14: tip = "Hold fire to shoot, while crouching or prone"; break;
    default: break;
    }
    if (tip && menus->noob_show) {
        text_color((Rgba){225, 195, 195, 250});
        text_draw(tip, b->x1 + 245, tip_y - 2);
    }
}

static void draw_esc_menu_texts(const Interface *hud, const Frame *f, const GameMenus *menus)
{
    const GameMenu *menu = &menus->menus[MENU_ESC];
    float sx = menu->w / BACKGROUND_WIDTH, sy = menu->h / BACKGROUND_WIDTH;
    float dx = (f->game_width / 2 - menu->w / 2) - menu->x, dy = (GAME_HEIGHT / 2 - menu->h / 2) - menu->y;
    draw_sprite_scaled(&hud->back, menu->x + dx, menu->y + dy, sx, sy,
                       (Rgba){255, 255, 255, (uint8_t)(STATUS_TRANSPARENCY * 0.56f)});

    text_style(FONT_SMALL);
    text_shadow(1, 1, (Rgba){0, 0, 0, 255});
    text_color((Rgba){250, 245, 255, 240});
    text_draw("ESC - return to game", menu->x + dx + 20, menu->y + menu->h + dy - 45);
    text_color((Rgba){230, 235, 255, 190});
    text_align(TEXT_BOTTOM);
    const char *version = "bettersoldat 0.1.0";
    text_draw(version, menu->x + menu->w + dx - 2 - text_width(version), menu->y + menu->h + dy);
    text_align(TEXT_TOP);

    text_style(FONT_MENU);
    text_color((Rgba){255, 255, 255, 250});
    for (int i = 0; i < menu->button_count; i++) {
        const MenuButton *b = &menu->buttons[i];
        if (!b->active) continue;
        int h = hovered(menus, MENU_ESC, i);
        float x = b->x1 + dx + (float)h + 10;
        float y = b->y1 + dy - (float)h + (b->y2 - b->y1) / 2 - text_height(b->caption) / 2;
        text_draw(b->caption, x, y);
    }
}

static void draw_team_menu_texts(const HudData *d, const GameMenus *menus)
{
    const GameMenu *menu = &menus->menus[MENU_TEAM];
    uint8_t alpha = d->frags_menu || d->stats_menu ? 80 : 255;
    const Rgba colors[6][2] = {
        {{255, 255, 255, alpha}, {255, 255, 255, 250}}, {{210, 15, 5, alpha}, {210, 15, 5, 250}},
        {{5, 15, 205, alpha}, {5, 15, 205, 250}},       {{210, 210, 5, alpha}, {210, 210, 5, 250}},
        {{5, 210, 5, alpha}, {5, 210, 5, 250}},         {{210, 210, 105, alpha}, {210, 210, 105, 250}},
    };
    text_style(FONT_MENU);
    text_shadow(1, 1, (Rgba){0, 0, 0, 255});
    text_color((Rgba){234, 234, 234, alpha});
    text_draw("Select Team:", 55, 165);

    for (int i = 0; i < menu->button_count; i++) {
        const MenuButton *b = &menu->buttons[i];
        if (!b->active) continue;
        int h = hovered(menus, MENU_TEAM, i);
        text_shadow(1, 1, i == 2 ? (Rgba){0x33, 0x33, 0x33, 255} : (Rgba){0, 0, 0, 255});
        text_color(colors[i][h]);
        float x = b->x1 + 10 + (float)h;
        float y = b->y1 - (float)h + (b->y2 - b->y1) / 2 - text_height(b->caption) / 2;
        text_draw(b->caption, x, y);
        if (i > 0 && i < 5) {
            char str[16];
            snprintf(str, sizeof(str), "(%d)", count_team(d, (Team)i));
            text_draw(str, 269 + (float)h, y);
        }
    }
}

// The kick and map windows: a box, the name it shows, its buttons.
static void draw_window_texts(const Interface *hud, const HudData *d, const GameMenus *menus, MenuId id)
{
    const GameMenu *menu = &menus->menus[id];
    draw_sprite_scaled(&hud->back, menu->x, menu->y, menu->w / BACKGROUND_WIDTH, menu->h / BACKGROUND_WIDTH,
                       (Rgba){255, 255, 255, (uint8_t)(STATUS_TRANSPARENCY * 0.56f)});
    text_style(FONT_MENU);
    text_shadow(1, 1, (Rgba){0, 0, 0, 255});
    const MenuButton *first = &menu->buttons[0];
    if (id == MENU_KICK) {
        const HudPlayer *p = &d->players[menus->kick_index];
        if (p->active) {
            text_color(with_alpha(p->shirt, 255));
            text_draw(p->name, first->x1, first->y1 - 15);
        }
    } else {
        text_color((Rgba){135, 235, 135, 230});
        text_draw(d->map_offered, first->x1, first->y1 - 15); // the map the window offers
    }
    text_color((Rgba){255, 255, 255, 250});
    for (int i = 0; i < menu->button_count; i++) {
        const MenuButton *b = &menu->buttons[i];
        if (!b->active) continue;
        int h = hovered(menus, id, i);
        float x = b->x1 + 10 + (float)h;
        float y = b->y1 + (b->y2 - b->y1) / 2 - (float)h - text_height(b->caption) / 2;
        text_draw(b->caption, x, y);
    }
}

// The keys, shown in the escape menu for the first three runs.
static void draw_keys_help(void)
{
    text_style(FONT_SMALLEST);
    text_color((Rgba){250, 90, 95, 255});
    text_draw("Default keys (shown for first 3 game runs)", 30, 28);
    text_style(FONT_SMALL);
    const char *lines[8] = {
        "[A]/[D] move left/right", "[W]/[S]/[X] jump / crouch / lie down", "[Left Mouse] fire!",
        "[Right Mouse] jet boots", "hold [E] to toss grenade", "[R] reloads weapon",
        "[Q] change weapon / [F] throw weapon", "[T] chat / [Y] team chat",
    };
    for (int i = 1; i <= 8; i++) {
        text_color((Rgba){230, (uint8_t)(232 - 2 * i), 255, 255});
        text_draw(lines[i - 1], 30, (float)(28 + 12 * i));
    }
}

void interface_draw(const Interface *hud, const HudData *d, const GameMenus *menus, const RenderState *state,
                    const Context *ctx, const MapView *map_view, const GameCamera *camera, Vec2 cursor, Rect viewport)
{
    Frame f = {
        .game_width = GAME_HEIGHT * viewport.width / viewport.height,
        .pixel = GAME_HEIGHT / viewport.height,
        .camera = camera,
    };
    f.iscale_x = f.game_width / DEFAULT_WIDTH;
    f.fragx = floorf(f.game_width / 2 - 300) - 25;
    const RenderSoldier *me = &state->soldiers[d->me];
    const HudPlayer *mine = &d->players[d->me];
    bool esc = menus->menus[MENU_ESC].active, limbo = menus->menus[MENU_LIMBO].active;
    bool team = menus->menus[MENU_TEAM].active;

    gfx_transform(mat3_ortho(0, f.game_width, 0, GAME_HEIGHT));
    text_pixel_ratio(vec2(f.pixel, f.pixel));
    text_shadow(0, 0, (Rgba){0});
    text_align(TEXT_TOP);
    text_scale(1.0f);

    draw_big_messages(&f, d, viewport);

    if (me->active) {
        draw_bonus_overlay(hud, &f, d);
        if (!mine->spectator) draw_bars(hud, &f, me, ctx);
        if (!limbo && !team && !esc && !me->dead && !mine->spectator) {
            if (d->sniper_line) draw_sniper_line(hud, &f, me, cursor);
            draw_cursor(hud, &f, d, me, cursor);
        }
        if (!mine->spectator) draw_player_indicator(hud, &f, d, me);
        draw_ping_dot(hud, &f, d);
    }

    draw_kill_console_icons(hud, &f, d, viewport);
    if (d->minimap) draw_minimap(hud, &f, d, state, map_view);
    float frags_bottom = 0;
    if (d->frags_menu) frags_bottom = draw_frags_background(hud, &f, d, viewport);
    draw_menu_boxes(hud, &f, d, menus);
    draw_team_box(hud, &f, d);

    // the texts, shadowed
    text_shadow(1, 1, (Rgba){0, 0, 0, 255});
    if (me->active) draw_player_texts(&f, d, me, ctx);
    draw_team_scores(&f, d);
    if (d->paused) {
        text_color((Rgba){185, 250, 138, 255});
        text_draw("Game paused", 197 + f.fragx, 24);
    }
    if (d->stats_menu) draw_weapon_stats(hud, &f, d);
    if (d->frags_menu) draw_frags_texts(&f, d, frags_bottom);
    bool typing = d->chat_type != HUD_CHAT_NONE;
    bool dim = d->frags_menu || d->stats_menu || menus->menus[MENU_TEAM].active || (typing && menus->menus[MENU_LIMBO].active) ||
               (!typing && esc && menus->noob_show);
    draw_console(&f, d, dim);
    if (me->active) draw_respawn_texts(hud, &f, d, me);
    draw_vote(hud, &f, d);
    if (d->radio_menu && !esc) draw_radio_menu(hud, d);
    draw_chat_input(&f, d);
    draw_kill_console(&f, d, viewport);

    if (me->active) {
        draw_chat_texts(&f, d, state);
        if (d->player_names) draw_player_names(&f, d, state);
        if (d->survival && d->cease_fire_counter > 0) draw_cease_fire(&f, d, me);
    }

    text_style(FONT_SMALL);
    if (d->cursor_text[0] && !me->dead && !team && !esc) { // the name under the cursor
        text_color((Rgba){255, 255, 255, 0x77});
        text_draw(d->cursor_text, cursor.x - text_width(d->cursor_text) / 2, cursor.y + 10);
    }
    if (d->free_camera) {
        text_color((Rgba){205, 205, 205, 255});
        text_draw("Free Camera", (f.game_width - text_width("Free Camera")) / 2, 430);
    } else if (d->camera_follow >= 0 && d->camera_follow != d->me) {
        char str[HUD_NAME + 16];
        snprintf(str, sizeof(str), "Following %s", d->players[d->camera_follow].name);
        int dead = d->players[d->camera_follow].dead;
        text_color((Rgba){205, (uint8_t)(205 - dead * 105), (uint8_t)(205 - dead * 105), 255});
        text_draw(str, (f.game_width - text_width(str)) / 2, 430);
    }
    if (d->show_info) {
        char str[32];
        text_color((Rgba){239, 170, 200, 255});
        snprintf(str, sizeof(str), "FPS: %d", d->fps);
        text_draw(str, 460 * f.iscale_x, 10);
        snprintf(str, sizeof(str), "Ping: %d", d->ping);
        text_draw(str, 550 * f.iscale_x, 10);
    }
    if (d->recording) {
        text_color((Rgba){195, 0, 0, (uint8_t)fabsf(sinf(5.1f * (float)d->time / 2) * 255)});
        text_draw("REC", 612 * f.iscale_x, 1);
    }
    if (menus->noob_show && esc && d->chat_type == HUD_CHAT_NONE) draw_keys_help();
    if (d->shot_distance_shown) {
        char str[64];
        text_color((Rgba){230, 65, 60, (uint8_t)(150 + fabsf(sinf(5.1f * (float)d->time) * 100))});
        snprintf(str, sizeof(str), "DISTANCE: %.2fm", d->shot_distance);
        text_draw(str, 390 * f.iscale_x, 431);
        snprintf(str, sizeof(str), "AIRTIME: %.2fs", d->shot_airtime);
        text_draw(str, 228 * f.iscale_x, 431);
        if (d->shot_ricochets > 0) {
            snprintf(str, sizeof(str), "RICOCHETS: %d", d->shot_ricochets);
            text_draw(str, 62 * f.iscale_x, 431);
        }
    }

    // bullet time's widescreen cut
    if (d->bullet_time) {
        draw_rect(0, 0, f.game_width, 80, (Rgba){0, 0, 0, 255});
        draw_rect(0, GAME_HEIGHT - 80, f.game_width, GAME_HEIGHT, (Rgba){0, 0, 0, 255});
    }

    // the menus' texts, then the pointer
    if (limbo) draw_weapon_menu_texts(d, menus);
    if (esc) draw_esc_menu_texts(hud, &f, menus);
    if (team) draw_team_menu_texts(d, menus);
    if (menus->menus[MENU_KICK].active) draw_window_texts(hud, d, menus, MENU_KICK);
    if (menus->menus[MENU_MAP].active) draw_window_texts(hud, d, menus, MENU_MAP);
    text_shadow(0, 0, (Rgba){0});

    if (esc || limbo || team || me->dead) {
        draw_sprite(&hud->menucursor, pixel_align(&f, cursor.x), pixel_align(&f, cursor.y), 0,
                    (Rgba){255, 255, 255, STATUS_TRANSPARENCY});
    }
}
