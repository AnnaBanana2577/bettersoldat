#include "ui/mainmenu.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "game/systems/systems.h"
#include "gfx/font.h"
#include "input/input.h"
#include "network/network.h"
#include "ui/taunts.h"

#ifndef SOLDATRELOADED_VERSION
#define SOLDATRELOADED_VERSION "dev" // xmake.lua sets it from set_version
#endif

// The layout, in the HUD's units (480 tall): the rail down the left, and the page
// beside it with its header (the title and a line under it), its body, and its footer
// (what the page says of where it stands, and its one button), all on one ground.
#define VIEW_H 480.0f
#define RAIL_W 148.0f
#define EDGE 14.0f        // the panel's distance from the rail and the window's edges
#define PAD 18.0f         // inside the panel
#define PANEL_X (RAIL_W + EDGE)
#define PANEL_TOP EDGE
#define PANEL_BOTTOM (VIEW_H - EDGE)
#define HEADER_H 58.0f
#define FOOTER_H 50.0f
#define BODY_TOP (PANEL_TOP + HEADER_H + 10)
#define BODY_BOTTOM (PANEL_BOTTOM - FOOTER_H - 8)
#define ACTION_CY (PANEL_BOTTOM - FOOTER_H / 2)
#define BIG_H 30.0f       // the footer's button
#define ROW_H 26.0f
#define SECTION_H 30.0f
#define CTRL_H 22.0f      // a field, a list's box, a chip, a small button
#define RADIUS 4.0f       // every control's corners
#define POPUP_ROW 20.0f
#define SCROLL_STEP 30.0f

// The look: night and steel for the ground, white for what is read, and one accent,
// ember, kept for what acts or is chosen: the main button, the page in the rail, the
// server picked, where the keys are, what is being set. Nothing else is coloured.
static const Rgba ACCENT = {232, 80, 30, 255}; // #E8501E
static const Rgba ACCENT_HOT = {242, 112, 66, 255};
static const Rgba ACCENT_SOFT = {232, 80, 30, 38};
static const Rgba TEXT = {236, 239, 244, 255};
static const Rgba MUTED = {146, 155, 172, 255};
static const Rgba FAINT = {94, 102, 120, 255};
static const Rgba SURFACE = {14, 17, 25, 222}; // the one ground the rail and the page share
static const Rgba DIVIDER = {255, 255, 255, 24};
static const Rgba CONTROL = {28, 34, 47, 255};
static const Rgba CONTROL_HOT = {38, 46, 63, 255};
static const Rgba TYPING = {12, 15, 22, 255};
static const Rgba TRACK = {48, 56, 74, 255};
static const Rgba BORDER = {255, 255, 255, 22}; // the hairline round a control
static const Rgba LINE = {255, 255, 255, 14};
static const Rgba HOVER = {255, 255, 255, 10};
static const Rgba WELL = {0, 0, 0, 70};
static const Rgba GOOD = {111, 208, 140, 255};
static const Rgba WARN = {236, 192, 84, 255};
static const Rgba BAD = {232, 85, 74, 255};

// The menu's type: four roles, each a face at a size that follows the window, with the
// space between its letters. Play for what is read, its Bold for emphasis, Russo One
// (wide, square) for the titles, and Black Ops One, a stencil, for the name alone; the
// capitals are tracked out, as small capitals are.
typedef struct Font {
    FontStyleId id;
    float scale;
    float tracking; // em
} Font;
static const Font F_TINY = {FONT_UI, 0.82f, 0};
static const Font F_BODY = {FONT_UI, 0.95f, 0};
static const Font F_BOLD = {FONT_UI_BOLD, 0.95f, 0};
static const Font F_LABEL = {FONT_UI, 1.0f, 0};
static const Font F_NAV = {FONT_UI, 1.0f, 0};
static const Font F_BUTTON = {FONT_UI_BOLD, 0.95f, 0.02f};
static const Font F_BIG = {FONT_UI_BOLD, 1.0f, 0.12f};
static const Font F_SECTION = {FONT_UI_BOLD, 0.8f, 0.16f};
static const Font F_GROUP = {FONT_UI_BOLD, 0.7f, 0.24f}; // the rail's group labels: smaller and fainter than its items
static const Font F_SUBTITLE = {FONT_UI, 0.95f, 0};
static const Font F_TITLE = {FONT_DISPLAY, 1.25f, 0.01f};
static const Font F_LOGO = {FONT_LOGO, 1.9f, 0.03f};
static const Font F_LOGO_SUB = {FONT_UI_BOLD, 0.78f, 0}; // its tracking is worked out to fit under the name

static void font_use(Font f)
{
    text_style_scaled(f.id, f.scale);
    text_tracking(f.tracking);
}

// The rail: the pages under their groups, play first.
static const char *const PAGE_NAMES[MAIN_PAGE_COUNT] = {
    "Servers", "Join by address", "Local play", "Demos", "Player", "Controls", "Taunts", "Options", "Graphics",
};
static const char *const PAGE_TITLES[MAIN_PAGE_COUNT] = {
    "Servers", "Join by address", "Local play", "Demos", "Player", "Controls", "Taunts", "Options", "Graphics",
};
static const char *const PAGE_LINES[MAIN_PAGE_COUNT] = {
    "The games being played now, from the lobby.",
    "Connect to a server you know the address of.",
    "Host a game on this machine, against bots or for friends on your network.",
    "Games recorded here, to watch again.",
    "Your name, and how your soldier looks and what it carries.",
    "The keys. Click a binding, then press the new key; Escape cancels.",
    "What a key says: a message to everyone or the team, or your own words as a radio call.",
    "Sound, the mouse, the interface and the connection.",
    "The window, and what is drawn of the world.",
};

// --- drawing ------------------------------------------------------------------------

static void rect(float x0, float y0, float x1, float y1, Rgba color)
{
    GfxVertex v[4] = {gfx_vertex(x0, y0, 0, 0, color), gfx_vertex(x1, y0, 0, 0, color), gfx_vertex(x1, y1, 0, 0, color),
                      gfx_vertex(x0, y1, 0, 0, color)};
    gfx_draw_quad(gfx_white(), v);
}

// A box with its corners rounded by `r`: a cross of three quads and a fan at each corner,
// none overlapping, so a see-through colour stays even.
static void rrect(float x, float y, float w, float h, float r, Rgba color)
{
    r = minf(r, minf(w, h) / 2);
    if (r < 0.75f) {
        rect(x, y, x + w, y + h, color);
        return;
    }
    rect(x + r, y, x + w - r, y + h, color);
    rect(x, y + r, x + r, y + h - r, color);
    rect(x + w - r, y + r, x + w, y + h - r, color);
    enum { SEGMENTS = 6 };
    GfxVertex v[4 * SEGMENTS * 3];
    int n = 0;
    const float cx[4] = {x + r, x + w - r, x + w - r, x + r}, cy[4] = {y + r, y + r, y + h - r, y + h - r};
    const float start[4] = {(float)M_PI, 1.5f * (float)M_PI, 0, 0.5f * (float)M_PI};
    for (int c = 0; c < 4; c++) {
        for (int s = 0; s < SEGMENTS; s++) {
            float a0 = start[c] + 0.5f * (float)M_PI * (float)s / SEGMENTS;
            float a1 = start[c] + 0.5f * (float)M_PI * (float)(s + 1) / SEGMENTS;
            v[n++] = gfx_vertex(cx[c], cy[c], 0, 0, color);
            v[n++] = gfx_vertex(cx[c] + r * cosf(a0), cy[c] + r * sinf(a0), 0, 0, color);
            v[n++] = gfx_vertex(cx[c] + r * cosf(a1), cy[c] + r * sinf(a1), 0, 0, color);
        }
    }
    gfx_draw_triangles(gfx_white(), v, n);
}

static void circle(float x, float y, float r, Rgba color) { rrect(x - r, y - r, 2 * r, 2 * r, r, color); }

// One window pixel in units, for the hairlines: set as each draw begins.
static float hairline = 1.0f;

// A box with its corners rounded by `r`: `fill` inside a hairline of `border`.
static void box_r(float x, float y, float w, float h, float r, Rgba fill, Rgba border)
{
    float b = hairline;
    rrect(x, y, w, h, r, border);
    rrect(x + b, y + b, w - 2 * b, h - 2 * b, r - b, fill);
}

// A control's box: the corners RADIUS, as every control's are.
static void box(float x, float y, float w, float h, Rgba fill, Rgba border) { box_r(x, y, w, h, RADIUS, fill, border); }

// A line one window pixel thick, across from `x0` to `x1` at `y`.
static void rule(float x0, float x1, float y, Rgba color) { rect(x0, y, x1, y + hairline, color); }

// A small triangle pointing down (a list's box, a column sorted downwards) or up.
static void chevron(float x, float y, float size, bool down, Rgba color)
{
    float h = size * 0.6f;
    GfxVertex v[3];
    if (down) {
        v[0] = gfx_vertex(x - size / 2, y - h / 2, 0, 0, color);
        v[1] = gfx_vertex(x + size / 2, y - h / 2, 0, 0, color);
        v[2] = gfx_vertex(x, y + h / 2, 0, 0, color);
    } else {
        v[0] = gfx_vertex(x - size / 2, y + h / 2, 0, 0, color);
        v[1] = gfx_vertex(x, y - h / 2, 0, 0, color);
        v[2] = gfx_vertex(x + size / 2, y + h / 2, 0, 0, color);
    }
    gfx_draw_triangles(gfx_white(), v, 3);
}

// A padlock, for a server that asks a password: 7 by 9 units from `x, y`.
static void padlock(float x, float y, Rgba color)
{
    rect(x + 1, y, x + 6, y + 1, color);
    rect(x + 1, y + 1, x + 2, y + 4, color);
    rect(x + 5, y + 1, x + 6, y + 4, color);
    rrect(x, y + 4, 7, 5, 1, color);
}

static float line_height(Font style)
{
    font_use(style);
    return text_height("Ag");
}

static float width_of(Font style, const char *text)
{
    font_use(style);
    return text_width(text);
}

static void text_at(Font style, const char *text, float x, float y, Rgba color)
{
    font_use(style);
    text_color(color);
    text_draw(text, x, y);
}

// Text centred on the line `cy`.
static void text_mid(Font style, const char *text, float x, float cy, Rgba color)
{
    text_at(style, text, x, cy - line_height(style) / 2, color);
}

// `text` cut to fit `width` in `style`, "..." where it was cut.
static void fit(Font style, char *out, size_t size, const char *text, float width)
{
    font_use(style);
    snprintf(out, size, "%s", text);
    if (text_width(out) <= width) return;
    for (size_t n = strlen(out); n > 0; n--) {
        snprintf(out, size, "%.*s...", (int)n, text);
        if (text_width(out) <= width) return;
    }
    snprintf(out, size, "...");
}

static void text_fit(Font style, const char *text, float x, float cy, float width, Rgba color)
{
    char cut[192];
    fit(style, cut, sizeof cut, text, width);
    text_mid(style, cut, x, cy, color);
}

// `text` in lines no wider than `width`, broken between words, from `y` down: the
// height it took.
static float text_wrap(Font style, const char *text, float x, float y, float width, Rgba color)
{
    float lh = line_height(style) + 2, top = y;
    char line[256] = "", trial[256];
    const char *p = text;
    while (*p) {
        const char *end = p;
        while (*end && *end != ' ') end++;
        snprintf(trial, sizeof trial, "%s%s%.*s", line, line[0] ? " " : "", (int)(end - p), p);
        if (line[0] && width_of(style, trial) > width) {
            text_at(style, line, x, y, color);
            y += lh;
            snprintf(line, sizeof line, "%.*s", (int)(end - p), p);
        } else {
            memcpy(line, trial, sizeof line);
        }
        p = end;
        while (*p == ' ') p++;
    }
    if (line[0]) {
        text_at(style, line, x, y, color);
        y += lh;
    }
    return y - top;
}

static Rgba with_alpha(Rgba color, uint8_t a)
{
    color.a = a;
    return color;
}

// --- the cvars ----------------------------------------------------------------------

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

// A colour cvar whose default is none (the grenades': their own art) and that holds none.
static bool color_unset(const Console *con, const char *name)
{
    const Cvar *cv = cvar_find(con, name);
    Rgba unused;
    return cv && !cv->default_value[0] && !rgba_parse_hex(cv->value, &unused);
}

static void set_int(Console *con, const char *name, int value)
{
    char number[16];
    snprintf(number, sizeof number, "%d", value);
    cvar_set(con, name, number);
}

// --- the pieces ---------------------------------------------------------------------

typedef struct Ui {
    MainMenu *m;
    Console *con;
    const Interface *hud;
    Vec2 cursor;
    float game_width, pixel;
    bool click;  // this draw's, used up by the first widget that takes it
    bool held;   // the left button is down: a slider follows the cursor
    bool blocked; // a popup is open: nothing under it is hovered or clicked
    float block_x, block_y, block_w, block_h;
    // the keys since the last draw, for the widget with the focus; what none takes moves it
    int move, side, page;
    bool enter, back;
    bool show_focus;
    int nav_count; // the focusable widgets laid out so far
    // the layout: the column rows go down, from `y`; what lies outside [top, bottom] is
    // not drawn (there is no clipping), and `extent` is how far the page reaches
    float x, w, y, top, bottom, extent;
    bool scrolling; // the rows here scroll with the page
} Ui;

static bool inside(Vec2 p, float x, float y, float w, float h) { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }

static bool over(const Ui *ui, float x, float y, float w, float h)
{
    if (ui->blocked && inside(ui->cursor, ui->block_x, ui->block_y, ui->block_w, ui->block_h)) return false;
    return inside(ui->cursor, x, y, w, h);
}

// The click, if it is on this box: used up, and the widget `id` (if any) takes the focus.
static bool take(Ui *ui, int id, float x, float y, float w, float h)
{
    if (!ui->click || !over(ui, x, y, w, h)) return false;
    ui->click = false;
    if (id >= 0) {
        ui->m->zone = MAIN_ZONE_CONTENT;
        ui->m->nav = id;
    }
    return true;
}

static int take_side(Ui *ui, bool focused)
{
    if (!focused) return 0;
    int side = ui->side;
    ui->side = 0;
    return side;
}

static bool take_enter(Ui *ui, bool focused)
{
    if (!focused || !ui->enter) return false;
    ui->enter = false;
    return true;
}

// The next widget in the keys' order, and whether it has the focus. A focused one out of
// view brings the page to it, if the keys put the focus there.
static int nav_next(Ui *ui) { return ui->nav_count++; }

static bool nav_focused(Ui *ui, int id, float y, float h)
{
    MainMenu *m = ui->m;
    bool focused = m->zone == MAIN_ZONE_CONTENT && m->nav == id;
    if (focused && ui->scrolling && m->scroll_follow) {
        if (y < ui->top) m->scroll -= ui->top - y + 6;
        else if (y + h > ui->bottom) m->scroll += y + h - ui->bottom + 6;
    }
    return focused;
}

static void focus_ring(const Ui *ui, bool focused, float x, float y, float w, float h, float r)
{
    if (focused && ui->show_focus) rrect(x - 2, y - 2, w + 4, h + 4, r + 2, with_alpha(ACCENT, 170));
}

// The scrollbars: the page's, and the lists' own.
enum { SCROLL_PAGE, SCROLL_SERVERS, SCROLL_MAPS, SCROLL_DEMOS };
#define SCROLL_W 3.0f
#define SCROLL_HIT 16.0f // the bar is thin; the mouse gets more

// A scrollbar's mouse: the bar `track` tall from `top` at `x`, its knob `knob` tall at
// `pos` (0 to 1) of the way down. A press on the knob holds it; one on the track beside
// it brings the knob there and holds on. The position, moved as the mouse drags. Called
// before whatever lies under the bar, so the press is the bar's.
static float scroll_take(Ui *ui, int id, float x, float top, float track, float knob, float pos)
{
    MainMenu *m = ui->m;
    float run = track - knob, hit_x = x - (SCROLL_HIT - SCROLL_W) / 2;
    if (run <= 0) return pos;
    float knob_y = top + run * pos;
    if (ui->click && over(ui, hit_x, top, SCROLL_HIT, track)) {
        ui->click = false;
        m->scroll_drag = id;
        m->scroll_grab = inside(ui->cursor, hit_x, knob_y, SCROLL_HIT, knob) ? ui->cursor.y - knob_y : knob / 2;
    }
    if (m->scroll_drag == id && ui->held) {
        pos = clampf((ui->cursor.y - m->scroll_grab - top) / run, 0, 1);
        m->scroll_follow = false;
    }
    return pos;
}

static void scroll_draw(const Ui *ui, int id, float x, float top, float track, float knob, float pos)
{
    bool active = ui->m->scroll_drag == id || over(ui, x - (SCROLL_HIT - SCROLL_W) / 2, top, SCROLL_HIT, track);
    rrect(x, top + (track - knob) * pos, SCROLL_W, knob, SCROLL_W / 2, active ? MUTED : FAINT);
}

// A row of the column: its box, whether it is in view, focused, under the cursor. The
// focused one, and the one under the cursor, are lit.
typedef struct Row {
    float x, y, w, h;
    int id;
    bool shown, focused, hot;
} Row;

static Row row(Ui *ui, float h, bool focusable)
{
    Row r = {.x = ui->x, .y = ui->y, .w = ui->w, .h = h, .id = -1};
    ui->y += h;
    if (ui->scrolling) ui->extent = maxf(ui->extent, r.y + h + ui->m->scroll);
    r.shown = r.y >= ui->top - 0.5f && r.y + h <= ui->bottom + 0.5f;
    if (focusable) {
        r.id = nav_next(ui);
        r.focused = nav_focused(ui, r.id, r.y, h);
    }
    r.hot = r.shown && focusable && over(ui, r.x, r.y, r.w, h);
    if (r.shown && r.focused && ui->show_focus) {
        rrect(r.x, r.y + 1, r.w, h - 2, RADIUS, ACCENT_SOFT);
        rrect(r.x, r.y + 6, 2, h - 12, 1, ACCENT);
    } else if (r.hot) {
        rrect(r.x, r.y + 1, r.w, h - 2, RADIUS, HOVER);
    }
    return r;
}

static void gap(Ui *ui, float h) { ui->y += h; }

// A heading over the rows that follow: small tracked capitals, a rule after them.
static void section(Ui *ui, const char *title)
{
    Row r = row(ui, SECTION_H, false);
    if (!r.shown) return;
    float cy = r.y + r.h - 10;
    text_mid(F_SECTION, title, r.x + 2, cy, MUTED);
    float tw = width_of(F_SECTION, title);
    rule(r.x + tw + 12, r.x + r.w, cy, LINE);
}

// Where a row's control goes: on its right, half of it at most.
static float ctrl_w(const Row *r) { return clampf(r->w * 0.5f, 110, 220); }
static float ctrl_x(const Row *r) { return r->x + r->w - 8 - ctrl_w(r); }
static float ctrl_y(const Row *r) { return r->y + (r->h - CTRL_H) / 2; }

static void row_label(const Row *r, const char *text)
{
    text_fit(F_LABEL, text, r->x + 10, r->y + r->h / 2, r->w - ctrl_w(r) - 28, TEXT);
}

// --- the popups ---------------------------------------------------------------------

static void popup_close(MainMenu *m) { m->popup.kind = MAIN_POPUP_NONE; }

// Under the widget's box at `x, y` (`h` tall), or over it when there is no room below;
// on the screen either way.
static void popup_place(MainPopup *p, float game_width, float x, float y, float h)
{
    p->x = clampf(x, 8, game_width - p->w - 8);
    p->y = y + h + 3;
    if (p->y + p->h > VIEW_H - 8) p->y = y - p->h - 3;
    p->y = clampf(p->y, 8, VIEW_H - p->h - 8);
}

static void popup_fill_list(MainPopup *p, const char *const *names, const bool *locked, int count)
{
    p->count = mini(count, MAINMENU_POPUP_ITEMS);
    for (int i = 0; i < p->count; i++) {
        snprintf(p->names[i], sizeof p->names[i], "%s", names[i]);
        p->locked[i] = locked && locked[i];
    }
}

static void popup_open_list(Ui *ui, int owner, const char *const *names, const bool *locked, int count, int current,
                            float x, float y, float w)
{
    MainPopup *p = &ui->m->popup;
    *p = (MainPopup){.kind = MAIN_POPUP_LIST, .owner = owner, .w = w, .hover = current};
    popup_fill_list(p, names, locked, count);
    p->h = (float)p->count * POPUP_ROW + 8;
    popup_place(p, ui->game_width, x, y, CTRL_H);
}

// --- the colour picker --------------------------------------------------------------

// Hue in degrees [0, 360), saturation and value 0 to 1.
static void rgb_to_hsv(Rgba c, float *h, float *s, float *v)
{
    float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
    float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn;
    *v = mx;
    *s = mx > 0 ? d / mx : 0;
    if (d <= 0) return; // a grey: no hue of its own, the one held stays
    float hue = mx == r ? fmodf((g - b) / d, 6.0f) : mx == g ? (b - r) / d + 2.0f : (r - g) / d + 4.0f;
    hue *= 60.0f;
    *h = hue < 0 ? hue + 360.0f : hue;
}

static Rgba hsv_to_rgb(float h, float s, float v)
{
    h = fmodf(h, 360.0f);
    if (h < 0) h += 360.0f;
    float c = v * s, x = c * (1 - fabsf(fmodf(h / 60.0f, 2.0f) - 1)), m = v - c;
    float r = 0, g = 0, b = 0;
    switch ((int)(h / 60.0f)) {
    case 0: r = c, g = x; break;
    case 1: r = x, g = c; break;
    case 2: g = c, b = x; break;
    case 3: g = x, b = c; break;
    case 4: r = x, b = c; break;
    default: r = c, b = x; break;
    }
    return (Rgba){(uint8_t)lroundf((r + m) * 255), (uint8_t)lroundf((g + m) * 255), (uint8_t)lroundf((b + m) * 255), 255};
}

static bool same_rgb(Rgba a, Rgba b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

// A few to start from: the greys, the wheel round, and a skin and a brown.
static const Rgba SWATCHES[] = {
    {255, 255, 255, 255}, {191, 191, 191, 255}, {127, 127, 127, 255}, {0, 0, 0, 255},
    {217, 59, 43, 255},   {240, 122, 30, 255},  {242, 193, 46, 255},  {91, 191, 74, 255},
    {47, 181, 165, 255},  {58, 160, 232, 255},  {47, 79, 191, 255},   {28, 42, 107, 255},
    {122, 63, 191, 255},  {217, 79, 168, 255},  {138, 90, 53, 255},   {230, 180, 120, 255},
};
#define SWATCH_COUNT ((int)(sizeof SWATCHES / sizeof SWATCHES[0]))
#define SWATCH_COLS 8

// The picker's parts, from its top left: the heading, the square of shades of the hue
// (saturation across, value down), the hue strip beside it, the swatches, and None.
#define PICK_PAD 8.0f
#define PICK_HEAD 22.0f
#define PICK_SQUARE_W 150.0f
#define PICK_SQUARE_H 112.0f
#define PICK_HUE_W 14.0f
#define PICK_SWATCH_H 16.0f
#define PICK_SWATCH_GAP 3.0f
#define PICK_CLEAR 24.0f

typedef struct PickerParts {
    float sq_x, sq_y, hue_x, sw_y, sw_w, clear_y;
} PickerParts;

static PickerParts picker_parts(const MainPopup *p)
{
    PickerParts k;
    k.sq_x = p->x + PICK_PAD;
    k.sq_y = p->y + PICK_HEAD;
    k.hue_x = k.sq_x + PICK_SQUARE_W + PICK_PAD;
    k.sw_y = k.sq_y + PICK_SQUARE_H + PICK_PAD;
    k.sw_w = (p->w - 2 * PICK_PAD - (SWATCH_COLS - 1) * PICK_SWATCH_GAP) / SWATCH_COLS;
    k.clear_y = k.sw_y + 2 * PICK_SWATCH_H + PICK_SWATCH_GAP + PICK_PAD;
    return k;
}

static void popup_open_color(Ui *ui, int owner, const char *cvar, float x, float y)
{
    MainPopup *p = &ui->m->popup;
    *p = (MainPopup){.kind = MAIN_POPUP_COLOR, .owner = owner, .hover = -1};
    snprintf(p->cvar, sizeof p->cvar, "%s", cvar);
    const Cvar *cv = cvar_find(ui->con, cvar);
    p->clearable = cv && !cv->default_value[0];
    p->w = PICK_PAD + PICK_SQUARE_W + PICK_PAD + PICK_HUE_W + PICK_PAD;
    p->h = PICK_HEAD + PICK_SQUARE_H + PICK_PAD + 2 * PICK_SWATCH_H + PICK_SWATCH_GAP + PICK_PAD + (p->clearable ? PICK_CLEAR : 0) + 4;
    p->val = 1;
    if (!color_unset(ui->con, cvar)) rgb_to_hsv(cvar_color(ui->con, cvar), &p->hue, &p->sat, &p->val);
    popup_place(p, ui->game_width, x + 20 - p->w, y, CTRL_H);
}

static void picker_set(Ui *ui, Rgba c)
{
    char hex[8];
    snprintf(hex, sizeof hex, "%02X%02X%02X", c.r, c.g, c.b);
    cvar_set(ui->con, ui->m->popup.cvar, hex);
}

// The swatch under `c`, SWATCH_COUNT for None, -1 for neither.
static int picker_item_at(const MainPopup *p, Vec2 c)
{
    PickerParts k = picker_parts(p);
    for (int i = 0; i < SWATCH_COUNT; i++) {
        float sx = k.sq_x + (float)(i % SWATCH_COLS) * (k.sw_w + PICK_SWATCH_GAP);
        float sy = k.sw_y + (float)(i / SWATCH_COLS) * (PICK_SWATCH_H + PICK_SWATCH_GAP);
        if (inside(c, sx, sy, k.sw_w, PICK_SWATCH_H)) return i;
    }
    if (p->clearable && inside(c, k.sq_x, k.clear_y, p->w - 2 * PICK_PAD, PICK_CLEAR - 4)) return SWATCH_COUNT;
    return -1;
}

// The picker's keys and mouse: the side keys take the saturation, up and down the
// value, Q and E (the shoulders) the hue; a press on the square or the strip drags
// there; a swatch is taken at once, None closes it as it clears. The colour is set as
// it goes, so whatever shows it (the soldier) follows.
static void picker_input(Ui *ui)
{
    MainMenu *m = ui->m;
    MainPopup *p = &m->popup;
    // the colour changed under it (typed into the row's box): it follows
    if (p->drag == 0 && !color_unset(ui->con, p->cvar) && !same_rgb(cvar_color(ui->con, p->cvar), hsv_to_rgb(p->hue, p->sat, p->val)))
        rgb_to_hsv(cvar_color(ui->con, p->cvar), &p->hue, &p->sat, &p->val);

    bool changed = false;
    if (ui->side || ui->move || ui->page) {
        p->sat = clampf(p->sat + 0.05f * (float)ui->side, 0, 1);
        p->val = clampf(p->val - 0.05f * (float)ui->move, 0, 1);
        p->hue = fmodf(p->hue + 10.0f * (float)ui->page + 360.0f, 360.0f);
        changed = true;
    }
    ui->move = ui->side = ui->page = 0;
    if (ui->enter || ui->back) popup_close(m);
    ui->enter = ui->back = false;
    if (p->kind == MAIN_POPUP_NONE) return;

    PickerParts k = picker_parts(p);
    p->hover = picker_item_at(p, ui->cursor);
    if (ui->click) {
        ui->click = false;
        if (!inside(ui->cursor, p->x, p->y, p->w, p->h)) {
            popup_close(m);
            return;
        }
        if (inside(ui->cursor, k.sq_x - 4, k.sq_y - 4, PICK_SQUARE_W + 8, PICK_SQUARE_H + 8)) p->drag = 1;
        else if (inside(ui->cursor, k.hue_x - 3, k.sq_y - 4, PICK_HUE_W + 6, PICK_SQUARE_H + 8)) p->drag = 2;
        else if (p->hover >= 0 && p->hover < SWATCH_COUNT) {
            rgb_to_hsv(SWATCHES[p->hover], &p->hue, &p->sat, &p->val);
            picker_set(ui, SWATCHES[p->hover]);
        } else if (p->hover == SWATCH_COUNT) {
            cvar_set(ui->con, p->cvar, "");
            popup_close(m);
            return;
        }
    }
    if (!ui->held) p->drag = 0;
    if (p->drag == 1) {
        p->sat = clampf((ui->cursor.x - k.sq_x) / PICK_SQUARE_W, 0, 1);
        p->val = 1 - clampf((ui->cursor.y - k.sq_y) / PICK_SQUARE_H, 0, 1);
        changed = true;
    } else if (p->drag == 2) {
        p->hue = clampf((ui->cursor.y - k.sq_y) / PICK_SQUARE_H, 0, 0.9999f) * 360.0f;
        changed = true;
    }
    if (changed) picker_set(ui, hsv_to_rgb(p->hue, p->sat, p->val));
}

static void picker_draw(const Ui *ui)
{
    const MainPopup *p = &ui->m->popup;
    PickerParts k = picker_parts(p);
    bool unset = p->clearable && color_unset(ui->con, p->cvar);
    Rgba now = hsv_to_rgb(p->hue, p->sat, p->val);
    text_mid(F_SECTION, "COLOUR", k.sq_x, p->y + PICK_HEAD / 2 + 1, MUTED);
    char hex[16];
    snprintf(hex, sizeof hex, unset ? "none" : "#%02X%02X%02X", now.r, now.g, now.b);
    float hw = width_of(F_BODY, hex);
    text_mid(F_BODY, hex, p->x + p->w - PICK_PAD - hw, p->y + PICK_HEAD / 2 + 1, MUTED);
    if (!unset) rrect(p->x + p->w - PICK_PAD - hw - 18, p->y + PICK_HEAD / 2 - 5, 12, 12, 3, now);

    // the square: a grid of quads with the colour worked out at each corner, so the
    // shading is the HSV square's and not a two-triangle blend's
    enum { GRID = 12 };
    GfxVertex v[GRID * GRID * 6];
    int n = 0;
    for (int j = 0; j < GRID; j++) {
        for (int i = 0; i < GRID; i++) {
            float s0 = (float)i / GRID, s1 = (float)(i + 1) / GRID, v0 = 1 - (float)j / GRID, v1 = 1 - (float)(j + 1) / GRID;
            float x0 = k.sq_x + s0 * PICK_SQUARE_W, x1 = k.sq_x + s1 * PICK_SQUARE_W;
            float y0 = k.sq_y + (1 - v0) * PICK_SQUARE_H, y1 = k.sq_y + (1 - v1) * PICK_SQUARE_H;
            GfxVertex a = gfx_vertex(x0, y0, 0, 0, hsv_to_rgb(p->hue, s0, v0)), b = gfx_vertex(x1, y0, 0, 0, hsv_to_rgb(p->hue, s1, v0));
            GfxVertex c = gfx_vertex(x1, y1, 0, 0, hsv_to_rgb(p->hue, s1, v1)), d = gfx_vertex(x0, y1, 0, 0, hsv_to_rgb(p->hue, s0, v1));
            v[n++] = a, v[n++] = b, v[n++] = c, v[n++] = a, v[n++] = c, v[n++] = d;
        }
    }
    gfx_draw_triangles(gfx_white(), v, n);
    float mx = k.sq_x + p->sat * PICK_SQUARE_W, my = k.sq_y + (1 - p->val) * PICK_SQUARE_H;
    circle(mx, my, 6.5f, (Rgba){0, 0, 0, 120});
    circle(mx, my, 5.5f, TEXT);
    circle(mx, my, 4, now);

    // the hue strip, top to bottom round the wheel, exact at each sixth
    for (int i = 0; i < 6; i++) {
        float y0 = k.sq_y + PICK_SQUARE_H * (float)i / 6, y1 = k.sq_y + PICK_SQUARE_H * (float)(i + 1) / 6;
        Rgba c0 = hsv_to_rgb(60.0f * (float)i, 1, 1), c1 = hsv_to_rgb(60.0f * (float)(i + 1), 1, 1);
        GfxVertex q[4] = {gfx_vertex(k.hue_x, y0, 0, 0, c0), gfx_vertex(k.hue_x + PICK_HUE_W, y0, 0, 0, c0),
                          gfx_vertex(k.hue_x + PICK_HUE_W, y1, 0, 0, c1), gfx_vertex(k.hue_x, y1, 0, 0, c1)};
        gfx_draw_quad(gfx_white(), q);
    }
    float hy = k.sq_y + p->hue / 360.0f * PICK_SQUARE_H;
    rrect(k.hue_x - 3, hy - 3, PICK_HUE_W + 6, 6, 2, (Rgba){0, 0, 0, 140});
    rrect(k.hue_x - 2, hy - 2, PICK_HUE_W + 4, 4, 1.5f, TEXT);

    // the swatches
    for (int i = 0; i < SWATCH_COUNT; i++) {
        float sx = k.sq_x + (float)(i % SWATCH_COLS) * (k.sw_w + PICK_SWATCH_GAP);
        float sy = k.sw_y + (float)(i / SWATCH_COLS) * (PICK_SWATCH_H + PICK_SWATCH_GAP);
        bool current = !unset && same_rgb(SWATCHES[i], now);
        if (current || i == p->hover) rrect(sx - 1.5f, sy - 1.5f, k.sw_w + 3, PICK_SWATCH_H + 3, 4, current ? TEXT : (Rgba){255, 255, 255, 90});
        rrect(sx, sy, k.sw_w, PICK_SWATCH_H, 3, SWATCHES[i]);
    }
    if (p->clearable) {
        bool hot = p->hover == SWATCH_COUNT;
        box(k.sq_x, k.clear_y, p->w - 2 * PICK_PAD, PICK_CLEAR - 4, hot ? CONTROL_HOT : CONTROL, BORDER);
        text_mid(F_BODY, unset ? "None (the art's own)  *" : "None (the art's own)", k.sq_x + 8, k.clear_y + (PICK_CLEAR - 4) / 2,
                 hot ? TEXT : MUTED);
    }
}

// The item of an open list under the cursor, -1 for none.
static int popup_item_at(const MainPopup *p, Vec2 c)
{
    if (!inside(c, p->x, p->y + 4, p->w, (float)p->count * POPUP_ROW)) return -1;
    return (int)((c.y - p->y - 4) / POPUP_ROW);
}

static void popup_choose(Ui *ui, int item)
{
    MainMenu *m = ui->m;
    MainPopup *p = &m->popup;
    if (item < 0 || item >= p->count || p->locked[item]) return;
    m->picked_owner = p->owner;
    m->picked = item;
    popup_close(m);
}

// The open popup first: it has the keys, and the click, which on it chooses and off it
// closes it, so nothing under it takes either.
static void popup_input(Ui *ui)
{
    MainMenu *m = ui->m;
    MainPopup *p = &m->popup;
    if (p->kind == MAIN_POPUP_NONE) return;
    ui->blocked = true;
    ui->block_x = p->x, ui->block_y = p->y, ui->block_w = p->w, ui->block_h = p->h;
    if (p->kind == MAIN_POPUP_COLOR) {
        picker_input(ui);
        return;
    }

    for (int step = ui->move + ui->side, n = 0; step && n < p->count; n++) { // past the locked
        p->hover = clampi(p->hover + (step > 0 ? 1 : -1), 0, p->count - 1);
        if (!p->locked[p->hover]) break;
    }
    ui->move = ui->side = 0;
    if (ui->enter) popup_choose(ui, p->hover);
    if (ui->back) popup_close(m);
    ui->enter = ui->back = false;

    if (!m->keys_used) {
        int at = popup_item_at(p, ui->cursor);
        if (at >= 0) p->hover = at;
    }
    if (ui->click) {
        ui->click = false;
        if (inside(ui->cursor, p->x, p->y, p->w, p->h)) popup_choose(ui, popup_item_at(p, ui->cursor));
        else popup_close(m);
    }
}

static void popup_draw(const Ui *ui)
{
    const MainPopup *p = &ui->m->popup;
    if (p->kind == MAIN_POPUP_NONE) return;
    rrect(p->x + 2, p->y + 4, p->w, p->h, RADIUS + 2, (Rgba){0, 0, 0, 120}); // its shadow
    box(p->x, p->y, p->w, p->h, (Rgba){22, 26, 37, 253}, (Rgba){255, 255, 255, 34});
    if (p->kind == MAIN_POPUP_COLOR) {
        picker_draw(ui);
        return;
    }
    for (int i = 0; i < p->count; i++) {
        float y = p->y + 4 + (float)i * POPUP_ROW;
        if (i == p->hover && !p->locked[i]) rrect(p->x + 4, y, p->w - 8, POPUP_ROW, RADIUS, ACCENT_SOFT);
        char text[64];
        snprintf(text, sizeof text, "%s%s", p->names[i], p->locked[i] ? " (locked)" : "");
        text_fit(F_BODY, text, p->x + 12, y + POPUP_ROW / 2, p->w - 24, p->locked[i] ? FAINT : i == p->hover ? TEXT : MUTED);
    }
}

// --- the widgets --------------------------------------------------------------------

// A list's choice, waiting for the widget `id` that opened it: -1 if none.
static int take_picked(Ui *ui, int id)
{
    MainMenu *m = ui->m;
    if (m->picked_owner != id) return -1;
    m->picked_owner = -1;
    return m->picked;
}

// A switch: on, the accent, its knob to the right.
static void switch_draw(float x, float cy, bool on, bool hot)
{
    Rgba track = on ? (hot ? ACCENT_HOT : ACCENT) : hot ? (Rgba){70, 80, 102, 255} : TRACK;
    rrect(x, cy - 7, 30, 14, 7, track);
    circle(on ? x + 23 : x + 7, cy, 5, TEXT);
}

// A cvar of 0 and 1, the whole row a switch.
static void toggle(Ui *ui, const char *label, const char *cvar)
{
    Row r = row(ui, ROW_H, true);
    bool on = cvar_int(ui->con, cvar, 0, 1) != 0;
    bool flip = take_enter(ui, r.focused) || take_side(ui, r.focused) != 0;
    if (r.shown && take(ui, r.id, r.x, r.y, r.w, r.h)) flip = true;
    if (flip) {
        on = !on;
        cvar_set(ui->con, cvar, on ? "1" : "0");
    }
    if (!r.shown) return;
    row_label(&r, label);
    float sx = r.x + r.w - 8 - 30;
    const char *state = on ? "On" : "Off";
    text_mid(F_BODY, state, sx - 10 - width_of(F_BODY, state), r.y + r.h / 2, on ? TEXT : MUTED);
    switch_draw(sx, r.y + r.h / 2, on, r.hot || r.focused);
}

// A number cvar from `lo` to `hi` in steps of `step`, dragged along its track or stepped
// by the side keys, shown as `fmt` of it (a %d for an integer, else a %f).
static void slider(Ui *ui, const char *label, const char *cvar, float lo, float hi, float step, bool integer, const char *fmt)
{
    MainMenu *m = ui->m;
    Row r = row(ui, ROW_H, true);
    const Cvar *cv = cvar_find(ui->con, cvar);
    float v = cv ? clampf(cv->number, lo, hi) : lo;
    float cw = ctrl_w(&r), x0 = ctrl_x(&r) + 6, x1 = ctrl_x(&r) + cw - 58, cy = r.y + r.h / 2;
    float next = v;
    int side = take_side(ui, r.focused);
    if (side) next = v + (float)side * step;
    if (r.shown && take(ui, r.id, x0 - 8, r.y, x1 - x0 + 16, r.h)) m->drag = r.id;
    if (m->drag == r.id && ui->held) {
        float t = clampf((ui->cursor.x - x0) / (x1 - x0), 0, 1);
        next = lo + roundf((t * (hi - lo)) / step) * step;
    }
    next = clampf(next, lo, hi);
    if (fabsf(next - v) > step * 0.01f) {
        char number[32];
        if (integer) snprintf(number, sizeof number, "%d", (int)lroundf(next));
        else snprintf(number, sizeof number, "%.2f", next);
        cvar_set(ui->con, cvar, number);
        v = next;
    }
    if (!r.shown) return;
    row_label(&r, label);
    float t = hi > lo ? (v - lo) / (hi - lo) : 0;
    bool active = r.hot || r.focused || m->drag == r.id;
    rrect(x0, cy - 1.5f, x1 - x0, 3, 1.5f, TRACK);
    if (t > 0) rrect(x0, cy - 1.5f, (x1 - x0) * t, 3, 1.5f, active ? ACCENT_HOT : ACCENT);
    circle(x0 + (x1 - x0) * t, cy, active ? 6.0f : 5.0f, TEXT);
    char shown[32];
    if (integer) snprintf(shown, sizeof shown, fmt, (int)lroundf(v));
    else snprintf(shown, sizeof shown, fmt, (double)v);
    text_mid(F_BODY, shown, ctrl_x(&r) + cw - width_of(F_BODY, shown), cy, TEXT);
}

// A box showing names[current] that opens the list to pick from; the side keys step
// along it, past the locked. What was picked, or -1.
static int select_box(Ui *ui, const char *label, const char *const *names, const bool *locked, int count, int current)
{
    MainMenu *m = ui->m;
    Row r = row(ui, ROW_H, true);
    int picked = take_picked(ui, r.id);
    int side = take_side(ui, r.focused);
    if (side) {
        int at = current;
        for (int n = 0; n < count; n++) {
            at = (at + side + count) % count;
            if (!locked || !locked[at]) break;
        }
        if (at != current && (!locked || !locked[at])) picked = at;
    }
    float cw = ctrl_w(&r), cx = ctrl_x(&r), cy = ctrl_y(&r);
    bool open = m->popup.kind == MAIN_POPUP_LIST && m->popup.owner == r.id;
    if ((r.shown && take(ui, r.id, r.x, r.y, r.w, r.h)) || take_enter(ui, r.focused)) {
        if (open) popup_close(m);
        else popup_open_list(ui, r.id, names, locked, count, current, cx, cy, cw);
        open = !open;
    }
    if (open) popup_fill_list(&m->popup, names, locked, count); // what is locked may change with the rest
    if (!r.shown) return picked;
    row_label(&r, label);
    box(cx, cy, cw, CTRL_H, r.hot || open ? CONTROL_HOT : CONTROL, open ? with_alpha(ACCENT, 200) : BORDER);
    char text[64];
    snprintf(text, sizeof text, "%s%s", current >= 0 && current < count ? names[current] : "",
             locked && current >= 0 && current < count && locked[current] ? " (locked)" : "");
    text_fit(F_BODY, text, cx + 8, cy + CTRL_H / 2, cw - 30, TEXT);
    chevron(cx + cw - 11, cy + CTRL_H / 2, 6, !open, open ? ACCENT : MUTED);
    return picked;
}

// A cvar that takes one of `values`, each named; one that is none of them shows as the
// first. The locked (NULL for none) are shown but never set.
static void cvar_select(Ui *ui, const char *label, const char *cvar, const int *values, const char *const *names, const bool *locked,
                        int count)
{
    const Cvar *cv = cvar_find(ui->con, cvar);
    int current = 0;
    for (int i = 0; i < count; i++)
        if (cv && cv->integer == values[i]) current = i;
    int picked = select_box(ui, label, names, locked, count, current);
    if (picked >= 0 && picked < count && !(locked && locked[picked])) set_int(ui->con, cvar, values[picked]);
}

#define SERVER_DOUBLE_CLICK 0.4 // seconds between the clicks that join a row, or select a text box's text

// What a field edits: a cvar's value, or the menu's own (the taunt being edited,
// "#taunt", and the server list's search, "#search").
static const char *field_value(const Ui *ui, const char *key)
{
    if (strcmp(key, "#taunt") == 0) return ui->m->taunt_text;
    if (key[0] == '#') return ui->m->search;
    const Cvar *cv = cvar_find(ui->con, key);
    return cv ? cv->value : "";
}

static void begin_edit(Ui *ui, const char *key, int max)
{
    MainMenu *m = ui->m;
    snprintf(m->focus_cvar, sizeof m->focus_cvar, "%s", key);
    snprintf(m->edit, sizeof m->edit, "%s", field_value(ui, key));
    m->edit_max = max;
    m->edit_select_all = false; // the new edit starts unselected (the double click sets it after)
    SDL_StartTextInput();
}

// A text box over `key` (a cvar, or "#search"): its value, or what is being typed while
// it has the keyboard, the end of it when it is too long, so the caret shows. `secret`
// shows stars.
static void field_box(Ui *ui, int id, bool focused, float x, float y, float w, const char *key, int max, const char *placeholder,
                      bool secret)
{
    MainMenu *m = ui->m;
    bool typing = strcmp(m->focus_cvar, key) == 0;
    bool hot = over(ui, x, y, w, CTRL_H);
    bool clicked = take(ui, id, x, y, w, CTRL_H);
    bool entered = take_enter(ui, focused);
    if (clicked || entered) {
        begin_edit(ui, key, max);
        typing = true;
    }
    if (clicked) { // two clicks on the same box, close together: its text all selected
        if (strcmp(m->field_clicked, key) == 0 && m->time - m->field_click_at < SERVER_DOUBLE_CLICK) m->edit_select_all = true;
        snprintf(m->field_clicked, sizeof m->field_clicked, "%s", key);
        m->field_click_at = m->time;
    }
    box(x, y, w, CTRL_H, typing ? TYPING : hot ? CONTROL_HOT : CONTROL, typing ? with_alpha(ACCENT, 220) : BORDER);
    const char *value = typing ? m->edit : field_value(ui, key);
    char shown[MAINMENU_EDIT + 4];
    size_t n = strlen(value);
    if (secret) {
        n = mini((int)n, MAINMENU_EDIT);
        memset(shown, '*', n);
        shown[n] = '\0';
    } else {
        snprintf(shown, sizeof shown, "%s", value);
    }
    float room = w - 16, cy = y + CTRL_H / 2;
    if (!shown[0] && !typing) {
        if (placeholder) text_fit(F_BODY, placeholder, x + 8, cy, room, FAINT);
        return;
    }
    const char *from = shown; // the end that fits, while typing; the start, cut, otherwise
    if (typing) {
        while (*from && width_of(F_BODY, from) > room - 4) from++;
        if (m->edit_select_all && from[0]) { // the whole text highlighted: a key, or Backspace, replaces it
            float lh = line_height(F_BODY);
            rect(x + 8, cy - lh / 2, x + 8 + width_of(F_BODY, from), cy + lh / 2, with_alpha(ACCENT, 90));
        }
        text_mid(F_BODY, from, x + 8, cy, TEXT);
        if (fmod(m->time, 1.0) < 0.55) {
            float cx = x + 8 + width_of(F_BODY, from) + 1, lh = line_height(F_BODY);
            rect(cx, cy - lh / 2, cx + 1, cy + lh / 2, ACCENT);
        }
    } else {
        text_fit(F_BODY, shown, x + 8, cy, room, TEXT);
    }
}

// A row with a text box on its right: Enter, or a click on the box, types into it.
static void field_row(Ui *ui, const char *label, const char *cvar, int max, const char *placeholder, bool secret)
{
    Row r = row(ui, ROW_H, true);
    if (!r.shown) {
        take_enter(ui, r.focused); // the page scrolls to it first
        return;
    }
    row_label(&r, label);
    field_box(ui, r.id, r.focused, ctrl_x(&r), ctrl_y(&r), ctrl_w(&r), cvar, max, placeholder, secret);
}

// A colour: its hex in a box, typed, and its swatch, which opens the picker (as Enter does).
static void color_row(Ui *ui, const char *label, const char *cvar)
{
    MainMenu *m = ui->m;
    Row r = row(ui, ROW_H, true);
    float cw = ctrl_w(&r), cx = ctrl_x(&r), cy = ctrl_y(&r), sx = cx + cw - CTRL_H;
    bool open = m->popup.kind == MAIN_POPUP_COLOR && m->popup.owner == r.id;
    bool toggle_picker = take_enter(ui, r.focused);
    if (r.shown && take(ui, r.id, sx, cy, CTRL_H, CTRL_H)) toggle_picker = true;
    if (toggle_picker) {
        if (open) popup_close(m);
        else popup_open_color(ui, r.id, cvar, sx, cy);
    }
    if (!r.shown) return;
    row_label(&r, label);
    field_box(ui, r.id, false, cx, cy, cw - CTRL_H - 6, cvar, 6, "none", false);
    bool hot = over(ui, sx, cy, CTRL_H, CTRL_H) || open;
    Rgba edge = hot ? ACCENT : (Rgba){255, 255, 255, 70};
    if (color_unset(ui->con, cvar)) { // no colour: the art's own, a slash through an empty box
        box(sx, cy, CTRL_H, CTRL_H, CONTROL, edge);
        gfx_draw_line(vec2(sx + 5, cy + CTRL_H - 5), vec2(sx + CTRL_H - 5, cy + 5), 1.5f, BAD);
    } else {
        box(sx, cy, CTRL_H, CTRL_H, with_alpha(cvar_color(ui->con, cvar), 255), edge);
    }
}

// A button, `primary` in the accent; a disabled one is shown but never pressed. True
// when pressed, by a click or by Enter while it has the focus.
static bool button_at(Ui *ui, float x, float y, float w, float h, const char *caption, bool primary, bool disabled)
{
    int id = nav_next(ui);
    bool focused = nav_focused(ui, id, y, h);
    bool hot = !disabled && over(ui, x, y, w, h);
    focus_ring(ui, focused, x, y, w, h, RADIUS);
    if (disabled) box(x, y, w, h, (Rgba){24, 28, 38, 200}, (Rgba){255, 255, 255, 12});
    else if (primary) rrect(x, y, w, h, RADIUS, hot ? ACCENT_HOT : ACCENT);
    else box(x, y, w, h, hot ? CONTROL_HOT : CONTROL, hot ? (Rgba){255, 255, 255, 48} : BORDER);
    float tw = width_of(F_BUTTON, caption);
    text_mid(F_BUTTON, caption, x + (w - tw) / 2, y + h / 2, disabled ? FAINT : TEXT);
    bool pressed = take(ui, id, x, y, w, h) || take_enter(ui, focused);
    return pressed && !disabled;
}

static float button_w(const char *caption) { return maxf(width_of(F_BUTTON, caption) + 28, 80); }

// A chip that is on or off, as a filter is: a tick box and its name.
static bool chip(Ui *ui, float x, float y, const char *caption, bool on)
{
    float w = width_of(F_BODY, caption) + 30;
    int id = nav_next(ui);
    bool focused = nav_focused(ui, id, y, CTRL_H);
    bool hot = over(ui, x, y, w, CTRL_H);
    focus_ring(ui, focused, x, y, w, CTRL_H, RADIUS);
    box(x, y, w, CTRL_H, on ? ACCENT_SOFT : hot ? CONTROL_HOT : CONTROL, on ? with_alpha(ACCENT, 120) : BORDER);
    float bx = x + 8, by = y + CTRL_H / 2 - 5;
    if (on) {
        rrect(bx, by, 10, 10, 2.5f, ACCENT);
        gfx_draw_line(vec2(bx + 2.5f, by + 5), vec2(bx + 4.5f, by + 7.5f), 1.4f, TEXT);
        gfx_draw_line(vec2(bx + 4.5f, by + 7.5f), vec2(bx + 8, by + 2.5f), 1.4f, TEXT);
    } else {
        box_r(bx, by, 10, 10, 2.5f, TYPING, (Rgba){255, 255, 255, 50});
    }
    text_mid(F_BODY, caption, x + 22, y + CTRL_H / 2, on ? TEXT : MUTED);
    return take(ui, id, x, y, w, CTRL_H) || take_enter(ui, focused);
}

// The footer's line: what the page says of where it stands.
static void footer_text(const Ui *ui, float x, float w, const char *text, Rgba color)
{
    (void)ui;
    text_fit(F_BODY, text, x, ACTION_CY, w, color);
}

// The footer's button, its right edge at `right`: the page's one thing to do (Join,
// Connect, Play), in the accent when `primary`. `left` gets where it begins, for what
// goes beside it. True when pressed.
static bool big_button(Ui *ui, float right, const char *caption, bool primary, bool disabled, float *left)
{
    float w = maxf(width_of(F_BIG, caption) + 52, 130), x = right - w, y = ACTION_CY - BIG_H / 2;
    if (left) *left = x;
    int id = nav_next(ui);
    bool focused = nav_focused(ui, id, y, BIG_H);
    bool hot = !disabled && over(ui, x, y, w, BIG_H);
    focus_ring(ui, focused, x, y, w, BIG_H, RADIUS);
    if (disabled) box(x, y, w, BIG_H, (Rgba){24, 28, 38, 200}, (Rgba){255, 255, 255, 12});
    else if (primary) rrect(x, y, w, BIG_H, RADIUS, hot ? ACCENT_HOT : ACCENT);
    else box(x, y, w, BIG_H, hot ? CONTROL_HOT : CONTROL, hot ? (Rgba){255, 255, 255, 48} : BORDER);
    float tw = width_of(F_BIG, caption);
    text_mid(F_BIG, caption, x + (w - tw) / 2, ACTION_CY, disabled ? FAINT : TEXT);
    bool pressed = take(ui, id, x, y, w, BIG_H) || take_enter(ui, focused);
    return pressed && !disabled;
}

static void go_page(MainMenu *m, MainPage page);
static void unfocus(MainMenu *m);

// --- direct connect -----------------------------------------------------------------

static void page_join(Ui *ui, const char *status, bool joined)
{
    float full_w = ui->w;
    ui->w = minf(full_w, 520); // the fields near their names
    section(ui, "SERVER");
    field_row(ui, "Address", "cl_server", 63, "host:port", false);
    field_row(ui, "Password", "cl_password", NET_PASSWORD_SIZE - 1, "if the server asks one", true);
    gap(ui, 6);
    float tip_y = ui->y;
    if (tip_y + 40 < ui->bottom)
        text_wrap(F_BODY, "A server's address is its IP or name and its port, as 192.168.1.20:23073. Ctrl+V pastes.", ui->x + 12,
                  tip_y, ui->w - 24, MUTED);

    ui->w = full_w;
    float bx = ui->x + ui->w;
    ui->scrolling = false;
    if (!joined) {
        if (big_button(ui, ui->x + ui->w, "CONNECT", true, false, &bx)) {
            const Cvar *cv = cvar_find(ui->con, "cl_server");
            snprintf(ui->m->command, sizeof ui->m->command, "connect %s", cv && cv->value[0] ? cv->value : "127.0.0.1");
            ui->m->connect_asked = true;
        }
    } else if (big_button(ui, ui->x + ui->w, "DISCONNECT", false, false, &bx)) {
        snprintf(ui->m->command, sizeof ui->m->command, "disconnect");
    }
    if (ui->m->connect_asked && status && status[0]) footer_text(ui, ui->x, bx - ui->x - 12, status, MUTED);
}

// --- the servers --------------------------------------------------------------------

#define SERVER_ROW 20.0f

static bool same_address(const QueryAddress *a, const QueryAddress *b)
{
    return a->port == b->port && strcmp(a->ip, b->ip) == 0;
}

static bool joinable(const BrowserServer *s) { return s->info.protocol == NET_VERSION; }

static const char *mode_name(uint8_t mode)
{
    switch (mode) {
    case MATCH_DEATHMATCH: return "DM";
    case MATCH_CTF: return "CTF";
    default: return "?";
    }
}

static const char *server_name(const BrowserServer *s) { return s->info.hostname[0] ? s->info.hostname : s->address.ip; }

// Whether `needle` is in `hay`, any case.
static bool contains(const char *hay, const char *needle)
{
    if (!needle[0]) return true;
    for (; *hay; hay++) {
        size_t i = 0;
        while (needle[i] && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i])) i++;
        if (!needle[i]) return true;
    }
    return false;
}

static int ci_compare(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d) return d;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

// Whether the list puts `a` before `b`: those this game can join first, then by the
// column, each in its natural order (names A to Z, the fullest first, the nearest
// first) unless turned; the fuller, then the nearer, after that.
static bool server_before(const MainMenu *m, const BrowserServer *a, const BrowserServer *b)
{
    if (joinable(a) != joinable(b)) return joinable(a);
    int pa = a->info.players + a->info.bots, pb = b->info.players + b->info.bots;
    int d = 0;
    switch (m->server_sort) {
    case SERVER_SORT_NAME: d = ci_compare(server_name(a), server_name(b)); break;
    case SERVER_SORT_MODE: d = (int)a->info.mode - (int)b->info.mode; break;
    case SERVER_SORT_MAP: d = ci_compare(a->info.map, b->info.map); break;
    case SERVER_SORT_PLAYERS: d = pb - pa; break;
    case SERVER_SORT_PING: d = a->ping - b->ping; break;
    }
    if (m->server_sort_up) d = -d;
    if (d) return d < 0;
    if (pa != pb) return pa > pb;
    return a->ping < b->ping;
}

static bool server_shown(const MainMenu *m, const BrowserServer *s)
{
    if (!s->answered) return false;
    int players = s->info.players + s->info.bots;
    if (m->hide_empty && s->info.players == 0) return false;
    if (m->hide_full && players >= s->info.max_players) return false;
    if (m->only_compatible && !joinable(s)) return false;
    return contains(server_name(s), m->search) || contains(s->info.map, m->search);
}

// Off to `s`: at once, or by Direct Connect with its address filled in when it asks a
// password, which is typed there.
static void join_server(Ui *ui, const BrowserServer *s)
{
    char address[32];
    snprintf(address, sizeof address, "%s:%u", s->address.ip, s->address.port);
    cvar_set(ui->con, "cl_server", address);
    if (!s->info.password) {
        snprintf(ui->m->command, sizeof ui->m->command, "connect %s", address);
        ui->m->connect_asked = true;
        return;
    }
    MainMenu *m = ui->m;
    go_page(m, MAIN_JOIN);
    m->zone = MAIN_ZONE_CONTENT;
    m->nav = 1; // the password
    begin_edit(ui, "cl_password", NET_PASSWORD_SIZE - 1);
}

static Rgba ping_color(int ping) { return ping < 80 ? GOOD : ping < 160 ? WARN : BAD; }

// Something is being waited for: a ring of dots at `r` from `x, y`, lit one after another.
static void spinner(float x, float y, float r, double time)
{
    enum { DOTS = 8 };
    float t = (float)fmod(time * 1.2, 1.0);
    for (int i = 0; i < DOTS; i++) {
        float a = 2 * (float)M_PI * (float)i / DOTS, phase = fmodf((float)i / DOTS - t + 1.0f, 1.0f);
        uint8_t alpha = (uint8_t)(40 + 215 * (1.0f - phase));
        circle(x + r * sinf(a), y - r * cosf(a), 1.7f, with_alpha(MUTED, alpha));
    }
}

// A column's heading, which sorts by it; a second click turns the order.
static void column_head(Ui *ui, float x, float y, float w, const char *title, ServerSort sort)
{
    MainMenu *m = ui->m;
    bool sorted = m->server_sort == sort, hot = over(ui, x, y, w, 18);
    text_mid(F_SECTION, title, x, y + 9, sorted ? TEXT : hot ? MUTED : FAINT);
    if (sorted) {
        // pointing down while the larger come first: the players' natural order, the
        // others' turned
        bool down = sort == SERVER_SORT_PLAYERS ? !m->server_sort_up : m->server_sort_up;
        chevron(x + width_of(F_SECTION, title) + 8, y + 9, 5, down, ACCENT);
    }
    if (take(ui, -1, x, y, w, 18)) {
        if (sorted) m->server_sort_up = !m->server_sort_up;
        else m->server_sort = sort, m->server_sort_up = false;
    }
}

// The lobby's servers that answered, a row each: name, mode, map, players and ping, under
// a search and the filters. A click picks one, a second joins it, as Join does; the wheel
// scrolls, and with the focus on the list the arrows pick and Enter joins. Below, what
// the one picked is, and the buttons.
static void page_servers(Ui *ui, const Browser *b)
{
    MainMenu *m = ui->m;
    float x = ui->x, w = ui->w, y = BODY_TOP;
    ui->scrolling = false; // the list scrolls itself

    // the search and the filters
    float sw = clampf(w * 0.34f, 120, 220);
    {
        int id = nav_next(ui);
        bool focused = nav_focused(ui, id, y, CTRL_H);
        focus_ring(ui, focused, x, y, sw, CTRL_H, RADIUS);
        field_box(ui, id, focused, x, y, sw, "#search", MAINMENU_SEARCH - 1, "Search by name or map", false);
    }
    float cx = x + sw + 10;
    if (chip(ui, cx, y, "Not empty", m->hide_empty)) m->hide_empty = !m->hide_empty;
    cx += width_of(F_BODY, "Not empty") + 30 + 6;
    if (chip(ui, cx, y, "Not full", m->hide_full)) m->hide_full = !m->hide_full;
    cx += width_of(F_BODY, "Not full") + 30 + 6;
    float rw = button_w("Refresh");
    if (cx + width_of(F_BODY, "Compatible") + 30 <= x + w - rw - 10)
        if (chip(ui, cx, y, "Compatible", m->only_compatible)) m->only_compatible = !m->only_compatible;
    if (button_at(ui, x + w - rw, y, rw, CTRL_H, "Refresh", false, b->state == BROWSER_FETCHING || b->state == BROWSER_QUERYING))
        snprintf(m->command, sizeof m->command, "browse");
    y += CTRL_H + 12;

    // the columns, from the right: ping, players, map, mode; the name has the rest
    float ping_x = x + w - 46, players_x = ping_x - 86, map_x = players_x - clampf(w * 0.24f, 80, 170), mode_x = map_x - 56;
    column_head(ui, x + 22, y, mode_x - x - 30, "SERVER", SERVER_SORT_NAME);
    column_head(ui, mode_x, y, 40, "MODE", SERVER_SORT_MODE);
    column_head(ui, map_x, y, players_x - map_x - 6, "MAP", SERVER_SORT_MAP);
    column_head(ui, players_x, y, 56, "PLAYERS", SERVER_SORT_PLAYERS);
    column_head(ui, ping_x, y, 40, "PING", SERVER_SORT_PING);
    y += 20;

    int order[BROWSER_MAX], n = 0;
    for (int i = 0; i < b->count; i++) {
        if (!server_shown(m, &b->servers[i])) continue;
        int at = n++;
        while (at > 0 && server_before(m, &b->servers[i], &b->servers[order[at - 1]])) {
            order[at] = order[at - 1];
            at--;
        }
        order[at] = i;
    }
    int picked_at = -1;
    for (int i = 0; i < n; i++)
        if (same_address(&b->servers[order[i]].address, &m->server_selected)) picked_at = i;

    float list_bottom = ui->bottom, h = list_bottom - y;
    int rows = maxi((int)(h / SERVER_ROW), 1);
    int list_id = nav_next(ui);
    bool focused = nav_focused(ui, list_id, y, h);
    if (focused && ui->move) { // the arrows pick along the list, and past its ends leave it
        int to = picked_at < 0 ? (ui->move > 0 ? 0 : -1) : picked_at + ui->move;
        if (to >= 0 && to < n) {
            picked_at = to;
            m->server_selected = b->servers[order[to]].address;
            ui->move = 0;
            if (to < m->server_scroll) m->server_scroll = to;
            if (to >= m->server_scroll + rows) m->server_scroll = to - rows + 1;
        }
    }
    if (over(ui, x, y, w, h) && m->wheel) {
        m->server_scroll -= m->wheel * 3;
        m->wheel = 0;
    }
    m->server_scroll = clampi(m->server_scroll, 0, maxi(n - rows, 0));
    if (n > rows) { // its bar, dragged: before the rows, so the press is the bar's
        float track = h - 8, knob = maxf(track * (float)rows / (float)n, 10);
        float pos = scroll_take(ui, SCROLL_SERVERS, x + w - 6, y + 4, track, knob, (float)m->server_scroll / (float)(n - rows));
        m->server_scroll = clampi((int)lroundf(pos * (float)(n - rows)), 0, n - rows);
    }
    focus_ring(ui, focused, x, y, w, h, RADIUS);
    box(x, y, w, h, WELL, LINE);
    const BrowserServer *selected = picked_at >= 0 ? &b->servers[order[picked_at]] : NULL;
    for (int row_at = 0; row_at < rows && m->server_scroll + row_at < n; row_at++) {
        int i = m->server_scroll + row_at;
        const BrowserServer *s = &b->servers[order[i]];
        float ry = y + 2 + (float)row_at * SERVER_ROW, cy = ry + SERVER_ROW / 2;
        bool picked = i == picked_at, can = joinable(s), hot = over(ui, x, ry, w, SERVER_ROW);
        if (picked) {
            rrect(x + 2, ry, w - 4, SERVER_ROW, RADIUS, ACCENT_SOFT);
            rrect(x + 2, ry + 4, 2, SERVER_ROW - 8, 1, ACCENT);
        } else if (hot) {
            rrect(x + 2, ry, w - 4, SERVER_ROW, RADIUS, HOVER);
        }
        Rgba color = can ? TEXT : FAINT, soft = can ? MUTED : FAINT;
        if (s->info.password) padlock(x + 10, cy - 5, soft);
        text_fit(F_BODY, server_name(s), x + 22, cy, mode_x - x - 30, color);
        text_mid(F_BODY, mode_name(s->info.mode), mode_x, cy, soft);
        text_fit(F_BODY, s->info.map, map_x, cy, players_x - map_x - 8, soft);
        char text[32];
        int players = s->info.players + s->info.bots;
        if (can) snprintf(text, sizeof text, "%d/%d", players, s->info.max_players);
        else snprintf(text, sizeof text, "v%u", s->info.protocol); // another version's server: not joinable from here
        text_mid(F_BODY, text, players_x, cy, !can ? FAINT : players >= s->info.max_players ? WARN : players == 0 ? MUTED : TEXT);
        snprintf(text, sizeof text, "%d", s->ping);
        text_mid(F_BODY, text, ping_x, cy, can ? ping_color(s->ping) : FAINT);
        if (take(ui, list_id, x, ry, w, SERVER_ROW)) {
            if (picked && can && m->time - m->server_clicked_at < SERVER_DOUBLE_CLICK) join_server(ui, s);
            m->server_selected = s->address;
            m->server_clicked_at = m->time;
            selected = s;
        }
    }
    if (n > rows) { // where in the list this is
        float track = h - 8, knob = maxf(track * (float)rows / (float)n, 10);
        scroll_draw(ui, SCROLL_SERVERS, x + w - 6, y + 4, track, knob, (float)m->server_scroll / (float)(n - rows));
    }

    // what the list is waiting on, in the list while it has nothing to show
    char status[192];
    switch (b->state) {
    case BROWSER_IDLE:
    case BROWSER_FETCHING: snprintf(status, sizeof status, "Asking the lobby for servers..."); break;
    case BROWSER_QUERYING: snprintf(status, sizeof status, "%d of %d servers answered...", b->answered, b->count); break;
    case BROWSER_FAILED: snprintf(status, sizeof status, "The lobby can't be reached: %s", b->error); break;
    case BROWSER_DONE:
        if (b->count == 0) snprintf(status, sizeof status, "No servers are listed right now. Host one with Local Play.");
        else if (n == 0 && b->answered > 0) snprintf(status, sizeof status, "No server matches the search and the filters.");
        else if (n == 0) snprintf(status, sizeof status, "%d listed, and none answered.", b->count);
        else snprintf(status, sizeof status, "%d server%s", n, n == 1 ? "" : "s");
        break;
    }
    if (n == 0) { // the list's empty state: what it is waiting on, with a spinner while it waits
        bool waiting = b->state == BROWSER_IDLE || b->state == BROWSER_FETCHING || b->state == BROWSER_QUERYING;
        char cut[192];
        fit(F_BODY, cut, sizeof cut, status, w - 40);
        float cy = y + h / 2 + (waiting ? 10 : 0);
        text_mid(F_BODY, cut, x + (w - width_of(F_BODY, cut)) / 2, cy, b->state == BROWSER_FAILED ? WARN : MUTED);
        if (waiting) spinner(x + w / 2, cy - 24, 9, m->time);
    }
    if (focused && ui->enter) {
        ui->enter = false;
        if (selected && joinable(selected)) join_server(ui, selected);
    }

    // the action bar: the one picked, and Join
    float jx = x + w;
    if (big_button(ui, x + w, "JOIN", true, !selected || !joinable(selected), &jx)) join_server(ui, selected);
    float tw = jx - x - 16;
    if (selected) {
        float top = ACTION_CY;
        text_fit(F_LABEL, server_name(selected), x, top - 7, tw, TEXT);
        char line[192];
        if (joinable(selected))
            snprintf(line, sizeof line, "%s:%u  -  %s on %s  -  %d players, %d bots, %d slots  -  %d ms%s", selected->address.ip,
                     selected->address.port, mode_name(selected->info.mode), selected->info.map, selected->info.players,
                     selected->info.bots, selected->info.max_players, selected->ping, selected->info.password ? "  -  password" : "");
        else
            snprintf(line, sizeof line, "Runs another version of the game (v%u; this is v%u).", selected->info.protocol, NET_VERSION);
        text_fit(F_BODY, line, x, top + 9, tw, joinable(selected) ? MUTED : WARN);
    } else if (n) {
        footer_text(ui, x, tw, "Pick a server, or double-click one to join it.", MUTED);
    }
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

static int rotation_count(const char *list)
{
    int n = 0;
    for (const char *p = list; *p;) {
        while (*p == ' ' || *p == ',' || *p == '\t') p++;
        if (!*p) break;
        while (*p && *p != ' ' && *p != ',' && *p != '\t') p++;
        n++;
    }
    return n;
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

#define MAP_ROW 18.0f

// The maps under assets, each a row with a box to tick it into the rotation or out of
// it, numbered in the order the rounds will go; the wheel pages through them, and with
// the focus on the list the arrows move along it and Enter ticks.
static void map_list(Ui *ui, float x, float y, float w, float h, const char (*maps)[64], int count)
{
    MainMenu *m = ui->m;
    int rows = maxi((int)((h - 4) / MAP_ROW), 1);
    int id = nav_next(ui);
    bool focused = nav_focused(ui, id, y, h);
    if (focused && ui->move) {
        int to = m->map_cursor + ui->move;
        if (to >= 0 && to < count) {
            m->map_cursor = to;
            ui->move = 0;
            if (to < m->map_scroll) m->map_scroll = to;
            if (to >= m->map_scroll + rows) m->map_scroll = to - rows + 1;
        }
    }
    m->map_cursor = clampi(m->map_cursor, 0, maxi(count - 1, 0));
    if (focused && ui->enter && count > 0) {
        ui->enter = false;
        rotation_toggle(ui->con, maps[m->map_cursor]);
    }
    if (over(ui, x, y, w, h) && m->wheel) {
        m->map_scroll -= m->wheel * 3;
        m->wheel = 0;
    }
    m->map_scroll = clampi(m->map_scroll, 0, maxi(count - rows, 0));
    if (count > rows) { // its bar, dragged: before the rows, so the press is the bar's
        float track = h - 8, knob = maxf(track * (float)rows / (float)count, 10);
        float pos = scroll_take(ui, SCROLL_MAPS, x + w - 6, y + 4, track, knob, (float)m->map_scroll / (float)(count - rows));
        m->map_scroll = clampi((int)lroundf(pos * (float)(count - rows)), 0, count - rows);
    }
    focus_ring(ui, focused, x, y, w, h, RADIUS);
    box(x, y, w, h, WELL, LINE);
    const Cvar *cv = cvar_find(ui->con, "sv_maps");
    const char *list = cv ? cv->value : "";
    for (int row_at = 0; row_at < rows; row_at++) {
        int i = m->map_scroll + row_at;
        if (i >= count) break;
        float ry = y + 2 + (float)row_at * MAP_ROW, cy = ry + MAP_ROW / 2;
        int at = rotation_index(list, maps[i]);
        bool hot = over(ui, x, ry, w, MAP_ROW), cursor = focused && ui->show_focus && i == m->map_cursor;
        if (cursor) rrect(x + 2, ry, w - 4, MAP_ROW, RADIUS, ACCENT_SOFT);
        else if (hot) rrect(x + 2, ry, w - 4, MAP_ROW, RADIUS, HOVER);
        // the tick box, the accent when in the rotation
        if (at >= 0) {
            rrect(x + 8, cy - 6, 12, 12, 3, ACCENT);
            gfx_draw_line(vec2(x + 10.5f, cy), vec2(x + 13, cy + 3), 1.6f, TEXT);
            gfx_draw_line(vec2(x + 13, cy + 3), vec2(x + 18, cy - 3), 1.6f, TEXT);
        } else {
            box_r(x + 8, cy - 6, 12, 12, 3, TYPING, (Rgba){255, 255, 255, 50});
        }
        text_fit(F_BODY, maps[i], x + 28, cy, w - 60, at >= 0 ? TEXT : MUTED);
        if (at >= 0) {
            char place[8];
            snprintf(place, sizeof place, "%d", at + 1);
            text_mid(F_BOLD, place, x + w - 14 - width_of(F_BOLD, place), cy, ACCENT);
        }
        if (take(ui, id, x, ry, w, MAP_ROW)) {
            m->map_cursor = i;
            rotation_toggle(ui->con, maps[i]);
        }
    }
    if (count > rows) {
        float track = h - 8, knob = maxf(track * (float)rows / (float)count, 10);
        scroll_draw(ui, SCROLL_MAPS, x + w - 6, y + 4, track, knob, (float)m->map_scroll / (float)(count - rows));
    }
}

static void page_local(Ui *ui, const char *status, bool hosting, const char (*maps)[64], int count)
{
    MainMenu *m = ui->m;
    static const int MODES[] = {0, 1, 2};
    static const char *const MODE_NAMES[] = {"The map's own", "Deathmatch", "Capture the Flag"};
    static const int SKILLS[] = {300, 200, 100, 50, 10};
    static const char *const SKILL_NAMES[] = {"Stupid", "Poor", "Normal", "Hard", "Impossible"};

    // the settings on the left, scrolling; the maps on the right, standing
    float x = ui->x, w = ui->w, list_w = clampf(w * 0.4f, 160, 260);
    ui->w = w - list_w - 20;
    section(ui, "MATCH");
    cvar_select(ui, "Mode", "sv_gamemode", MODES, MODE_NAMES, NULL, 3);
    slider(ui, "Time limit", "sv_timelimit", 5, 60, 5, true, "%d min");
    slider(ui, "Score limit", "sv_killlimit", 5, 100, 5, true, "%d");
    section(ui, "BOTS");
    slider(ui, "Deathmatch", "bots_random_noteam", 0, 15, 1, true, "%d");
    slider(ui, "Alpha team", "bots_random_alpha", 0, 15, 1, true, "%d");
    slider(ui, "Bravo team", "bots_random_bravo", 0, 15, 1, true, "%d");
    cvar_select(ui, "Skill", "bots_difficulty", SKILLS, SKILL_NAMES, NULL, 5);
    toggle(ui, "Bots chat", "bots_chat");
    section(ui, "SERVER");
    toggle(ui, "Rope", "sv_rope");
    field_row(ui, "Port", "sv_port", 5, "23073", false);
    ui->w = w;

    bool scrolling = ui->scrolling;
    ui->scrolling = false;
    float lx = x + w - list_w, ly = BODY_TOP;
    const Cvar *cv = cvar_find(ui->con, "sv_maps");
    const char *list = cv ? cv->value : "";
    int chosen = rotation_count(list);
    text_mid(F_SECTION, "MAP ROTATION", lx + 2, ly + SECTION_H - 10, MUTED);
    char counted[32];
    snprintf(counted, sizeof counted, chosen ? "%d chosen" : "none chosen", chosen);
    text_mid(F_TINY, counted, lx + list_w - 4 - width_of(F_TINY, counted), ly + SECTION_H - 10, FAINT);
    ly += SECTION_H + 2;
    float lh = ui->bottom - ly - 18;
    map_list(ui, lx, ly, list_w, lh, maps, count);
    text_fit(F_BODY, chosen ? "Ticked maps play in this order." : "None ticked: the current map repeats.", lx + 2,
             ly + lh + 10, list_w - 4, FAINT);

    float bx = x + w;
    if (big_button(ui, x + w, hosting ? "STOP" : "PLAY", !hosting, false, &bx))
        snprintf(m->command, sizeof m->command, hosting ? "disconnect" : "host");
    char hint[160];
    if (status && status[0] && hosting) snprintf(hint, sizeof hint, "%s", status);
    else snprintf(hint, sizeof hint, "Friends can join at your address, port %d.", cvar_int(ui->con, "sv_port", 0, 65535));
    footer_text(ui, x, bx - x - 12, hint, MUTED);
    ui->scrolling = scrolling;
}

// --- the demos ----------------------------------------------------------------------

#define DEMO_ROW 20.0f

static void play_demo(Ui *ui, const DemoListing *d)
{
    snprintf(ui->m->command, sizeof ui->m->command, "playdemo \"%s\"", d->name);
}

// A demo's length as minutes and seconds; a file cut short doesn't say its own.
static void demo_length(char *out, size_t size, const DemoHeader *h)
{
    uint32_t s = h->ticks / TICK_RATE;
    if (h->ticks == 0) snprintf(out, size, "-");
    else snprintf(out, size, "%u:%02u", s / 60, s % 60);
}

static void demo_date(char *out, size_t size, const DemoHeader *h)
{
    time_t when = (time_t)h->date;
    struct tm *t = localtime(&when);
    if (!t || !strftime(out, size, "%Y-%m-%d %H:%M", t)) snprintf(out, size, "-");
}

// The demos in demos/, newest first, a row each: its name, map, recorder, length and
// date. A click picks one, a second plays it, as Play does; the wheel scrolls, and with
// the focus on the list the arrows pick and Enter plays. Above, whether every game is
// recorded (demo_autorecord); below, the one picked, and Play.
static void page_demos(Ui *ui, const DemoListing *demos, int n)
{
    MainMenu *m = ui->m;
    float x = ui->x, w = ui->w, y = BODY_TOP;
    ui->scrolling = false; // the list scrolls itself

    bool autorecord = cvar_int(ui->con, "demo_autorecord", 0, 1) != 0;
    if (chip(ui, x, y, "Record every game", autorecord)) set_int(ui->con, "demo_autorecord", !autorecord);
    char counted[32];
    snprintf(counted, sizeof counted, n == 1 ? "1 demo" : "%d demos", n);
    text_mid(F_BODY, counted, x + w - width_of(F_BODY, counted), y + CTRL_H / 2, FAINT);
    y += CTRL_H + 12;

    // the columns, from the right: date, length, player, map; the name has the rest
    float date_x = x + w - 108, length_x = date_x - 66, player_x = length_x - clampf(w * 0.18f, 70, 120),
          map_x = player_x - clampf(w * 0.2f, 70, 140);
    text_mid(F_SECTION, "NAME", x + 12, y + 9, FAINT);
    text_mid(F_SECTION, "MAP", map_x, y + 9, FAINT);
    text_mid(F_SECTION, "PLAYER", player_x, y + 9, FAINT);
    text_mid(F_SECTION, "LENGTH", length_x, y + 9, FAINT);
    text_mid(F_SECTION, "RECORDED", date_x, y + 9, FAINT);
    y += 20;

    int picked_at = -1;
    for (int i = 0; i < n; i++)
        if (strcmp(demos[i].name, m->demo_selected) == 0) picked_at = i;

    float h = ui->bottom - y;
    int rows = maxi((int)(h / DEMO_ROW), 1);
    int list_id = nav_next(ui);
    bool focused = nav_focused(ui, list_id, y, h);
    if (focused && ui->move) { // the arrows pick along the list, and past its ends leave it
        int to = picked_at < 0 ? (ui->move > 0 ? 0 : -1) : picked_at + ui->move;
        if (to >= 0 && to < n) {
            picked_at = to;
            snprintf(m->demo_selected, sizeof m->demo_selected, "%s", demos[to].name);
            ui->move = 0;
            if (to < m->demo_scroll) m->demo_scroll = to;
            if (to >= m->demo_scroll + rows) m->demo_scroll = to - rows + 1;
        }
    }
    if (over(ui, x, y, w, h) && m->wheel) {
        m->demo_scroll -= m->wheel * 3;
        m->wheel = 0;
    }
    m->demo_scroll = clampi(m->demo_scroll, 0, maxi(n - rows, 0));
    if (n > rows) { // its bar, dragged: before the rows, so the press is the bar's
        float track = h - 8, knob = maxf(track * (float)rows / (float)n, 10);
        float pos = scroll_take(ui, SCROLL_DEMOS, x + w - 6, y + 4, track, knob, (float)m->demo_scroll / (float)(n - rows));
        m->demo_scroll = clampi((int)lroundf(pos * (float)(n - rows)), 0, n - rows);
    }
    focus_ring(ui, focused, x, y, w, h, RADIUS);
    box(x, y, w, h, WELL, LINE);
    const DemoListing *selected = picked_at >= 0 ? &demos[picked_at] : NULL;
    for (int row_at = 0; row_at < rows && m->demo_scroll + row_at < n; row_at++) {
        int i = m->demo_scroll + row_at;
        const DemoListing *d = &demos[i];
        float ry = y + 2 + (float)row_at * DEMO_ROW, cy = ry + DEMO_ROW / 2;
        bool picked = i == picked_at, hot = over(ui, x, ry, w, DEMO_ROW);
        if (picked) {
            rrect(x + 2, ry, w - 4, DEMO_ROW, RADIUS, ACCENT_SOFT);
            rrect(x + 2, ry + 4, 2, DEMO_ROW - 8, 1, ACCENT);
        } else if (hot) {
            rrect(x + 2, ry, w - 4, DEMO_ROW, RADIUS, HOVER);
        }
        char text[32];
        text_fit(F_BODY, d->name, x + 12, cy, map_x - x - 20, TEXT);
        text_fit(F_BODY, d->header.map, map_x, cy, player_x - map_x - 8, MUTED);
        text_fit(F_BODY, d->header.name, player_x, cy, length_x - player_x - 8, MUTED);
        demo_length(text, sizeof text, &d->header);
        text_mid(F_BODY, text, length_x, cy, MUTED);
        demo_date(text, sizeof text, &d->header);
        text_fit(F_BODY, text, date_x, cy, x + w - date_x - 8, MUTED);
        if (take(ui, list_id, x, ry, w, DEMO_ROW)) {
            if (picked && m->time - m->demo_clicked_at < SERVER_DOUBLE_CLICK) play_demo(ui, d);
            snprintf(m->demo_selected, sizeof m->demo_selected, "%s", d->name);
            m->demo_clicked_at = m->time;
            selected = d;
        }
    }
    if (n > rows) { // where in the list this is
        float track = h - 8, knob = maxf(track * (float)rows / (float)n, 10);
        scroll_draw(ui, SCROLL_DEMOS, x + w - 6, y + 4, track, knob, (float)m->demo_scroll / (float)(n - rows));
    }
    if (n == 0) { // the list's empty state: how to make one
        const char *none = "No demos yet. Type record in the console during a game, or record every game.";
        char cut[128];
        fit(F_BODY, cut, sizeof cut, none, w - 40);
        text_mid(F_BODY, cut, x + (w - width_of(F_BODY, cut)) / 2, y + h / 2, MUTED);
    }
    if (focused && ui->enter) {
        ui->enter = false;
        if (selected) play_demo(ui, selected);
    }

    // the action bar: the one picked, and Play
    float px = x + w;
    if (big_button(ui, x + w, "PLAY", true, !selected, &px)) play_demo(ui, selected);
    float tw = px - x - 16;
    if (selected) {
        char length[16], date[32], line[192];
        demo_length(length, sizeof length, &selected->header);
        demo_date(date, sizeof date, &selected->header);
        text_fit(F_LABEL, selected->name, x, ACTION_CY - 7, tw, TEXT);
        snprintf(line, sizeof line, "%s on %s  -  %s  -  %s", selected->header.name, selected->header.map, length, date);
        text_fit(F_BODY, line, x, ACTION_CY + 9, tw, MUTED);
    } else if (n) {
        footer_text(ui, x, tw, "Pick a demo, or double-click one to play it. The arrows skip ten seconds as it plays.", MUTED);
    }
}

// --- the player ---------------------------------------------------------------------

static const char *const HAIR_STYLES[] = {"Army", "Dreadlocks", "Punk", "Mr. T", "Normal", "Fringe", "Bob"};
static const char *const HEAD_STYLES[] = {"None", "Helmet", "Hat", "Waifu helmet"};
static const char *const CHAIN_STYLES[] = {"None", "Dog tags", "Gold chain"};
static const char *const SECONDARIES[] = {"USSOCOM", "Combat Knife", "Chainsaw", "LAW"};

// The gostek as the cvars dress it, standing, at `at` in the menu's units, `scale`
// times its size in the world. Drawn from the art of the style the cvars choose.
static void preview(Ui *ui, const Gostek *gostek, const Context *ctx, Vec2 at, float scale)
{
    if (!gostek || !ctx || !ctx->anims) return;
    const Anims *anims = ctx->anims;
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
        .grenades = 1,
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
    // the chain's and the dreadlocks' points, at rest, as soldier_swing would settle them
    // on a soldier standing still: each end hanging straight down from its anchor by its
    // constraint's length. Left at nothing, they drew the hair between the feet.
    {
        const Pose *p = &rs.pose;
        rs.swing[0] = p->p[8];
        rs.swing[2] = vec2_add(p->p[8], vec2_scale(vec2_sub(p->p[11], p->p[8]), 50.0f));
        for (int k = 0; k < 2; k++) {
            float rest = 0;
            if (ctx->skeletons) {
                const ParticleObject *sk = &ctx->skeletons->gostek;
                int c = sk->constraint_count - 2 + k; // 22 to 21, then 24 to 23
                if (c >= 0) rest = vec2_length(vec2_sub(sk->points[sk->constraints[c][1]], sk->points[sk->constraints[c][0]]));
            }
            rs.swing[2 * k + 1] = vec2_add(rs.swing[2 * k], vec2(0, rest));
        }
    }
    // the world's origin lands on `at`, the world `scale` times larger than the units
    gfx_transform(mat3_ortho(-at.x / scale, (ui->game_width - at.x) / scale, -at.y / scale, (VIEW_H - at.y) / scale));
    // the belt's grenades as the game will draw them: their art while no colour is set
    const Cvar *nades = cvar_find(con, "cl_grenade_color");
    Rgba nade_color = {0};
    if (nades && rgba_parse_hex(nades->value, &nade_color)) nade_color.a = 255;
    else nade_color = (Rgba){0};
    gostek_draw(gostek, &rs, false, nade_color);
    gfx_transform(mat3_ortho(0, ui->game_width, 0, VIEW_H));
    text_pixel_ratio(vec2(ui->pixel, ui->pixel));
}

static void page_player(Ui *ui, const Gostek *gostek, const Context *ctx)
{
    float x = ui->x, w = ui->w, pw = clampf(w * 0.34f, 150, 220);
    ui->w = w - pw - 20;
    int style = cvar_int(ui->con, "cl_player_style", 0, GOSTEK_STYLE_COUNT - 1);
    bool plain = style == GOSTEK_STYLE_RAT || style == GOSTEK_STYLE_FURRY; // they wear only some hair, and no headgear

    section(ui, "IDENTITY");
    field_row(ui, "Name", "cl_player_name", NET_NAME_SIZE - 1, "Major", false);
    section(ui, "LOOK");
    {
        static const int STYLES[] = {GOSTEK_STYLE_MALE, GOSTEK_STYLE_FEMALE, GOSTEK_STYLE_WAIFU, GOSTEK_STYLE_RAT, GOSTEK_STYLE_FURRY};
        static const char *const STYLE_NAMES[] = {"Male", "Female", "Waifu", "Rat", "Furry"};
        cvar_select(ui, "Style", "cl_player_style", STYLES, STYLE_NAMES, NULL, 5);
        static const int HAIR_VALUES[] = {0, 1, 2, 3, 4, 5, 6};
        // the rat and the furry wear only army, punk and Mr. T; everyone else may wear all six
        static const bool RAT_HAIR_LOCKED[] = {false, true, false, false, true, true, true};
        cvar_select(ui, "Hair", "cl_player_hairstyle", HAIR_VALUES, HAIR_STYLES, plain ? RAT_HAIR_LOCKED : NULL, 7);
        static const int HEAD_VALUES[] = {0, 1, 2, 3};
        static const bool RAT_HEAD_LOCKED[] = {false, true, true, true};
        cvar_select(ui, "Headgear", "cl_player_headstyle", HEAD_VALUES, HEAD_STYLES, plain ? RAT_HEAD_LOCKED : NULL, 4);
        static const int CHAIN_VALUES[] = {0, 1, 2};
        cvar_select(ui, "Chain", "cl_player_chainstyle", CHAIN_VALUES, CHAIN_STYLES, NULL, 3);
    }
    section(ui, "COLOURS");
    color_row(ui, "Shirt", "cl_player_shirt");
    color_row(ui, "Pants", "cl_player_pants");
    color_row(ui, "Skin", "cl_player_skin");
    color_row(ui, "Hair", "cl_player_hair");
    color_row(ui, "Jets", "cl_player_jet");
    color_row(ui, "Grenades", "cl_grenade_color");
    section(ui, "LOADOUT");
    {
        int values[WEAPON_COUNT], count = 0;
        const char *names[WEAPON_COUNT];
        for (int i = WEAPON_EAGLE; i <= WEAPON_MINIGUN && count < MAINMENU_POPUP_ITEMS; i++) {
            values[count] = i;
            names[count++] = ctx && ctx->weapons.info[i].name ? ctx->weapons.info[i].name : "?";
        }
        cvar_select(ui, "Primary", "cl_player_wep", values, names, NULL, count);
        static const int SECONDARY_VALUES[] = {0, 1, 2, 3};
        cvar_select(ui, "Secondary", "cl_player_secwep", SECONDARY_VALUES, SECONDARIES, NULL, 4);
    }
    gap(ui, 4);
    Row note = row(ui, 22, false);
    if (note.shown) text_mid(F_BODY, "Team games dress you in your team's shirt.", note.x + 12, note.y + 11, FAINT);
    ui->w = w;

    // the soldier as dressed, on a stand of its own, as large as the stand allows
    float px = x + w - pw, py = BODY_TOP, ph = ui->bottom - BODY_TOP;
    box(px, py, pw, ph, WELL, LINE);
    float floor_y = py + ph * 0.74f, scale = clampf(ph / 52.0f, 4.0f, 7.0f);
    char name[64];
    const Cvar *cv = cvar_find(ui->con, "cl_player_name");
    fit(F_BOLD, name, sizeof name, cv && cv->value[0] ? cv->value : "Major", pw - 20);
    text_at(F_BOLD, name, px + (pw - width_of(F_BOLD, name)) / 2, py + 14, TEXT);
    rrect(px + pw / 2 - 34, floor_y - 2, 60, 5, 2.5f, (Rgba){0, 0, 0, 90}); // the ground under it
    preview(ui, gostek, ctx, vec2(px + pw / 2 - 2 * scale, floor_y), scale);
}

// --- the controls -------------------------------------------------------------------

// The keys' rows: what each does, and the key that does it, by what it is for.
typedef struct Control {
    const char *label, *command;
} Control;

static const Control CONTROLS[] = {
    {"Left", "+left"},           {"Right", "+right"},          {"Jump", "+jump"},           {"Crouch", "+crouch"},
    {"Prone", "+prone"},         {"Jet", "+jet"},              {"Fire", "+fire"},           {"Throw grenade", "+throw"},
    {"Reload", "+reload"},       {"Change weapon", "+change"}, {"Throw weapon", "+drop"},   {"Throw flag", "+flagthrow"},
    {"Suicide", "say /kill"},    {"Chat", "chat"},             {"Team chat", "teamchat"},   {"Command", "cmd"},
    {"Radio", "+radio"},         {"Weapons menu", "weaponsmenu"}, {"Team menu", "teammenu"}, {"Scoreboard", "fragsmenu"},
    {"Weapon stats", "statsmenu"}, {"Minimap", "toggle ui_minimap"},
};
#define CONTROL_COUNT ((int)(sizeof CONTROLS / sizeof CONTROLS[0]))

// The groups, as runs of CONTROLS: a title and where each begins.
typedef struct ControlGroup {
    const char *title;
    int first, count;
    int column;
} ControlGroup;

static const ControlGroup CONTROL_GROUPS[] = {
    {"MOVEMENT", 0, 6, 0},
    {"COMBAT", 6, 7, 0},
    {"TALK", 13, 4, 1},
    {"MENUS", 17, 5, 1},
};

// The key bound to `command`, the first if several; "" if none.
static const char *key_of(const Console *con, const char *command)
{
    for (int i = 0; i < console_bind_count(con); i++) {
        const char *key, *text;
        if (console_bind_at(con, i, &key, &text) && strcmp(text, command) == 0) return key;
    }
    return "";
}

static bool is_modifier(SDL_Scancode key)
{
    return key == SDL_SCANCODE_LSHIFT || key == SDL_SCANCODE_RSHIFT || key == SDL_SCANCODE_LCTRL || key == SDL_SCANCODE_RCTRL ||
           key == SDL_SCANCODE_LALT || key == SDL_SCANCODE_RALT;
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

// A control's row: what it does, and its key on a chip; a click, or Enter, waits for the
// next key, which the events give it (mainmenu_event).
static void bind_row(Ui *ui, int i)
{
    MainMenu *m = ui->m;
    Row r = row(ui, 24, true);
    bool capturing = m->capturing == i;
    if ((r.shown && take(ui, r.id, r.x, r.y, r.w, r.h)) || take_enter(ui, r.focused)) {
        m->capturing = i;
        m->capture_mod[0] = '\0';
    }
    if (!r.shown) return;
    char key[40];
    const char *bound = key_of(ui->con, CONTROLS[i].command);
    if (capturing && m->capture_mod[0]) snprintf(key, sizeof key, "%s + ...", m->capture_mod); // alone, or with the next
    else if (capturing) snprintf(key, sizeof key, "Press a key");
    else if (!bound[0]) snprintf(key, sizeof key, "unbound");
    else {
        size_t n = 0;
        for (; bound[n] && n + 1 < sizeof key; n++) key[n] = (char)toupper((unsigned char)bound[n]);
        key[n] = '\0';
    }
    float kh = 18, kw = maxf(width_of(F_BOLD, key) + 18, 54), kx = r.x + r.w - 8 - kw, ky = r.y + (r.h - kh) / 2;
    text_fit(F_LABEL, CONTROLS[i].label, r.x + 10, r.y + r.h / 2, kx - r.x - 20, TEXT);
    if (capturing) {
        float pulse = 0.5f + 0.5f * sinf((float)m->time * 6.0f);
        rrect(kx, ky, kw, kh, RADIUS, with_alpha(ACCENT, (uint8_t)(110 + 120 * pulse)));
    } else {
        box(kx, ky, kw, kh, r.hot ? CONTROL_HOT : CONTROL, r.hot ? (Rgba){255, 255, 255, 48} : BORDER);
    }
    text_mid(F_BOLD, key, kx + (kw - width_of(F_BOLD, key)) / 2, ky + kh / 2, capturing ? TEXT : bound[0] ? TEXT : FAINT);
}

static void page_controls(Ui *ui)
{
    float x = ui->x, w = ui->w, top_y = ui->y;
    bool two = w >= 400;
    float col_w = two ? (w - 20) / 2 : w;
    float ends[2] = {top_y, top_y};
    for (size_t g = 0; g < sizeof CONTROL_GROUPS / sizeof CONTROL_GROUPS[0]; g++) {
        const ControlGroup *group = &CONTROL_GROUPS[g];
        int column = two ? group->column : 0;
        ui->x = x + (float)column * (col_w + 20);
        ui->w = col_w;
        ui->y = ends[column];
        section(ui, group->title);
        for (int i = group->first; i < group->first + group->count; i++) bind_row(ui, i);
        gap(ui, 6);
        ends[column] = ui->y;
    }
    ui->x = x;
    ui->w = w;
    ui->y = maxf(ends[0], ends[1]);
}

// --- the taunts ---------------------------------------------------------------------

// The editor's loaded taunt: the slot's bind, as taunt_at reads it, or a new taunt
// on `slot` (chat, on alt) when nothing is bound there.
static void taunt_load(MainMenu *m, const Console *con, int slot)
{
    unfocus(m);
    m->taunt_slot = slot;
    Taunt t;
    if (taunt_at(con, slot, &t)) {
        m->taunt_mod = t.mod;
        m->taunt_mode = t.mode;
        m->taunt_radio = t.radio;
        snprintf(m->taunt_text, sizeof m->taunt_text, "%s", t.text);
    } else {
        m->taunt_mod = 0;
        m->taunt_mode = TAUNT_CHAT;
        m->taunt_radio = 0;
        m->taunt_text[0] = '\0';
    }
}

// A taunt as its list row reads: the combo, as Alt+Q, what the text is — a radio
// call, or said to everyone or the team — and the text. A click, or Enter, loads it
// into the editor.
static void taunt_row(Ui *ui, const Console *con, int slot, const Taunt *t)
{
    MainMenu *m = ui->m;
    Row r = row(ui, ROW_H, true);
    if ((r.shown && take(ui, r.id, r.x, r.y, r.w, r.h)) || take_enter(ui, r.focused)) taunt_load(m, con, slot);
    if (!r.shown) return;
    char combo[24];
    snprintf(combo, sizeof combo, "%c%s+%c", (char)toupper((unsigned char)TAUNT_MOD_KEYS[t->mod][0]),
             TAUNT_MOD_KEYS[t->mod] + 1, (char)toupper((unsigned char)TAUNT_SLOT_KEYS[slot][0]));
    const char *what = t->radio ? "Radio" : t->mode == TAUNT_TEAM ? "Team" : "Chat";
    float cy = r.y + r.h / 2, cx = r.x + 10;
    float cw = minf(width_of(F_BOLD, combo), 88);
    text_fit(F_BOLD, combo, cx, cy, cw, TEXT);
    cx += cw + 12;
    float ww = minf(width_of(F_BODY, what), 110);
    text_fit(F_BODY, what, cx, cy, ww, t->radio ? ACCENT : MUTED);
    cx += ww + 12;
    text_fit(F_BODY, t->text, cx, cy, r.w - 10 - cx + r.x, MUTED);
}

// The loaded taunt written back: the message as its bind (taunt_compose), the slot
// unbound when it is empty. The config is saved, so it survives the game. The
// Update button and Enter in the message both run this.
static void taunt_update(MainMenu *m, Console *con)
{
    if (m->taunt_slot < 0) return;
    char text[CONSOLE_VALUE_SIZE];
    if (m->taunt_text[0]) {
        taunt_compose(text, sizeof text, m->taunt_mode, m->taunt_text, m->taunt_radio);
        taunt_set(con, m->taunt_slot, m->taunt_mod, text);
    } else {
        taunt_set(con, m->taunt_slot, m->taunt_mod, ""); // empty: the slot unbound
    }
    console_save(con, "config.cfg");
}

// The loaded taunt unbound, and the editor emptied: the Clear button and Delete.
static void taunt_clear(MainMenu *m, Console *con)
{
    if (m->taunt_slot < 0) return;
    taunt_set(con, m->taunt_slot, m->taunt_mod, "");
    taunt_load(m, con, m->taunt_slot);
    console_save(con, "config.cfg");
}

// The editor: on the left, the taunts there are, in the slots' order; on the right,
// the message and what it is, then the keyboard, whose key the combo is bound on.
// Update writes the loaded taunt into the config, Clear unbinds it (Delete too).
static void page_taunts(Ui *ui)
{
    Console *con = ui->con;
    MainMenu *m = ui->m;
    float x = ui->x, w = ui->w, top_y = ui->y;
    bool two = w >= 560; // below that the two columns would be too narrow for the modes and the keyboard
    float col_w = two ? (w - 20) / 2 : w;
    float ends[2] = {top_y, top_y};

    ui->w = col_w;
    section(ui, "TAUNTS");
    bool any = false;
    for (int slot = 0; slot < TAUNT_SLOTS; slot++) {
        Taunt t;
        if (taunt_at(con, slot, &t)) {
            any = true;
            taunt_row(ui, con, slot, &t);
        }
    }
    if (!any) {
        Row r = row(ui, ROW_H, false);
        if (r.shown)
            text_fit(F_BODY, "None yet: pick a key on the keyboard and type what it says.", r.x + 10, r.y + r.h / 2, r.w - 20, MUTED);
    }
    gap(ui, 6);
    ends[0] = ui->y;

    ui->x = two ? x + col_w + 20 : x;
    ui->y = two ? top_y : ends[0];
    section(ui, "EDIT");
    { // the message, typed; what makes it a console line (quotes, semicolons) is left out
        Row r = row(ui, ROW_H, true);
        field_box(ui, r.id, r.focused, r.x + 10, ctrl_y(&r), r.w - 20, "#taunt", CONSOLE_VALUE_SIZE - 1, "What the key says",
                  false);
    }
    if (m->taunt_radio) {
        // the call says the message to the team itself, so there is nothing to pick here
        Row r = row(ui, ROW_H, false);
        if (r.shown)
            text_fit(F_BODY, "The call says the message to the team, with its sound.", r.x + 10, r.y + r.h / 2, r.w - 20, MUTED);
    } else { // the two modes, one of them always on
        static const char *const MODE_NAMES[] = {"Chat", "Team chat"};
        Row r = row(ui, ROW_H, false);
        float cx = r.x;
        for (int mode = 0; mode < 2; mode++) {
            float cw = width_of(F_BODY, MODE_NAMES[mode]) + 30;
            if (chip(ui, cx, r.y + (r.h - CTRL_H) / 2, MODE_NAMES[mode], (int)m->taunt_mode == mode))
                m->taunt_mode = (TauntMode)mode;
            cx += cw + 8;
        }
    }
    { // the radio call the key sends, named from the radio_* cvars as the menu reads them
        char labels[10][40];
        const char *names[10];
        names[0] = "None";
        for (int i = 1; i <= 9; i++) {
            int call = (i - 1) / 3 + 1, place = (i - 1) % 3 + 1;
            char name[CONSOLE_NAME_SIZE];
            snprintf(name, sizeof name, "radio_%d", call);
            const Cvar *c = cvar_find(con, name);
            snprintf(name, sizeof name, "radio_%d_%d", call, place);
            const Cvar *p = cvar_find(con, name);
            // cut to the box's room: the words' own lines in the game show them whole
            snprintf(labels[i], sizeof labels[i], "%.*s - %.*s", 20, c ? c->value : "?", 12, p ? p->value : "?");
            names[i] = labels[i];
        }
        int picked = select_box(ui, "Radio", names, NULL, 10, m->taunt_radio);
        if (picked >= 0) m->taunt_radio = picked;
    }
    {
        static const char *const MOD_NAMES[] = {"Alt", "Ctrl", "Shift"};
        int picked = select_box(ui, "Modifier", MOD_NAMES, NULL, TAUNT_MODS, m->taunt_mod);
        if (picked >= 0) m->taunt_mod = picked;
    }
    section(ui, "KEY");
    { // the 36 keys, as the number row and the qwerty rows; a taunt's key is tinted
        static const int ROW_FIRST[4] = {0, 10, 20, 29};
        static const int ROW_COUNT[4] = {10, 10, 9, 7};
        const float key_gap = 5, kh = 24;
        float kw = minf(28, (col_w - 9 * key_gap) / 10);
        for (int row_i = 0; row_i < 4; row_i++) {
            Row r = row(ui, kh + 6, false);
            float row_w = ROW_COUNT[row_i] * kw + (ROW_COUNT[row_i] - 1) * key_gap;
            float kx = r.x + (r.w - row_w) / 2;
            for (int k = 0; k < ROW_COUNT[row_i]; k++) {
                int slot = ROW_FIRST[row_i] + k;
                Taunt t;
                bool bound = taunt_at(con, slot, &t);
                bool selected = m->taunt_slot == slot;
                int id = nav_next(ui);
                bool focused = nav_focused(ui, id, r.y, kh);
                bool hot = over(ui, kx, r.y, kw, kh);
                if ((r.shown && take(ui, id, kx, r.y, kw, kh)) || take_enter(ui, focused)) taunt_load(m, con, slot);
                if (!r.shown) {
                    kx += kw + key_gap;
                    continue;
                }
                focus_ring(ui, focused, kx, r.y, kw, kh, RADIUS);
                Rgba fill = selected ? ACCENT_SOFT : bound ? (Rgba){232, 80, 30, 22} : hot ? CONTROL_HOT : CONTROL;
                Rgba edge = selected ? ACCENT : bound ? with_alpha(ACCENT, 140) : BORDER;
                box(kx, r.y, kw, kh, fill, edge);
                char label[2] = {(char)toupper((unsigned char)TAUNT_SLOT_KEYS[slot][0]), '\0'};
                text_mid(F_BOLD, label, kx + kw / 2, r.y + kh / 2, bound || selected ? TEXT : MUTED);
                kx += kw + key_gap;
            }
        }
    }
    gap(ui, 6);
    ends[1] = ui->y;

    ui->x = x;
    ui->w = w;
    ui->y = maxf(ends[0], ends[1]);
    ui->scrolling = false;
    float bx;
    bool none = m->taunt_slot < 0;
    if (big_button(ui, x + w, "UPDATE", true, none, &bx)) {
        taunt_update(m, con);
    } else if (big_button(ui, bx - 12, "CLEAR", false, none, NULL)) {
        taunt_clear(m, con);
    }
}

// --- options and graphics -----------------------------------------------------------

typedef struct Resolution {
    int w, h;
} Resolution;
static const Resolution RESOLUTIONS[] = {{640, 480},   {800, 600},   {1024, 768},  {1280, 720},
                                         {1280, 960},  {1600, 900},  {1920, 1080}, {2560, 1440}};
#define RESOLUTION_COUNT ((int)(sizeof RESOLUTIONS / sizeof RESOLUTIONS[0]))

static void page_options(Ui *ui)
{
    section(ui, "SOUND");
    slider(ui, "Volume", "snd_volume", 0, 100, 5, true, "%d%%");
    toggle(ui, "Distant battle sounds", "snd_effects_battle");
    toggle(ui, "Deafening blasts", "snd_effects_explosions");
    section(ui, "MOUSE");
    slider(ui, "Sensitivity", "cl_sensitivity", 0.1f, 5.0f, 0.1f, false, "%.1f");
    color_row(ui, "Menu cursor colour", "cl_cursor_color");
    slider(ui, "Menu cursor size", "cl_cursor_size", 50, 200, 10, true, "%d%%");
    color_row(ui, "Crosshair colour", "cl_crosshair_color");
    slider(ui, "Crosshair size", "cl_crosshair_size", 50, 200, 10, true, "%d%%");
    section(ui, "INTERFACE");
    toggle(ui, "Player names", "ui_playernames");
    toggle(ui, "Minimap", "ui_minimap");
    toggle(ui, "Follow scoped shot", "cl_trackshot");
    section(ui, "NETWORK");
    slider(ui, "Smoothing", "cl_smooth", 0, 500, 25, true, "%d ms");
}

// What is drawn: the window, then the world's scenery, weather and trails, and the sky,
// the map's colours or two of the player's own.
static void page_graphics(Ui *ui)
{
    Console *con = ui->con;
    section(ui, "DISPLAY");
    {
        static const int MODES[] = {0, 1, 2};
        static const char *const MODE_NAMES[] = {"Windowed", "Fullscreen", "Borderless"};
        cvar_select(ui, "Window", "r_fullscreen", MODES, MODE_NAMES, NULL, 3);
    }
    {
        // the presets, and the size set by hand when it is none of them
        int w = cvar_int(con, "r_screenwidth", 320, 16384), h = cvar_int(con, "r_screenheight", 240, 16384);
        char labels[RESOLUTION_COUNT + 1][24];
        const char *names[RESOLUTION_COUNT + 1];
        int count = 0, current = -1;
        for (int i = 0; i < RESOLUTION_COUNT; i++) {
            snprintf(labels[count], sizeof labels[count], "%d x %d", RESOLUTIONS[i].w, RESOLUTIONS[i].h);
            names[count] = labels[count];
            if (RESOLUTIONS[i].w == w && RESOLUTIONS[i].h == h) current = count;
            count++;
        }
        if (current < 0) {
            snprintf(labels[count], sizeof labels[count], "%d x %d", w, h);
            names[count] = labels[count];
            current = count++;
        }
        int picked = select_box(ui, "Resolution", names, NULL, count, current);
        if (picked >= 0 && picked < RESOLUTION_COUNT) {
            set_int(con, "r_screenwidth", RESOLUTIONS[picked].w);
            set_int(con, "r_screenheight", RESOLUTIONS[picked].h);
        }
    }
    toggle(ui, "VSync", "r_swapeffect");
    {
        // the frame rate's limit (r_fpslimit, r_maxfps): none, a preset, or the one set by hand
        static const int RATES[] = {30, 60, 75, 120, 144, 165, 240, 360, 500};
        enum { RATE_COUNT = sizeof RATES / sizeof RATES[0] };
        int limit = cvar_int(con, "r_fpslimit", 0, 1) ? cvar_int(con, "r_maxfps", 10, 1000) : 0;
        char labels[RATE_COUNT + 2][16];
        const char *names[RATE_COUNT + 2];
        int values[RATE_COUNT + 2], count = 0, current = 0;
        snprintf(labels[count], sizeof labels[count], "None");
        values[count++] = 0;
        for (int i = 0; i < RATE_COUNT; i++) {
            if (RATES[i] == limit) current = count;
            snprintf(labels[count], sizeof labels[count], "%d FPS", RATES[i]);
            values[count++] = RATES[i];
        }
        if (limit && current == 0) { // set by hand: shown as it is
            current = count;
            snprintf(labels[count], sizeof labels[count], "%d FPS", limit);
            values[count++] = limit;
        }
        for (int i = 0; i < count; i++) names[i] = labels[i];
        int picked = select_box(ui, "Frame rate limit", names, NULL, count, current);
        if (picked >= 0 && picked < count && picked != current) {
            set_int(con, "r_fpslimit", values[picked] != 0);
            if (values[picked]) set_int(con, "r_maxfps", values[picked]);
        }
    }
    section(ui, "WORLD");
    toggle(ui, "Background scenery", "r_scenery");
    toggle(ui, "Weather", "r_weathereffects");
    toggle(ui, "Bullet trails", "r_trails");
    section(ui, "SKY");
    {
        static const int SKY[] = {0, 1};
        static const char *const SKY_NAMES[] = {"The map's", "My colours"};
        cvar_select(ui, "Sky", "r_forcebg", SKY, SKY_NAMES, NULL, 2);
    }
    if (cvar_int(con, "r_forcebg", 0, 1)) {
        color_row(ui, "Sky top", "r_forcebg_color1");
        color_row(ui, "Sky bottom", "r_forcebg_color2");
    }
}

// --- the menu -----------------------------------------------------------------------

static void unfocus(MainMenu *m)
{
    if (m->focus_cvar[0]) SDL_StopTextInput();
    m->focus_cvar[0] = '\0';
    m->edit_select_all = false; // the field gone, its selection with it
}

static void go_page(MainMenu *m, MainPage page)
{
    if (page == MAIN_SERVERS && m->page != MAIN_SERVERS) snprintf(m->command, sizeof m->command, "browse"); // the list as it is now
    m->page = page;
    m->side = (int)page;
    m->nav = 0;
    m->scroll = m->scroll_max = 0;
    m->capturing = -1;
    m->drag = -1;
    popup_close(m);
    unfocus(m);
}

void mainmenu_show(MainMenu *m, bool shown)
{
    m->shown = shown;
    m->page = MAIN_SERVERS;
    if (shown) snprintf(m->command, sizeof m->command, "browse"); // the list as it is now
    m->zone = MAIN_ZONE_RAIL;
    m->side = 0;
    m->nav = 0;
    m->scroll = m->scroll_max = 0;
    m->capturing = -1;
    m->drag = -1;
    m->picked_owner = -1;
    m->scroll_drag = -1;
    m->clicked = m->mouse_down = false;
    m->key_move = m->key_side = m->key_page = 0;
    m->key_enter = m->key_back = false;
    m->taunt_slot = -1;
    m->taunt_mod = 0;
    m->taunt_mode = TAUNT_CHAT;
    m->taunt_radio = 0;
    m->taunt_text[0] = '\0';
    m->edit_select_all = false;
    m->field_click_at = 0;
    m->field_clicked[0] = '\0';
    popup_close(m);
    unfocus(m);
}

// The edit into what the focused field edits.
static void edit_commit(MainMenu *m, Console *con)
{
    if (strcmp(m->focus_cvar, "#taunt") == 0) snprintf(m->taunt_text, sizeof m->taunt_text, "%s", m->edit);
    else if (m->focus_cvar[0] == '#') snprintf(m->search, sizeof m->search, "%s", m->edit);
    else cvar_set(con, m->focus_cvar, m->edit);
}

// Text into the focused field, typed or pasted: what fits, control characters (a pasted
// line's end among them) left out; the cvar follows.
static void edit_insert(MainMenu *m, Console *con, const char *text)
{
    if (m->edit_select_all) { // all of it selected: the first key, or paste, replaces it
        m->edit[0] = '\0';
        m->edit_select_all = false;
    }
    size_t len = strlen(m->edit);
    for (const char *s = text; *s && (int)len < m->edit_max && len + 1 < sizeof m->edit; s++) {
        if ((unsigned char)*s < 32) continue;
        m->edit[len++] = *s;
        m->edit[len] = '\0';
    }
    edit_commit(m, con);
}

static void key_nav(MainMenu *m, int move, int side, bool enter, bool back)
{
    m->key_move += move;
    m->key_side += side;
    m->key_enter |= enter;
    m->key_back |= back;
    m->keys_used = true;
    if (move) m->scroll_follow = true;
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
        // A modifier (Shift, Ctrl, Alt) waits: let go alone, it is the key; held with
        // another, the two are, as "shift+e", which the binds look up first (input.h).
        char name[32], combo[48];
        bool modifier = (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) && is_modifier(e->key.keysym.scancode);
        if (modifier) {
            if (e->type == SDL_KEYDOWN && !m->capture_mod[0]) input_event_key_name(e, m->capture_mod, sizeof m->capture_mod);
            if (e->type == SDL_KEYUP && m->capture_mod[0]) {
                rebind(con, m->capture_mod, CONTROLS[m->capturing].command);
                m->capture_mod[0] = '\0';
                m->capturing = -1;
            }
            return true;
        }
        if (input_event_key_name(e, name, sizeof name)) {
            // the modifier held, named as the binds name it: Alt before Ctrl before Shift
            SDL_Keymod mod = e->type == SDL_KEYDOWN ? e->key.keysym.mod : KMOD_NONE;
            const char *with = (mod & KMOD_ALT) ? "alt" : (mod & KMOD_CTRL) ? "ctrl" : (mod & KMOD_SHIFT) ? "shift" : NULL;
            if (with) snprintf(combo, sizeof combo, "%s+%s", with, name);
            else snprintf(combo, sizeof combo, "%s", name);
            rebind(con, combo, CONTROLS[m->capturing].command);
            m->capture_mod[0] = '\0';
            m->capturing = -1;
            return true;
        }
        if (e->type == SDL_CONTROLLERBUTTONDOWN && e->cbutton.button == SDL_CONTROLLER_BUTTON_B) m->capturing = -1;
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
            if ((e->key.keysym.mod & KMOD_CTRL) && e->key.keysym.scancode == SDL_SCANCODE_A) {
                m->edit_select_all = true; // Ctrl+A, as the double click: all of the text selected
                return true;
            }
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
                if (m->edit_select_all) {
                    m->edit[0] = '\0'; // the whole selection, gone at once
                    m->edit_select_all = false;
                } else {
                    size_t len = strlen(m->edit);
                    if (len) m->edit[len - 1] = '\0';
                }
                edit_commit(m, con);
                break;
            }
            case SDL_SCANCODE_TAB: // on to the next, or back with Shift
                unfocus(m);
                key_nav(m, (e->key.keysym.mod & KMOD_SHIFT) ? -1 : 1, 0, false, false);
                break;
            case SDL_SCANCODE_RETURN:
            case SDL_SCANCODE_KP_ENTER: // the taunt's message: Enter is the Update button
                if (strcmp(m->focus_cvar, "#taunt") == 0) taunt_update(m, con);
                unfocus(m);
                break;
            case SDL_SCANCODE_ESCAPE: unfocus(m); break;
            default: break;
            }
            return true;
        }
        if (e->type == SDL_KEYUP) return true;
    }
    switch (e->type) {
    case SDL_MOUSEBUTTONDOWN:
        if (e->button.button == SDL_BUTTON_LEFT) m->clicked = m->mouse_down = true;
        return true;
    case SDL_MOUSEBUTTONUP:
        if (e->button.button == SDL_BUTTON_LEFT) m->mouse_down = false;
        return true;
    case SDL_MOUSEWHEEL: m->wheel += e->wheel.y; return true;
    case SDL_TEXTINPUT:
    case SDL_KEYUP: return true;
    case SDL_KEYDOWN:
        switch (e->key.keysym.scancode) {
        case SDL_SCANCODE_UP: key_nav(m, -1, 0, false, false); break;
        case SDL_SCANCODE_DOWN: key_nav(m, 1, 0, false, false); break;
        case SDL_SCANCODE_LEFT: key_nav(m, 0, -1, false, false); break;
        case SDL_SCANCODE_RIGHT: key_nav(m, 0, 1, false, false); break;
        case SDL_SCANCODE_TAB: key_nav(m, (e->key.keysym.mod & KMOD_SHIFT) ? -1 : 1, 0, false, false); break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
        case SDL_SCANCODE_SPACE: key_nav(m, 0, 0, true, false); break;
        case SDL_SCANCODE_ESCAPE: key_nav(m, 0, 0, false, true); break;
        case SDL_SCANCODE_Q: m->key_page--, m->keys_used = true; break;
        case SDL_SCANCODE_E: m->key_page++, m->keys_used = true; break;
        case SDL_SCANCODE_DELETE: // on the taunts page, Delete clears the loaded taunt
            if (m->page == MAIN_TAUNTS) taunt_clear(m, con);
            break;
        default: break;
        }
        return true;
    case SDL_CONTROLLERBUTTONDOWN:
        switch (e->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: key_nav(m, -1, 0, false, false); break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: key_nav(m, 1, 0, false, false); break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: key_nav(m, 0, -1, false, false); break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: key_nav(m, 0, 1, false, false); break;
        case SDL_CONTROLLER_BUTTON_A: key_nav(m, 0, 0, true, false); break;
        case SDL_CONTROLLER_BUTTON_B: key_nav(m, 0, 0, false, true); break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: m->key_page--, m->keys_used = true; break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: m->key_page++, m->keys_used = true; break;
        default: break;
        }
        return true;
    case SDL_CONTROLLERAXISMOTION: { // the stick as the pad: a step each time it is pushed over
        int axis = e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ? 0 : e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY ? 1 : -1;
        if (axis < 0) return true;
        int dir = e->caxis.value > 20000 ? 1 : e->caxis.value < -20000 ? -1 : 0;
        if (dir && dir != m->axis[axis]) key_nav(m, axis ? dir : 0, axis ? 0 : dir, false, false);
        if (dir || abs(e->caxis.value) < 12000) m->axis[axis] = dir;
        return true;
    }
    case SDL_CONTROLLERBUTTONUP: return true;
    default: return false;
    }
}

// The rail's items, in the keys' order: the pages, then Resume (while a server has us)
// and Quit.
static int rail_count(bool joined) { return MAIN_PAGE_COUNT + (joined ? 1 : 0) + 1; }

static void rail_activate(Ui *ui, int item, bool joined)
{
    MainMenu *m = ui->m;
    if (item < MAIN_PAGE_COUNT) {
        if ((int)m->page != item) go_page(m, (MainPage)item);
        m->zone = MAIN_ZONE_CONTENT;
        m->nav = 0;
    } else if (joined && item == MAIN_PAGE_COUNT) {
        mainmenu_show(m, false);
    } else {
        snprintf(m->command, sizeof m->command, "quit");
    }
}

// The keys while the rail has them: up and down go along it (a page is shown as its
// item is reached), right or Enter goes into the page, Escape back to the game. In the
// page, Escape (or up from its first widget) comes back out here; Q and E, or a
// controller's shoulders, turn the pages from anywhere.
static void rail_keys(Ui *ui, bool joined)
{
    MainMenu *m = ui->m;
    if (ui->page) {
        int to = ((int)m->page + ui->page + MAIN_PAGE_COUNT) % MAIN_PAGE_COUNT;
        MainZone zone = m->zone;
        go_page(m, (MainPage)to);
        m->zone = zone;
        ui->page = 0;
    }
    if (m->zone == MAIN_ZONE_CONTENT) {
        if (ui->back) {
            m->zone = MAIN_ZONE_RAIL;
            m->side = (int)m->page;
            ui->back = false;
        }
        return;
    }
    int count = rail_count(joined);
    m->side = clampi(m->side, 0, count - 1);
    if (ui->move) {
        m->side = clampi(m->side + ui->move, 0, count - 1);
        if (m->side < MAIN_PAGE_COUNT && (int)m->page != m->side) go_page(m, (MainPage)m->side);
    }
    if (ui->enter || (ui->side > 0 && m->side < MAIN_PAGE_COUNT)) rail_activate(ui, m->side, joined);
    if (ui->back && joined) mainmenu_show(m, false); // back to the game; with none, nothing to go back to
    ui->enter = ui->back = false;
    ui->side = ui->move = 0;
}

// A button of the rail, outside the page's order (the rail's keys reach it).
static bool rail_button(Ui *ui, float x, float y, float w, float h, const char *caption, bool primary, bool focused)
{
    bool hot = over(ui, x, y, w, h);
    focus_ring(ui, focused, x, y, w, h, RADIUS);
    if (primary) rrect(x, y, w, h, RADIUS, hot ? ACCENT_HOT : ACCENT);
    else box(x, y, w, h, hot ? CONTROL_HOT : CONTROL, hot ? (Rgba){255, 255, 255, 48} : BORDER);
    float tw = width_of(F_BUTTON, caption);
    text_mid(F_BUTTON, caption, x + (w - tw) / 2, y + h / 2, TEXT);
    return take(ui, -1, x, y, w, h);
}

// The name, written: SOLDAT in the stencil face, and RELOADED under it in the accent,
// its letters spaced out to the same width. The height it took.
static float logo(float x, float y, float width)
{
    Font name = F_LOGO; // as large as the rail allows
    float w = width_of(name, "SOLDAT");
    if (w > width) {
        name.scale *= width / w;
        w = width_of(name, "SOLDAT");
    }
    text_at(name, "SOLDAT", x, y, TEXT);
    float h = line_height(name);
    Font sub = F_LOGO_SUB;
    float w0 = width_of(sub, "RELOADED");
    sub.tracking = 0.1f;
    float w1 = width_of(sub, "RELOADED"); // the width grows evenly with the tracking
    sub.tracking = w1 > w0 ? 0.1f * (w - w0) / (w1 - w0) : 0;
    float sy = y + h - 5;
    text_at(sub, "RELOADED", x, sy, ACCENT);
    return sy + line_height(sub) - y;
}

#define RAIL_ITEM_H 24.0f
#define RAIL_PAD 10.0f

// The rail down the left, on the same ground as the page, a divider between: the name,
// the pages under their groups, and at the bottom Resume (while a server has us), Quit
// and the version.
static void rail(Ui *ui, bool joined)
{
    MainMenu *m = ui->m;
    rect(RAIL_W - hairline, 0, RAIL_W, VIEW_H, DIVIDER);

    float x = RAIL_PAD + 8, y = 24;
    y += logo(x, y, RAIL_W - 2 * x) + 28;

    bool focus_rail = m->zone == MAIN_ZONE_RAIL;
    static const struct {
        const char *name;
        MainPage first, end;
    } GROUPS[] = {{"PLAY", MAIN_SERVERS, MAIN_PLAYER}, {"SETTINGS", MAIN_PLAYER, MAIN_PAGE_COUNT}};
    for (size_t g = 0; g < sizeof GROUPS / sizeof GROUPS[0]; g++) {
        // the group's label: small, faint and set apart, so it reads as a heading and not
        // as one more item
        text_at(F_GROUP, GROUPS[g].name, x, y, with_alpha(FAINT, 200));
        y += line_height(F_GROUP) + 6;
        for (int i = GROUPS[g].first; i < (int)GROUPS[g].end; i++) {
            bool chosen = m->page == (MainPage)i, focused = focus_rail && m->side == i;
            float ix = RAIL_PAD, iw = RAIL_W - 2 * RAIL_PAD;
            bool hot = over(ui, ix, y, iw, RAIL_ITEM_H);
            if (chosen) {
                rrect(ix, y, iw, RAIL_ITEM_H, RADIUS, ACCENT_SOFT);
                rrect(ix, y + 5, 2, RAIL_ITEM_H - 10, 1, ACCENT);
            } else if (hot) {
                rrect(ix, y, iw, RAIL_ITEM_H, RADIUS, HOVER);
            }
            focus_ring(ui, focused, ix, y, iw, RAIL_ITEM_H, RADIUS);
            text_fit(F_NAV, PAGE_NAMES[i], x, y + RAIL_ITEM_H / 2, iw - 16, chosen || hot ? TEXT : MUTED);
            if (take(ui, -1, ix, y, iw, RAIL_ITEM_H)) {
                go_page(m, (MainPage)i);
                m->zone = MAIN_ZONE_RAIL;
            }
            y += RAIL_ITEM_H + 2;
        }
        y += 18;
    }

    // the bottom, from the bottom up: the version, Quit, and Resume
    const char *version = "v" SOLDATRELOADED_VERSION;
    float vy = VIEW_H - 14 - line_height(F_TINY);
    text_at(F_TINY, version, x, vy, FAINT);
    float bw = RAIL_W - 2 * RAIL_PAD, by = vy - 10 - CTRL_H;
    if (rail_button(ui, RAIL_PAD, by, bw, CTRL_H, "Quit", false, focus_rail && m->side == rail_count(joined) - 1))
        snprintf(m->command, sizeof m->command, "quit");
    if (joined) {
        by -= CTRL_H + 8;
        if (rail_button(ui, RAIL_PAD, by, bw, CTRL_H, "Resume", true, focus_rail && m->side == MAIN_PAGE_COUNT)) mainmenu_show(m, false);
    }
}

// The panel's header: the page's title, and a line on what it is for.
static void header(const Ui *ui, float x, float w)
{
    MainPage page = ui->m->page;
    float y = PANEL_TOP + 13;
    text_at(F_TITLE, PAGE_TITLES[page], x, y, TEXT);
    y += line_height(F_TITLE) + 4;
    text_fit(F_SUBTITLE, PAGE_LINES[page], x, y + line_height(F_SUBTITLE) / 2, w, MUTED);
    rule(x, x + w, PANEL_TOP + HEADER_H, LINE);
}

// A glow: `color` at the middle, fading to nothing at `r`.
static void glow(float cx, float cy, float r, Rgba color)
{
    enum { SEGMENTS = 32 };
    GfxVertex v[SEGMENTS * 3];
    Rgba rim = with_alpha(color, 0);
    for (int s = 0; s < SEGMENTS; s++) {
        float a0 = 2 * (float)M_PI * (float)s / SEGMENTS, a1 = 2 * (float)M_PI * (float)(s + 1) / SEGMENTS;
        v[s * 3] = gfx_vertex(cx, cy, 0, 0, color);
        v[s * 3 + 1] = gfx_vertex(cx + r * cosf(a0), cy + r * sinf(a0), 0, 0, rim);
        v[s * 3 + 2] = gfx_vertex(cx + r * cosf(a1), cy + r * sinf(a1), 0, 0, rim);
    }
    gfx_draw_triangles(gfx_white(), v, SEGMENTS * 3);
}

// A quad from `a` along one edge to `b` along the other.
static void shade(float x0, float y0, float x1, float y1, Rgba a, Rgba b, bool across)
{
    GfxVertex v[4];
    if (across) { // `a` on the left, `b` on the right
        v[0] = gfx_vertex(x0, y0, 0, 0, a), v[1] = gfx_vertex(x1, y0, 0, 0, b);
        v[2] = gfx_vertex(x1, y1, 0, 0, b), v[3] = gfx_vertex(x0, y1, 0, 0, a);
    } else { // `a` at the top, `b` at the bottom
        v[0] = gfx_vertex(x0, y0, 0, 0, a), v[1] = gfx_vertex(x1, y0, 0, 0, a);
        v[2] = gfx_vertex(x1, y1, 0, 0, b), v[3] = gfx_vertex(x0, y1, 0, 0, b);
    }
    gfx_draw_quad(gfx_white(), v);
}

// A number from 0 to 1 that `n` always gives.
static float hash01(uint32_t n)
{
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n ^= n >> 4;
    n *= 0x27d4eb2du;
    n ^= n >> 15;
    return (float)(n & 0xffffff) / (float)0x1000000;
}

#define EMBERS 70

// What is behind the menu: night falling to steel, a warm glow low on the left and a
// cool one high on the right, the edges darkened, and embers rising slowly through it,
// each flickering and fading as it goes. All of it from the clock alone: nothing kept.
static void background(float W, double time)
{
    float t = (float)time;
    shade(0, 0, W, VIEW_H, (Rgba){18, 24, 38, 255}, (Rgba){10, 12, 18, 255}, false);
    float breathe = 0.85f + 0.15f * sinf(t * 0.4f);
    glow(W * 0.12f, VIEW_H * 1.02f, VIEW_H * 0.85f, (Rgba){255, 104, 24, (uint8_t)(72 * breathe)});
    glow(W * 0.9f, -VIEW_H * 0.05f, VIEW_H * 0.75f, (Rgba){79, 163, 255, 20});
    for (int i = 0; i < EMBERS; i++) {
        float speed = 6 + 16 * hash01(i * 7 + 1), size = 0.8f + 1.7f * hash01(i * 7 + 2);
        float span = VIEW_H + 60, rise = fmodf(t * speed + hash01(i * 7 + 3) * span, span);
        float y = VIEW_H + 20 - rise, life = rise / span; // 0 as it starts, 1 as it goes
        float x = hash01(i * 7 + 4) * W + sinf(t * (0.3f + 0.5f * hash01(i * 7 + 5)) + (float)i) * 14 + life * 30;
        float flicker = 0.7f + 0.3f * sinf(t * (3 + 4 * hash01(i * 7 + 6)) + (float)i);
        float fade = (1 - life) * minf(life * 8, 1) * flicker;
        uint8_t g = (uint8_t)(110 + 80 * hash01(i * 7 + 7));
        glow(x, y, size * 6, (Rgba){255, g, 40, (uint8_t)(70 * fade)});
        circle(x, y, size * 0.6f, (Rgba){255, (uint8_t)mini(g + 60, 255), 120, (uint8_t)(255 * fade)});
    }
    // the vignette
    Rgba dark = {0, 0, 0, 120}, none = {0, 0, 0, 0};
    shade(0, 0, W * 0.18f, VIEW_H, dark, none, true);
    shade(W * 0.82f, 0, W, VIEW_H, none, dark, true);
    shade(0, VIEW_H * 0.75f, W, VIEW_H, none, dark, false);
}

// What the footer says on the pages with nothing of their own to say there.
static const char *page_note(MainPage page)
{
    switch (page) {
    case MAIN_TAUNTS: return "Update writes the taunt into config.cfg; Clear, or Delete, takes the key's away.";
    case MAIN_CONTROLS:
    case MAIN_PLAYER:
    case MAIN_OPTIONS:
    case MAIN_GRAPHICS: return "Changes take effect at once, and are saved to config.cfg when the game closes.";
    default: return "";
    }
}

void mainmenu_draw(MainMenu *m, Console *con, const Interface *hud, const Gostek *gostek, const Context *ctx,
                   Vec2 cursor, float game_width, float pixel, double time, const char *status,
                   bool joined, bool hosting, const char (*maps)[64], int map_count, const Browser *browser,
                   const DemoListing *demos, int demo_count)
{
    if (!m->shown) {
        m->wheel = 0;
        m->key_move = m->key_side = m->key_page = 0;
        m->key_enter = m->key_back = false;
        return;
    }
    m->time = time;
    m->joined = joined;
    if (fabsf(cursor.x - m->last_cursor.x) > 0.01f || fabsf(cursor.y - m->last_cursor.y) > 0.01f || m->clicked) m->keys_used = false;
    m->last_cursor = cursor;
    if (!m->mouse_down) m->drag = m->scroll_drag = -1;
    Ui ui = {.m = m,
             .con = con,
             .hud = hud,
             .cursor = cursor,
             .game_width = game_width,
             .pixel = pixel,
             .click = m->clicked,
             .held = m->mouse_down,
             .move = m->key_move,
             .side = m->key_side,
             .page = m->key_page,
             .enter = m->key_enter,
             .back = m->key_back,
             .show_focus = m->keys_used};
    m->clicked = false;
    m->key_move = m->key_side = m->key_page = 0;
    m->key_enter = m->key_back = false;

    gfx_transform(mat3_ortho(0, game_width, 0, VIEW_H));
    text_pixel_ratio(vec2(pixel, pixel));
    text_shadow(1, 1, (Rgba){0, 0, 0, 160});
    text_align(TEXT_TOP);
    text_scale(1.0f);

    hairline = pixel;
    background(game_width, time);
    rect(0, 0, game_width, VIEW_H, SURFACE); // one ground for the rail and the page, the embers faint through it

    // the page beside the rail: as wide as the window allows, up to a width the rows
    // read well at
    float panel_w = minf(game_width - EDGE - PANEL_X, 740);
    float x = PANEL_X + PAD, w = panel_w - 2 * PAD;

    popup_input(&ui); // the popup first: it has the keys and the clicks while open
    rail_keys(&ui, joined);
    rail(&ui, joined);
    header(&ui, x, w);
    rule(x, x + w, PANEL_BOTTOM - FOOTER_H, LINE); // the footer's

    m->scroll = clampf(m->scroll, 0, m->scroll_max);
    ui.x = x;
    ui.w = w;
    ui.top = BODY_TOP;
    ui.bottom = BODY_BOTTOM;
    ui.y = BODY_TOP - m->scroll;
    ui.extent = BODY_TOP;
    ui.scrolling = true;

    switch (m->page) {
    case MAIN_SERVERS: page_servers(&ui, browser); break;
    case MAIN_JOIN: page_join(&ui, status, joined); break;
    case MAIN_LOCAL: page_local(&ui, status, hosting, maps, map_count); break;
    case MAIN_DEMOS: page_demos(&ui, demos, demo_count); break;
    case MAIN_PLAYER: page_player(&ui, gostek, ctx); break;
    case MAIN_CONTROLS: page_controls(&ui); break;
    case MAIN_TAUNTS: page_taunts(&ui); break;
    case MAIN_OPTIONS: ui.w = minf(w, 520); page_options(&ui); break;
    case MAIN_GRAPHICS: ui.w = minf(w, 520); page_graphics(&ui); break;
    default: break;
    }
    ui.x = x;
    ui.w = w;
    const char *note = page_note(m->page);
    if (note[0]) footer_text(&ui, x, w, note, MUTED); // a settings page: a note on where its changes go

    // what no widget took: up and down move the focus along the page, up from its
    // first widget back out to the rail
    if (m->zone == MAIN_ZONE_CONTENT) {
        if (ui.move < 0 && m->nav == 0) {
            m->zone = MAIN_ZONE_RAIL;
            m->side = (int)m->page;
        } else if (ui.move) {
            m->nav += ui.move;
        }
    }
    m->nav = clampi(m->nav, 0, maxi(ui.nav_count - 1, 0));
    if (m->zone == MAIN_ZONE_CONTENT && ui.nav_count == 0) m->zone = MAIN_ZONE_RAIL;

    // the page's scroll, by the wheel over it; its bar, when there is more than shows
    m->scroll_max = maxf(ui.extent - BODY_BOTTOM, 0);
    if (m->wheel && m->popup.kind == MAIN_POPUP_NONE && inside(cursor, PANEL_X, BODY_TOP, panel_w, BODY_BOTTOM - BODY_TOP)) {
        m->scroll -= (float)m->wheel * SCROLL_STEP;
        m->scroll_follow = false;
    }
    m->scroll = clampf(m->scroll, 0, m->scroll_max);
    if (m->scroll_max > 0) { // its bar, beside the page, dragged
        float track = BODY_BOTTOM - BODY_TOP, view = track / (track + m->scroll_max);
        float knob = maxf(track * view, 16), bar_x = x + w + 8;
        float pos = scroll_take(&ui, SCROLL_PAGE, bar_x, BODY_TOP, track, knob, m->scroll / m->scroll_max);
        m->scroll = pos * m->scroll_max;
        scroll_draw(&ui, SCROLL_PAGE, bar_x, BODY_TOP, track, knob, pos);
    }
    m->wheel = 0;

    popup_draw(&ui);
    if (ui.click) { // a click on nothing takes the keyboard from a field
        unfocus(m);
    }

    text_shadow(0, 0, (Rgba){0});
    text_tracking(0);
    interface_draw_pointer(hud, cursor, cvar_color(con, "cl_cursor_color"),
                           clampi(cvar_int(con, "cl_cursor_size", 50, 200), 50, 200) / 100.0f);
}

bool mainmenu_take_command(MainMenu *m, char *out, size_t size)
{
    if (!m->command[0]) return false;
    snprintf(out, size, "%s", m->command);
    m->command[0] = '\0';
    return true;
}

void mainmenu_open_page(MainMenu *m, MainPage page)
{
    go_page(m, page);
    m->zone = MAIN_ZONE_RAIL;
}

