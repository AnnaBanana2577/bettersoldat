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

// --- the pages ----------------------------------------------------------------------

static void page_join(Ui *ui, const char *status, bool joined)
{
    float x = PAGE_X, y = 120;
    label(x, y, "Server address (host:port)", DIM);
    field(ui, x, y + 18, 260, "cl_server", 63);
    y += 60;
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

static const char *const HAIR_STYLES[] = {"Army", "Dreadlocks", "Punk", "Mr. T", "Normal"};
static const char *const HEAD_STYLES[] = {"None", "Helmet", "Hat"};
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

// The gostek as the cvars dress it, standing, at `at` in the menu's units, `scale`
// times its size in the world.
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
        .look = {
            .shirt = cvar_color(con, "cl_player_shirt"),
            .pants = cvar_color(con, "cl_player_pants"),
            .skin = cvar_color(con, "cl_player_skin"),
            .hair = cvar_color(con, "cl_player_hair"),
            .jet = cvar_color(con, "cl_player_jet"),
            .hair_style = (uint8_t)cvar_int(con, "cl_player_hairstyle", 0, 4),
            .head_style = (uint8_t)cvar_int(con, "cl_player_headstyle", 0, 2),
            .chain_style = (uint8_t)cvar_int(con, "cl_player_chainstyle", 0, 2),
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
    for (size_t i = 0; i < sizeof COLOURS / sizeof COLOURS[0]; i++) {
        label(x, y, COLOURS[i][0], DIM);
        field(ui, x + 110, y - 3, 90, COLOURS[i][1], 6);
        Rgba c = cvar_color(ui->con, COLOURS[i][1]);
        c.a = 255;
        rect(x + 210, y - 3, x + 210 + ROW - 4, y - 3 + ROW - 4, c);
        y += ROW;
    }
    label(x, y, "Hair", DIM);
    cycler(ui, x + 110, y - 3, "cl_player_hairstyle", 0, 4, HAIR_STYLES[cvar_int(ui->con, "cl_player_hairstyle", 0, 4)]);
    y += ROW;
    label(x, y, "Head", DIM);
    cycler(ui, x + 110, y - 3, "cl_player_headstyle", 0, 2, HEAD_STYLES[cvar_int(ui->con, "cl_player_headstyle", 0, 2)]);
    y += ROW;
    label(x, y, "Chain", DIM);
    cycler(ui, x + 110, y - 3, "cl_player_chainstyle", 0, 2, CHAIN_STYLES[cvar_int(ui->con, "cl_player_chainstyle", 0, 2)]);
    y += ROW;
    int primary = cvar_int(ui->con, "cl_player_wep", WEAPON_EAGLE, WEAPON_MINIGUN);
    label(x, y, "Primary", DIM);
    cycler(ui, x + 110, y - 3, "cl_player_wep", WEAPON_EAGLE, WEAPON_MINIGUN, weapons && weapons->info[primary].name ? weapons->info[primary].name : "");
    y += ROW;
    label(x, y, "Secondary", DIM);
    cycler(ui, x + 110, y - 3, "cl_player_secwep", 0, 3, SECONDARIES[cvar_int(ui->con, "cl_player_secwep", 0, 3)]);
    y += ROW;
    label(x, y, "Colours are RRGGBB in hex. The shirt is the team's in a team game.", DIM);

    preview(ui, gostek, anims, vec2(ui->game_width - 110, 250), 3.0f);
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
    y += ROW + 6;
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
    m->capturing = -1;
    m->clicked = false;
    unfocus(m);
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
    // a field being typed into
    if (m->focus_cvar[0]) {
        if (e->type == SDL_TEXTINPUT) {
            size_t len = strlen(m->edit);
            for (const char *s = e->text.text; *s && (int)len < m->edit_max && len + 1 < sizeof m->edit; s++) {
                if ((unsigned char)*s < 32) continue;
                m->edit[len++] = *s;
                m->edit[len] = '\0';
            }
            cvar_set(con, m->focus_cvar, m->edit);
            return true;
        }
        if (e->type == SDL_KEYDOWN) {
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
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEWHEEL:
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
                   bool joined)
{
    if (!m->shown) return;
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
    text_draw("bettersoldat", LEFT, 36);

    // the home column
    float y = 120;
    static const char *const PAGES[] = {"Join Game", "Player", "Controls", "Options"};
    for (int i = 0; i < 4; i++) {
        if (button(&ui, LEFT, y, 200, PAGES[i])) {
            m->page = (MainPage)(MAIN_JOIN + i);
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
    case MAIN_PLAYER: page_player(&ui, gostek, anims, weapons); break;
    case MAIN_CONTROLS: page_controls(&ui); break;
    case MAIN_OPTIONS: page_options(&ui); break;
    default:
        label(PAGE_X, 120, "Join a server to play. Escape returns here from the game.", DIM);
        break;
    }
    if (ui.click) unfocus(m); // a click on nothing takes the focus away

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
