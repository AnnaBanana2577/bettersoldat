#include "ui/mainmenu.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/systems/systems.h"
#include "gfx/font.h"
#include "input/input.h"
#include "network/network.h"

#define GAME_HEIGHT_UNITS 480.0f
#define LEFT 40.0f       // the home column
#define PAGE_X 280.0f    // where a page begins
#define ROW 26.0f        // a row's height
#define BUTTON_H 28.0f

static const Rgba TEXT = {255, 255, 255, 250};
static const Rgba DIM = {200, 205, 215, 220};
static const Rgba FIELD = {0, 0, 0, 110};
static const Rgba FIELD_FOCUSED = {30, 30, 40, 170};
static const Rgba BOX = {255, 255, 255, 112};

// --- the pieces ---------------------------------------------------------------------

typedef struct Ui {
    MainMenu *m;
    Console *con;
    const Interface *hud;
    Vec2 cursor;
    float game_width, pixel;
    bool click; // this draw's, used up by the first widget that takes it
} Ui;

static bool over(const Ui *ui, float x, float y, float w, float h)
{
    return ui->cursor.x >= x && ui->cursor.x < x + w && ui->cursor.y >= y && ui->cursor.y < y + h;
}

static bool take_click(Ui *ui, float x, float y, float w, float h)
{
    if (!ui->click || !over(ui, x, y, w, h)) return false;
    ui->click = false;
    return true;
}

static void rect(float x0, float y0, float x1, float y1, Rgba color)
{
    GfxVertex v[4] = {gfx_vertex(x0, y0, 0, 0, color), gfx_vertex(x1, y0, 0, 0, color), gfx_vertex(x1, y1, 0, 0, color),
                      gfx_vertex(x0, y1, 0, 0, color)};
    gfx_draw_quad(gfx_white(), v);
}

static void label(float x, float y, const char *text, Rgba color)
{
    text_style(FONT_SMALL);
    text_color(color);
    text_draw(text, x, y);
}

// A button the width of its box; true when clicked.
static bool button(Ui *ui, float x, float y, float w, const char *caption)
{
    bool hot = over(ui, x, y, w, BUTTON_H);
    rect(x, y, x + w, y + BUTTON_H, hot ? (Rgba){255, 255, 255, 60} : (Rgba){0, 0, 0, 70});
    text_style(FONT_MENU);
    text_color(TEXT);
    text_draw(caption, x + 10 + (hot ? 1 : 0), y + BUTTON_H / 2 - text_height(caption) / 2 - (hot ? 1 : 0));
    return take_click(ui, x, y, w, BUTTON_H);
}

// A text field editing a cvar: its value, or what is being typed while it has the focus.
static void field(Ui *ui, float x, float y, float w, const char *cvar, int max)
{
    MainMenu *m = ui->m;
    bool focused = strcmp(m->focus_cvar, cvar) == 0;
    rect(x, y, x + w, y + ROW - 4, focused ? FIELD_FOCUSED : FIELD);
    const Cvar *cv = cvar_find(ui->con, cvar);
    const char *shown = focused ? m->edit : cv ? cv->value : "";
    char text[MAINMENU_EDIT + 2];
    bool caret = focused && fmod(m->time, 1.0) < 0.5;
    snprintf(text, sizeof text, "%s%s", shown, caret ? "|" : "");
    label(x + 5, y + 3, text, TEXT);
    if (take_click(ui, x, y, w, ROW - 4)) {
        snprintf(m->focus_cvar, sizeof m->focus_cvar, "%s", cvar);
        snprintf(m->edit, sizeof m->edit, "%s", cv ? cv->value : "");
        m->edit_max = max;
        SDL_StartTextInput();
    }
}

// "< value >": the arrows step an integer cvar within its range, wrapping.
static void cycler(Ui *ui, float x, float y, const char *cvar, int lo, int hi, const char *shown)
{
    const Cvar *cv = cvar_find(ui->con, cvar);
    int v = cv ? clampi(cv->integer, lo, hi) : lo;
    bool left = take_click(ui, x, y, 20, ROW - 4), right = take_click(ui, x + 150, y, 20, ROW - 4);
    if (left || right) {
        v += right ? 1 : -1;
        if (v > hi) v = lo;
        if (v < lo) v = hi;
        char number[16];
        snprintf(number, sizeof number, "%d", v);
        cvar_set(ui->con, cvar, number);
    }
    label(x + 4, y + 3, "<", over(ui, x, y, 20, ROW - 4) ? TEXT : DIM);
    label(x + 154, y + 3, ">", over(ui, x + 150, y, 20, ROW - 4) ? TEXT : DIM);
    text_style(FONT_SMALL);
    text_color(TEXT);
    text_draw(shown, x + 85 - text_width(shown) / 2, y + 3);
}

// "< value >" over an integer cvar stepped by `step` within its range, shown as `fmt`
// of it (one %d). Not wrapping, as a limit shouldn't.
static void stepper(Ui *ui, float x, float y, const char *cvar, int lo, int hi, int step, const char *fmt)
{
    const Cvar *cv = cvar_find(ui->con, cvar);
    int v = cv ? clampi(cv->integer, lo, hi) : lo;
    bool left = take_click(ui, x, y, 20, ROW - 4), right = take_click(ui, x + 150, y, 20, ROW - 4);
    if (left || right) {
        char number[16];
        snprintf(number, sizeof number, "%d", clampi(v + (right ? step : -step), lo, hi));
        cvar_set(ui->con, cvar, number);
    }
    label(x + 4, y + 3, "<", over(ui, x, y, 20, ROW - 4) ? TEXT : DIM);
    label(x + 154, y + 3, ">", over(ui, x + 150, y, 20, ROW - 4) ? TEXT : DIM);
    char shown[32];
    snprintf(shown, sizeof shown, fmt, v);
    text_style(FONT_SMALL);
    text_color(TEXT);
    text_draw(shown, x + 85 - text_width(shown) / 2, y + 3);
}

// "< name >" over a cvar that takes one of `values`, each named; the arrows step along
// them, wrapping. A value that is none of them shows as the first.
static void choice(Ui *ui, float x, float y, const char *cvar, const int *values, const char *const *names, int count)
{
    const Cvar *cv = cvar_find(ui->con, cvar);
    int at = 0;
    for (int i = 0; i < count; i++)
        if (cv && cv->integer == values[i]) at = i;
    bool left = take_click(ui, x, y, 20, ROW - 4), right = take_click(ui, x + 150, y, 20, ROW - 4);
    if (left || right) {
        at = (at + (right ? 1 : count - 1)) % count;
        char number[16];
        snprintf(number, sizeof number, "%d", values[at]);
        cvar_set(ui->con, cvar, number);
    }
    label(x + 4, y + 3, "<", over(ui, x, y, 20, ROW - 4) ? TEXT : DIM);
    label(x + 154, y + 3, ">", over(ui, x + 150, y, 20, ROW - 4) ? TEXT : DIM);
    text_style(FONT_SMALL);
    text_color(TEXT);
    text_draw(names[at], x + 85 - text_width(names[at]) / 2, y + 3);
}

// As choice, but some values are locked: shown with "(locked)", skipped by the arrows,
// never set.
static void locked_choice(Ui *ui, float x, float y, const char *cvar, const int *values, const char *const *names,
                          const bool *locked, int count)
{
    const Cvar *cv = cvar_find(ui->con, cvar);
    int at = 0;
    for (int i = 0; i < count; i++)
        if (cv && cv->integer == values[i]) at = i;
    bool left = take_click(ui, x, y, 20, ROW - 4), right = take_click(ui, x + 150, y, 20, ROW - 4);
    if (left || right) {
        int next = at;
        do next = (next + (right ? 1 : count - 1)) % count;
        while (locked[next] && next != at); // nothing is unlocked: the cvar stays
        if (!locked[next]) {
            char number[16];
            snprintf(number, sizeof number, "%d", values[next]);
            cvar_set(ui->con, cvar, number);
        }
        at = next;
    }
    char shown[64];
    snprintf(shown, sizeof shown, "%s%s", names[at], locked[at] ? " (locked)" : "");
    label(x + 4, y + 3, "<", over(ui, x, y, 20, ROW - 4) ? TEXT : DIM);
    label(x + 154, y + 3, ">", over(ui, x + 150, y, 20, ROW - 4) ? TEXT : DIM);
    text_style(FONT_SMALL);
    text_color(TEXT);
    text_draw(shown, x + 85 - text_width(shown) / 2, y + 3);
}

// --- the pages ----------------------------------------------------------------------

static void page_join(Ui *ui, const char *status, bool joined)
{
    float x = PAGE_X, y = 120;
    label(x, y, "Server address (host:port)", DIM);
    field(ui, x, y + 18, 260, "cl_server", 63);
    label(x, y + 50, "Password, if the server asks one", DIM);
    field(ui, x, y + 68, 260, "cl_password", 31); // NET_PASSWORD_SIZE - 1
    y += 110;
    if (!joined) {
        if (button(ui, x, y, 120, "Connect")) {
            const Cvar *cv = cvar_find(ui->con, "cl_server");
            snprintf(ui->m->command, sizeof ui->m->command, "connect %s", cv && cv->value[0] ? cv->value : "127.0.0.1");
        }
    } else if (button(ui, x, y, 120, "Disconnect")) {
        snprintf(ui->m->command, sizeof ui->m->command, "disconnect");
    }
    if (status && status[0]) label(x, y + 40, status, DIM);
}

// --- local play ---------------------------------------------------------------------

// The rotation (sv_maps) as a list of names separated by spaces or commas: whether
// `name` is in it, and its place from 0 (-1 if not).
static int rotation_index(const char *list, const char *name)
{
    int at = 0;
    const char *p = list;
    while (*p) {
        while (*p == ' ' || *p == ',' || *p == '\t') p++;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != ' ' && *p != ',' && *p != '\t') p++;
        if ((size_t)(p - start) == strlen(name) && strncmp(start, name, (size_t)(p - start)) == 0) return at;
        at++;
    }
    return -1;
}

// `name` into the rotation if it isn't there, out of it if it is.
static void rotation_toggle(Console *con, const char *name)
{
    const Cvar *cv = cvar_find(con, "sv_maps");
    const char *list = cv ? cv->value : "";
    char out[CONSOLE_VALUE_SIZE] = "";
    size_t n = 0;
    bool had = false;
    const char *p = list;
    while (*p) {
        while (*p == ' ' || *p == ',' || *p == '\t') p++;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != ' ' && *p != ',' && *p != '\t') p++;
        if ((size_t)(p - start) == strlen(name) && strncmp(start, name, (size_t)(p - start)) == 0) {
            had = true;
            continue;
        }
        int w = snprintf(out + n, sizeof out - n, n ? " %.*s" : "%.*s", (int)(p - start), start);
        if (w < 0 || n + (size_t)w >= sizeof out) break;
        n += (size_t)w;
    }
    if (!had) {
        int w = snprintf(out + n, sizeof out - n, n ? " %s" : "%s", name);
        if (w < 0 || n + (size_t)w >= sizeof out) return; // no room for another
    }
    cvar_set(con, "sv_maps", out);
}

#define MAP_ROW 16.0f
#define MAP_ROWS 17 // shown at once

// The maps under assets, each a row to click into the rotation or out of it, the wheel
// paging through them; the rotation's places are numbered, as the rounds will go.
static void map_list(Ui *ui, float x, float y, const char (*maps)[64], int count)
{
    MainMenu *m = ui->m;
    float w = 220, h = MAP_ROWS * MAP_ROW;
    if (over(ui, x, y, w, h) && m->wheel) m->map_scroll -= m->wheel * 3;
    m->map_scroll = clampi(m->map_scroll, 0, maxi(count - MAP_ROWS, 0));
    rect(x, y, x + w, y + h, FIELD);
    const Cvar *cv = cvar_find(ui->con, "sv_maps");
    const char *list = cv ? cv->value : "";
    for (int row = 0; row < MAP_ROWS; row++) {
        int i = m->map_scroll + row;
        if (i >= count) break;
        float ry = y + (float)row * MAP_ROW;
        int at = rotation_index(list, maps[i]);
        bool hot = over(ui, x, ry, w, MAP_ROW);
        if (hot || at >= 0) rect(x, ry, x + w, ry + MAP_ROW, at >= 0 ? (Rgba){120, 160, 255, 70} : (Rgba){255, 255, 255, 40});
        char place[8] = "";
        if (at >= 0) snprintf(place, sizeof place, "%d.", at + 1);
        label(x + 4, ry + 1, place, TEXT);
        label(x + 26, ry + 1, maps[i], at >= 0 ? TEXT : DIM);
        if (take_click(ui, x, ry, w, MAP_ROW)) rotation_toggle(ui->con, maps[i]);
    }
    if (count > MAP_ROWS) { // where in the list this is
        float track = h - 4, knob = maxf(track * (float)MAP_ROWS / (float)count, 8);
        float top = y + 2 + (track - knob) * (float)m->map_scroll / (float)(count - MAP_ROWS);
        rect(x + w - 5, top, x + w - 2, top + knob, DIM);
    }
}

static void page_local(Ui *ui, const char *status, bool hosting, const char (*maps)[64], int count)
{
    float x = PAGE_X, y = 90;
    Console *con = ui->con;
    static const int MODES[] = {0, 1, 2};
    static const char *const MODE_NAMES[] = {"The map's own", "Deathmatch", "Capture the Flag"};
    static const int SKILLS[] = {300, 200, 100, 50, 10};
    static const char *const SKILL_NAMES[] = {"Stupid", "Poor", "Normal", "Hard", "Impossible"};
    static const int ONOFF[] = {0, 1};
    static const char *const ONOFF_NAMES[] = {"Off", "On"};

    label(x, y, "Mode", DIM);
    choice(ui, x + 110, y - 3, "sv_gamemode", MODES, MODE_NAMES, 3);
    y += ROW;
    label(x, y, "Time limit", DIM);
    stepper(ui, x + 110, y - 3, "sv_timelimit", 5, 60, 5, "%d min");
    y += ROW;
    label(x, y, "Score limit", DIM);
    stepper(ui, x + 110, y - 3, "sv_killlimit", 5, 100, 5, "%d");
    y += ROW + 6;
    label(x, y, "Bots, deathmatch", DIM);
    stepper(ui, x + 110, y - 3, "bots_random_noteam", 0, 15, 1, "%d");
    y += ROW;
    label(x, y, "Bots, alpha", DIM);
    stepper(ui, x + 110, y - 3, "bots_random_alpha", 0, 15, 1, "%d");
    y += ROW;
    label(x, y, "Bots, bravo", DIM);
    stepper(ui, x + 110, y - 3, "bots_random_bravo", 0, 15, 1, "%d");
    y += ROW;
    label(x, y, "Bot skill", DIM);
    choice(ui, x + 110, y - 3, "bots_difficulty", SKILLS, SKILL_NAMES, 5);
    y += ROW;
    label(x, y, "Bot chat", DIM);
    choice(ui, x + 110, y - 3, "bots_chat", ONOFF, ONOFF_NAMES, 2);
    y += ROW + 6;
    label(x, y, "Port", DIM);
    field(ui, x + 110, y - 3, 80, "sv_port", 5);
    y += ROW + 10;

    if (!hosting) {
        if (button(ui, x, y, 120, "Play")) snprintf(ui->m->command, sizeof ui->m->command, "host");
    } else if (button(ui, x, y, 120, "Stop")) {
        snprintf(ui->m->command, sizeof ui->m->command, "disconnect");
    }
    if (status && status[0]) label(x, y + 36, status, DIM);
    label(x, y + 58, "A server starts here and you join it; friends can", DIM);
    label(x, y + 72, "join too, at your address and this port.", DIM);

    float lx = x + 300;
    label(lx, 70, "Maps in rotation (click to add or remove; the first plays first)", DIM);
    map_list(ui, lx, 90, maps, count);
    const Cvar *cv = cvar_find(con, "sv_maps");
    if (!cv || !cv->value[0]) label(lx, 90 + MAP_ROWS * MAP_ROW + 6, "None chosen: the map cvar's plays, again and again.", DIM);
}

static const char *const HAIR_STYLES[] = {"Army", "Dreadlocks", "Punk", "Mr. T", "Normal", "Fringe", "Bob"};
static const char *const HEAD_STYLES[] = {"None", "Helmet", "Hat", "Waifu helmet"};
static const char *const CHAIN_STYLES[] = {"None", "Dog tags", "Gold chain"};
static const char *const SECONDARIES[] = {"USSOCOM", "Combat Knife", "Chainsaw", "LAW"};

static int cvar_int(const Console *con, const char *name, int lo, int hi)
{
    const Cvar *cv = cvar_find(con, name);
    return cv ? clampi(cv->integer, lo, hi) : lo;
}

static Rgba cvar_color(const Console *con, const char *name)
{
    Rgba color = {255, 255, 255, 255};
    const Cvar *cv = cvar_find(con, name);
    if (cv && !rgba_parse_hex(cv->value, &color)) rgba_parse_hex(cv->default_value, &color);
    return color;
}

typedef struct PaletteColor {
    Rgba color;
    float hue, saturation, value;
} PaletteColor;

// Sort colors by hue, then from darker to lighter within each hue.
static int compare_palette_colors(const void *a, const void *b)
{
    const PaletteColor *left = a, *right = b;
    if (left->hue != right->hue) return left->hue < right->hue ? -1 : 1;
    if (left->value != right->value) return left->value < right->value ? -1 : 1;
    if (left->saturation != right->saturation) return left->saturation < right->saturation ? -1 : 1;
    if (left->color.r != right->color.r) return left->color.r < right->color.r ? -1 : 1;
    if (left->color.g != right->color.g) return left->color.g < right->color.g ? -1 : 1;
    if (left->color.b != right->color.b) return left->color.b < right->color.b ? -1 : 1;
    return 0;
}

// Build the 216 RGB colors and arrange them in hue and brightness order.
static void make_palette(PaletteColor colors[216])
{
    static const uint8_t levels[] = {0, 51, 102, 153, 204, 255};
    int at = 0;
    for (int b = 0; b < 6; b++) {
        for (int g = 0; g < 6; g++) {
            for (int r = 0; r < 6; r++) {
                float red = (float)levels[r] / 255.0f;
                float green = (float)levels[g] / 255.0f;
                float blue = (float)levels[b] / 255.0f;
                float maximum = fmaxf(red, fmaxf(green, blue));
                float minimum = fminf(red, fminf(green, blue));
                float delta = maximum - minimum;
                float hue = 0;
                if (delta > 0) {
                    if (maximum == red)
                        hue = 60.0f * fmodf((green - blue) / delta, 6.0f);
                    else if (maximum == green)
                        hue = 60.0f * ((blue - red) / delta + 2.0f);
                    else
                        hue = 60.0f * ((red - green) / delta + 4.0f);
                    if (hue < 0) hue += 360.0f;
                } else {
                    hue = -1.0f;
                }
                colors[at++] = (PaletteColor){
                    .color = {levels[r], levels[g], levels[b], 255},
                    .hue = hue,
                    .saturation = maximum > 0 ? delta / maximum : 0,
                    .value = maximum,
                };
            }
        }
    }
    qsort(colors, 216, sizeof colors[0], compare_palette_colors);
}

static void color_picker(Ui *ui, const char *cvar, float x, float y, bool draw)
{
    if (strcmp(ui->m->color_picker, cvar) != 0) return;
    PaletteColor colors[216];
    make_palette(colors);
    const float cell = 9, gap = 1, width = 12 * (cell + gap) - gap;
    float left = clampf(x + 235, 20, ui->game_width - width - 30);
    float top = y;
    if (!draw) {
        if (!ui->click) return;
        if (!over(ui, left - 6, top - 5, width + 12, 209)) {
            ui->m->color_picker[0] = '\0';
            return;
        }
        ui->click = false;
        if (over(ui, left + width - 12, top - 2, 14, 16)) {
            ui->m->color_picker[0] = '\0';
            return;
        }
        for (int row = 0; row < 18; row++) {
            for (int col = 0; col < 12; col++) {
                float sx = left + (float)col * (cell + gap);
                float sy = top + 20 + (float)row * (cell + gap);
                if (!over(ui, sx, sy, cell, cell)) continue;
                Rgba color = colors[row * 12 + col].color;
                char hex[7];
                snprintf(hex, sizeof hex, "%02X%02X%02X", color.r, color.g, color.b);
                cvar_set(ui->con, cvar, hex);
                return;
            }
        }
        return;
    }
    rect(left - 6, top - 5, left + width + 6, top + 204, FIELD_FOCUSED);
    label(left, top, "Hex palette", TEXT);
    label(left + width - 10, top, "X", DIM);

    Rgba selected = cvar_color(ui->con, cvar);
    for (int row = 0; row < 18; row++) {
        for (int col = 0; col < 12; col++) {
            float sx = left + (float)col * (cell + gap);
            float sy = top + 20 + (float)row * (cell + gap);
            Rgba color = colors[row * 12 + col].color;
            bool current = color.r == selected.r && color.g == selected.g && color.b == selected.b;
            if (current) rect(sx - 1, sy - 1, sx + cell + 1, sy + cell + 1, TEXT);
            rect(sx, sy, sx + cell, sy + cell, color);
        }
    }
}

// The gostek as the cvars dress it, standing, at `at` in the menu's units, `scale`
// times its size in the world. Drawn from the art of the style the cvars choose.
static void preview(Ui *ui, const Gostek *gostek, const Anims *anims, Vec2 at, float scale)
{
    if (!gostek || !anims) return;
    Console *con = ui->con;
    Soldier s = {.active = true, .team = TEAM_NONE, .direction = 1, .health = DEFAULT_HEALTH, .aim = vec2(60, -12)};
    anim_set(anims, &s.legs, ANIM_STAND, 1);
    anim_set(anims, &s.body, ANIM_STAND, 1);
    RenderSoldier rs = {
        .active = true,
        .team = TEAM_NONE,
        .pose = soldier_pose(anims, &s, vec2(0, 0)),
        .weapon = (WeaponId)cvar_int(con, "cl_player_wep", WEAPON_EAGLE, WEAPON_MINIGUN),
        .secondary = (WeaponId)(WEAPON_COLT + cvar_int(con, "cl_player_secwep", 0, WEAPON_LAW - WEAPON_COLT)),
        .health = DEFAULT_HEALTH,
        .body_anim = ANIM_STAND,
        .wear_helmet = 1, // on the head, so the chosen headgear shows
        .look = {
            .shirt = cvar_color(con, "cl_player_shirt"),
            .pants = cvar_color(con, "cl_player_pants"),
            .skin = cvar_color(con, "cl_player_skin"),
            .hair = cvar_color(con, "cl_player_hair"),
            .jet = cvar_color(con, "cl_player_jet"),
            .hair_style = (uint8_t)cvar_int(con, "cl_player_hairstyle", 0, 6),
            .head_style = (uint8_t)cvar_int(con, "cl_player_headstyle", 0, 3),
            .chain_style = (uint8_t)cvar_int(con, "cl_player_chainstyle", 0, 2),
            .style = (uint8_t)cvar_int(con, "cl_player_style", 0, GOSTEK_STYLE_COUNT - 1),
        },
    };
    // the world's origin lands on `at`, the world `scale` times larger than the units
    gfx_transform(mat3_ortho(-at.x / scale, (ui->game_width - at.x) / scale, -at.y / scale, (GAME_HEIGHT_UNITS - at.y) / scale));
    gostek_draw(gostek, &rs, false);
    gfx_transform(mat3_ortho(0, ui->game_width, 0, GAME_HEIGHT_UNITS));
    text_pixel_ratio(vec2(ui->pixel, ui->pixel));
}

static void page_player(Ui *ui, const Gostek *gostek, const Anims *anims, const Weapons *weapons)
{
    float x = PAGE_X, y = 100;
    label(x, y, "Name", DIM);
    field(ui, x + 110, y - 3, 170, "cl_player_name", NET_NAME_SIZE - 1);
    y += ROW + 6;
    static const char *const COLOURS[][2] = {{"Shirt", "cl_player_shirt"}, {"Pants", "cl_player_pants"}, {"Skin", "cl_player_skin"},
                                             {"Hair", "cl_player_hair"},   {"Jet", "cl_player_jet"}};
    float colors_y = y;
    for (size_t i = 0; i < sizeof COLOURS / sizeof COLOURS[0]; i++) {
        if (strcmp(ui->m->color_picker, COLOURS[i][1]) == 0) {
            color_picker(ui, COLOURS[i][1], x, colors_y + (float)i * ROW - 3, false);
            break;
        }
    }
    for (size_t i = 0; i < sizeof COLOURS / sizeof COLOURS[0]; i++) {
        label(x, y, COLOURS[i][0], DIM);
        field(ui, x + 110, y - 3, 90, COLOURS[i][1], 6);
        Rgba c = cvar_color(ui->con, COLOURS[i][1]);
        c.a = 255;
        rect(x + 210, y - 3, x + 210 + ROW - 4, y - 3 + ROW - 4, c);
        if (take_click(ui, x + 210, y - 3, ROW - 4, ROW - 4)) {
            if (strcmp(ui->m->color_picker, COLOURS[i][1]) == 0)
                ui->m->color_picker[0] = '\0';
            else
                snprintf(ui->m->color_picker, sizeof ui->m->color_picker, "%s", COLOURS[i][1]);
        }
        y += ROW;
    }
    label(x, y, "Hair", DIM);
    int style = cvar_int(ui->con, "cl_player_style", 0, GOSTEK_STYLE_COUNT - 1);
    {
        static const int HAIR_VALUES[] = {0, 1, 2, 3, 4, 5, 6};
        // the rat and the furry wear only army, punk and Mr. T; everyone else may wear all six
        static const bool RAT_HAIR_LOCKED[] = {false, true, false, false, true, true, true};
        static const bool HAIR_FREE[] = {false, false, false, false, false, false, false};
        locked_choice(ui, x + 110, y - 3, "cl_player_hairstyle", HAIR_VALUES, HAIR_STYLES,
                      style == GOSTEK_STYLE_RAT || style == GOSTEK_STYLE_FURRY ? RAT_HAIR_LOCKED : HAIR_FREE,
                      (int)(sizeof HAIR_VALUES / sizeof HAIR_VALUES[0]));
    }
    y += ROW;
    label(x, y, "Head", DIM);
    {
        static const int HEAD_VALUES[] = {0, 1, 2, 3};
        // the rat and the furry wear no headgear; everyone else may wear all three
        static const bool RAT_HEAD_LOCKED[] = {false, true, true, true};
        static const bool HEAD_FREE[] = {false, false, false, false};
        locked_choice(ui, x + 110, y - 3, "cl_player_headstyle", HEAD_VALUES, HEAD_STYLES,
                      style == GOSTEK_STYLE_RAT || style == GOSTEK_STYLE_FURRY ? RAT_HEAD_LOCKED : HEAD_FREE,
                      (int)(sizeof HEAD_VALUES / sizeof HEAD_VALUES[0]));
    }
    y += ROW;
    label(x, y, "Chain", DIM);
    cycler(ui, x + 110, y - 3, "cl_player_chainstyle", 0, 2, CHAIN_STYLES[cvar_int(ui->con, "cl_player_chainstyle", 0, 2)]);
    y += ROW;
    label(x, y, "Style", DIM);
    {
        static const int STYLES[] = {GOSTEK_STYLE_MALE, GOSTEK_STYLE_FEMALE, GOSTEK_STYLE_WAIFU, GOSTEK_STYLE_RAT,
                                     GOSTEK_STYLE_FURRY};
        static const char *const STYLE_NAMES[] = {"Male", "Female", "Waifu", "Rat", "Furry"};
        static const bool STYLE_LOCKED[] = {false, false, false, false, false}; // female and rat wear the male art as a template for now
        locked_choice(ui, x + 110, y - 3, "cl_player_style", STYLES, STYLE_NAMES, STYLE_LOCKED,
                      (int)(sizeof STYLES / sizeof STYLES[0]));
    }
    y += ROW;
    int primary = cvar_int(ui->con, "cl_player_wep", WEAPON_EAGLE, WEAPON_MINIGUN);
    label(x, y, "Primary", DIM);
    cycler(ui, x + 110, y - 3, "cl_player_wep", WEAPON_EAGLE, WEAPON_MINIGUN, weapons && weapons->info[primary].name ? weapons->info[primary].name : "");
    y += ROW;
    label(x, y, "Secondary", DIM);
    cycler(ui, x + 110, y - 3, "cl_player_secwep", 0, 3, SECONDARIES[cvar_int(ui->con, "cl_player_secwep", 0, 3)]);
    y += ROW;
    label(x, y, "Enter RRGGBB or click a swatch to pick. Team games use the team's shirt.", DIM);

    preview(ui, gostek, anims, vec2(ui->game_width - 110, 250), 3.0f);
    for (size_t i = 0; i < sizeof COLOURS / sizeof COLOURS[0]; i++) {
        if (strcmp(ui->m->color_picker, COLOURS[i][1]) == 0) {
            color_picker(ui, COLOURS[i][1], x, colors_y + (float)i * ROW - 3, true);
            break;
        }
    }
}

// The keys' rows: what each does, and the key that does it.
typedef struct Control {
    const char *label, *command;
} Control;

static const Control CONTROLS[] = {
    {"Left", "+left"},          {"Right", "+right"},          {"Jump", "+jump"},         {"Crouch", "+crouch"},
    {"Prone", "+prone"},        {"Jet", "+jet"},              {"Fire", "+fire"},         {"Throw grenade", "+throw"},
    {"Reload", "+reload"},      {"Change weapon", "+change"}, {"Throw weapon", "+drop"}, {"Throw flag", "+flagthrow"},
    {"Suicide", "+suicide"},    {"Chat", "chat"},             {"Team chat", "teamchat"}, {"Command", "cmd"},
    {"Radio", "+radio"},        {"Weapons menu", "weaponsmenu"}, {"Team menu", "teammenu"}, {"Scoreboard", "fragsmenu"},
    {"Weapon stats", "statsmenu"}, {"Minimap", "toggle ui_minimap"},
};
#define CONTROL_COUNT ((int)(sizeof CONTROLS / sizeof CONTROLS[0]))

// The key bound to `command`, the first if several; "" if none.
static const char *key_of(const Console *con, const char *command)
{
    for (int i = 0; i < console_bind_count(con); i++) {
        const char *key, *text;
        if (console_bind_at(con, i, &key, &text) && strcmp(text, command) == 0) return key;
    }
    return "";
}

// `key` does `command` now, and nothing else does.
static void rebind(Console *con, const char *key, const char *command)
{
    char old[16][32];
    int n = 0;
    for (int i = 0; i < console_bind_count(con) && n < 16; i++) {
        const char *k, *text;
        if (console_bind_at(con, i, &k, &text) && strcmp(text, command) == 0) snprintf(old[n++], sizeof old[0], "%s", k);
    }
    for (int i = 0; i < n; i++) console_bind(con, old[i], "");
    console_bind(con, key, command);
}

static void page_controls(Ui *ui)
{
    float x = PAGE_X, y = 90;
    int half = (CONTROL_COUNT + 1) / 2;
    for (int i = 0; i < CONTROL_COUNT; i++) {
        float cx = x + (float)(i / half) * 190, cy = y + (float)(i % half) * 19;
        bool capturing = ui->m->capturing == i;
        bool hot = over(ui, cx, cy, 180, 18);
        if (hot || capturing) rect(cx, cy, cx + 180, cy + 18, capturing ? (Rgba){255, 230, 170, 60} : (Rgba){255, 255, 255, 40});
        label(cx + 4, cy + 2, CONTROLS[i].label, DIM);
        label(cx + 100, cy + 2, capturing ? "press a key" : key_of(ui->con, CONTROLS[i].command), TEXT);
        if (take_click(ui, cx, cy, 180, 18)) ui->m->capturing = i;
    }
    label(x, y + (float)half * 19 + 10, "Click a key to change it, then press the new one. Escape keeps the old.", DIM);
}

typedef struct Resolution {
    int w, h;
} Resolution;
static const Resolution RESOLUTIONS[] = {{640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1280, 960}, {1600, 900}, {1920, 1080}, {2560, 1440}};
#define RESOLUTION_COUNT ((int)(sizeof RESOLUTIONS / sizeof RESOLUTIONS[0]))

static void page_options(Ui *ui)
{
    float x = PAGE_X, y = 100;
    Console *con = ui->con;
    static const char *const MODES[] = {"Windowed", "Fullscreen", "Borderless"};
    label(x, y, "Window", DIM);
    cycler(ui, x + 110, y - 3, "r_fullscreen", 0, 2, MODES[cvar_int(con, "r_fullscreen", 0, 2)]);
    y += ROW;

    // the resolution: the presets, stepped through; the current one shown even between them
    int w = cvar_int(con, "r_screenwidth", 320, 16384), h = cvar_int(con, "r_screenheight", 240, 16384);
    int at = -1;
    for (int i = 0; i < RESOLUTION_COUNT; i++)
        if (RESOLUTIONS[i].w == w && RESOLUTIONS[i].h == h) at = i;
    char shown[32];
    snprintf(shown, sizeof shown, "%dx%d", w, h);
    label(x, y, "Resolution", DIM);
    bool left = take_click(ui, x + 110, y - 3, 20, ROW - 4), right = take_click(ui, x + 260, y - 3, 20, ROW - 4);
    if (left || right) {
        int next = at < 0 ? (right ? 0 : RESOLUTION_COUNT - 1) : (at + (right ? 1 : RESOLUTION_COUNT - 1)) % RESOLUTION_COUNT;
        char number[16];
        snprintf(number, sizeof number, "%d", RESOLUTIONS[next].w);
        cvar_set(con, "r_screenwidth", number);
        snprintf(number, sizeof number, "%d", RESOLUTIONS[next].h);
        cvar_set(con, "r_screenheight", number);
    }
    label(x + 114, y, "<", over(ui, x + 110, y - 3, 20, ROW - 4) ? TEXT : DIM);
    label(x + 264, y, ">", over(ui, x + 260, y - 3, 20, ROW - 4) ? TEXT : DIM);
    text_style(FONT_SMALL);
    text_color(TEXT);
    text_draw(shown, x + 195 - text_width(shown) / 2, y);
    y += ROW;

    label(x, y, "VSync", DIM);
    cycler(ui, x + 110, y - 3, "r_swapeffect", 0, 1, cvar_int(con, "r_swapeffect", 0, 1) ? "On" : "Off");
    y += ROW;
    label(x, y, "Volume", DIM);
    {
        int v = cvar_int(con, "snd_volume", 0, 100);
        snprintf(shown, sizeof shown, "%d", v);
        bool l = take_click(ui, x + 110, y - 3, 20, ROW - 4), r = take_click(ui, x + 260, y - 3, 20, ROW - 4);
        if (l || r) {
            char number[16];
            snprintf(number, sizeof number, "%d", clampi(v + (r ? 10 : -10), 0, 100));
            cvar_set(con, "snd_volume", number);
        }
        label(x + 114, y, "<", DIM);
        label(x + 264, y, ">", DIM);
        text_style(FONT_SMALL);
        text_color(TEXT);
        text_draw(shown, x + 195 - text_width(shown) / 2, y);
    }
    y += ROW;
    label(x, y, "Sensitivity", DIM);
    {
        const Cvar *cv = cvar_find(con, "cl_sensitivity");
        float v = cv ? cv->number : 1.0f;
        snprintf(shown, sizeof shown, "%.1f", v);
        bool l = take_click(ui, x + 110, y - 3, 20, ROW - 4), r = take_click(ui, x + 260, y - 3, 20, ROW - 4);
        if (l || r) {
            char number[16];
            snprintf(number, sizeof number, "%.1f", clampf(v + (r ? 0.1f : -0.1f), 0.1f, 5.0f));
            cvar_set(con, "cl_sensitivity", number);
        }
        label(x + 114, y, "<", DIM);
        label(x + 264, y, ">", DIM);
        text_style(FONT_SMALL);
        text_color(TEXT);
        text_draw(shown, x + 195 - text_width(shown) / 2, y);
    }
    y += ROW;
    label(x, y, "Player names", DIM);
    cycler(ui, x + 110, y - 3, "ui_playernames", 0, 1, cvar_int(con, "ui_playernames", 0, 1) ? "On" : "Off");
    y += ROW;
    label(x, y, "Minimap", DIM);
    cycler(ui, x + 110, y - 3, "ui_minimap", 0, 1, cvar_int(con, "ui_minimap", 0, 1) ? "On" : "Off");
    y += ROW;
    label(x, y, "Smoothing (ms)", DIM);
    {
        int v = cvar_int(con, "cl_smooth", 0, 500);
        snprintf(shown, sizeof shown, "%d", v);
        bool l = take_click(ui, x + 110, y - 3, 20, ROW - 4), r = take_click(ui, x + 260, y - 3, 20, ROW - 4);
        if (l || r) {
            char number[16];
            snprintf(number, sizeof number, "%d", clampi(v + (r ? 25 : -25), 0, 500));
            cvar_set(con, "cl_smooth", number);
        }
        label(x + 114, y, "<", DIM);
        label(x + 264, y, ">", DIM);
        text_style(FONT_SMALL);
        text_color(TEXT);
        text_draw(shown, x + 195 - text_width(shown) / 2, y);
    }
    y += ROW;
    label(x, y, "Map sky", DIM);
    cycler(ui, x + 110, y - 3, "r_forcebg", 0, 1, cvar_int(con, "r_forcebg", 0, 1) ? "My colours" : "The map's");
    y += ROW;
    if (cvar_int(con, "r_forcebg", 0, 1)) { // the two colours, top and bottom, as the look's are edited
        static const char *const SKY[][2] = {{"Sky top", "r_forcebg_color1"}, {"Sky bottom", "r_forcebg_color2"}};
        for (size_t i = 0; i < sizeof SKY / sizeof SKY[0]; i++) {
            label(x, y, SKY[i][0], DIM);
            field(ui, x + 110, y - 3, 90, SKY[i][1], 6);
            Rgba c = cvar_color(con, SKY[i][1]);
            c.a = 255;
            rect(x + 210, y - 3, x + 210 + ROW - 4, y - 3 + ROW - 4, c);
            y += ROW;
        }
    }
    y += 6;
    label(x, y, "Everything here is saved to config.cfg when the game closes.", DIM);
}

// --- the menu -----------------------------------------------------------------------

static void unfocus(MainMenu *m)
{
    if (m->focus_cvar[0]) SDL_StopTextInput();
    m->focus_cvar[0] = '\0';
}

void mainmenu_show(MainMenu *m, bool shown)
{
    m->shown = shown;
    m->page = MAIN_HOME;
    m->color_picker[0] = '\0';
    m->capturing = -1;
    m->clicked = false;
    unfocus(m);
}

// Text into the focused field, typed or pasted: what fits, control characters (a pasted
// line's end among them) left out; the cvar follows.
static void edit_insert(MainMenu *m, Console *con, const char *text)
{
    size_t len = strlen(m->edit);
    for (const char *s = text; *s && (int)len < m->edit_max && len + 1 < sizeof m->edit; s++) {
        if ((unsigned char)*s < 32) continue;
        m->edit[len++] = *s;
        m->edit[len] = '\0';
    }
    cvar_set(con, m->focus_cvar, m->edit);
}

bool mainmenu_event(MainMenu *m, Console *con, const SDL_Event *e)
{
    if (!m->shown) return false;
    // a key for a control
    if (m->capturing >= 0) {
        if (e->type == SDL_KEYDOWN && e->key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
            m->capturing = -1;
            return true;
        }
        char name[32];
        if (input_event_key_name(e, name, sizeof name)) {
            rebind(con, name, CONTROLS[m->capturing].command);
            m->capturing = -1;
            return true;
        }
        return e->type == SDL_KEYDOWN || e->type == SDL_KEYUP || e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP ||
               e->type == SDL_TEXTINPUT;
    }
    // a field being typed into: what is typed, or pasted with Ctrl+V (an address, a password)
    if (m->focus_cvar[0]) {
        if (e->type == SDL_TEXTINPUT) {
            edit_insert(m, con, e->text.text);
            return true;
        }
        if (e->type == SDL_KEYDOWN) {
            if ((e->key.keysym.mod & KMOD_CTRL) && e->key.keysym.scancode == SDL_SCANCODE_V) {
                char *clip = SDL_GetClipboardText();
                if (clip) {
                    edit_insert(m, con, clip);
                    SDL_free(clip);
                }
                return true;
            }
            switch (e->key.keysym.scancode) {
            case SDL_SCANCODE_BACKSPACE: {
                size_t len = strlen(m->edit);
                if (len) m->edit[len - 1] = '\0';
                cvar_set(con, m->focus_cvar, m->edit);
                break;
            }
            case SDL_SCANCODE_RETURN:
            case SDL_SCANCODE_KP_ENTER:
            case SDL_SCANCODE_TAB:
            case SDL_SCANCODE_ESCAPE: unfocus(m); break;
            default: break;
            }
            return true;
        }
        if (e->type == SDL_KEYUP) return true;
    }
    switch (e->type) {
    case SDL_MOUSEBUTTONDOWN:
        if (e->button.button == SDL_BUTTON_LEFT) m->clicked = true;
        return true;
    case SDL_MOUSEWHEEL: m->wheel += e->wheel.y; return true;
    case SDL_MOUSEBUTTONUP:
    case SDL_TEXTINPUT:
    case SDL_KEYUP: return true;
    case SDL_KEYDOWN:
        if (e->key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
            if (m->page != MAIN_HOME) m->page = MAIN_HOME;
            else if (m->joined) mainmenu_show(m, false); // nothing to go back to otherwise
        }
        return true;
    default: return false;
    }
}

void mainmenu_draw(MainMenu *m, Console *con, const Interface *hud, const Gostek *gostek, const Anims *anims,
                   const Weapons *weapons, Vec2 cursor, float game_width, float pixel, double time, const char *status,
                   bool joined, bool hosting, const char (*maps)[64], int map_count)
{
    if (!m->shown) {
        m->wheel = 0;
        return;
    }
    m->time = time;
    m->joined = joined;
    Ui ui = {.m = m, .con = con, .hud = hud, .cursor = cursor, .game_width = game_width, .pixel = pixel, .click = m->clicked};
    m->clicked = false;

    gfx_transform(mat3_ortho(0, game_width, 0, GAME_HEIGHT_UNITS));
    text_pixel_ratio(vec2(pixel, pixel));
    text_shadow(1, 1, (Rgba){0, 0, 0, 200});
    text_align(TEXT_TOP);
    text_scale(1.0f);

    { // the background: a gradient, dusk at the top to night below
        Rgba top = {28, 34, 52, 255}, bottom = {6, 7, 12, 255};
        GfxVertex v[4] = {gfx_vertex(0, 0, 0, 0, top), gfx_vertex(game_width, 0, 0, 0, top),
                          gfx_vertex(game_width, GAME_HEIGHT_UNITS, 0, 0, bottom), gfx_vertex(0, GAME_HEIGHT_UNITS, 0, 0, bottom)};
        gfx_draw_quad(gfx_white(), v);
    }
    interface_draw_box(hud, 20, 20, game_width - 40, GAME_HEIGHT_UNITS - 40, BOX);

    text_style_scaled(FONT_BIG, 1.0f);
    text_color(TEXT);
    text_draw("SoldatReloaded", LEFT, 36);

    // the home column
    float y = 120;
    static const char *const PAGES[] = {"Join Game", "Local Play", "Player", "Controls", "Options"};
    for (int i = 0; i < 5; i++) {
        if (button(&ui, LEFT, y, 200, PAGES[i])) {
            m->page = (MainPage)(MAIN_JOIN + i);
            m->color_picker[0] = '\0';
            m->capturing = -1;
            unfocus(m);
        }
        y += BUTTON_H + 6;
    }
    y += 10;
    if (joined) {
        if (button(&ui, LEFT, y, 200, "Resume")) mainmenu_show(m, false);
        y += BUTTON_H + 6;
    }
    if (button(&ui, LEFT, y, 200, "Quit")) snprintf(m->command, sizeof m->command, "quit");

    switch (m->page) {
    case MAIN_JOIN: page_join(&ui, status, joined); break;
    case MAIN_LOCAL: page_local(&ui, status, hosting, maps, map_count); break;
    case MAIN_PLAYER: page_player(&ui, gostek, anims, weapons); break;
    case MAIN_CONTROLS: page_controls(&ui); break;
    case MAIN_OPTIONS: page_options(&ui); break;
    default:
        label(PAGE_X, 120, "Join a server, or play here against bots (Local Play). Escape returns here from the game.", DIM);
        break;
    }
    if (ui.click) unfocus(m); // a click on nothing takes the focus away
    m->wheel = 0;

    text_shadow(0, 0, (Rgba){0});
    interface_draw_pointer(hud, cursor);
}

bool mainmenu_take_command(MainMenu *m, char *out, size_t size)
{
    if (!m->command[0]) return false;
    snprintf(out, size, "%s", m->command);
    m->command[0] = '\0';
    return true;
}
