#include "ui/consoles.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAIN_SCROLL_TICKS 150 // Client.pas: MainConsole.ScrollTickMax, NewMessageWait
#define MAIN_MESSAGE_WAIT 150
#define BIG_SCROLL_TICKS 1500000 // the big console keeps its lines, in effect
#define CONSOLE_LINE_HEIGHT (1.5f * 9.0f) // font_consolelineheight * the small font's size
#define GAME_HEIGHT_UNITS 480.0f

static void hud_console_init(HudConsole *h, int count_max, int visible, int scroll_tick_max, int wait)
{
    *h = (HudConsole){.count_max = count_max, .visible = visible, .scroll_tick_max = scroll_tick_max, .new_message_wait = wait};
    if (h->count_max > HUD_CONSOLE_KEPT) h->count_max = HUD_CONSOLE_KEPT;
    if (h->count_max < 1) h->count_max = 1;
    if (h->visible > h->count_max) h->visible = h->count_max;
    if (h->visible > HUD_CONSOLE_LINES) h->visible = HUD_CONSOLE_LINES;
}

void consoles_init(Consoles *c, int main_length)
{
    *c = (Consoles){0};
    hud_console_init(&c->main, main_length, main_length, MAIN_SCROLL_TICKS, MAIN_MESSAGE_WAIT);
    // the big console keeps the whole scrollback and shows what fits 85% of the view
    hud_console_init(&c->big, HUD_CONSOLE_KEPT, (int)floorf(0.85f * GAME_HEIGHT_UNITS / CONSOLE_LINE_HEIGHT), BIG_SCROLL_TICKS, 0);
}

// ScrollConsole: the oldest line goes.
static void scroll(HudConsole *h)
{
    if (h->count == 0) return;
    memmove(h->lines, h->lines + 1, sizeof h->lines - sizeof h->lines[0]);
    h->count--;
}

// ConsoleAdd: at the bottom, the clock set back, the oldest gone when full.
static void add(HudConsole *h, const char *text, Rgba color)
{
    HudLine *l = &h->lines[h->count++];
    snprintf(l->text, sizeof l->text, "%s", text);
    l->color = color;
    h->scroll_tick = -h->new_message_wait;
    if (h->count == h->count_max) scroll(h);
}

void consoles_pull(Consoles *c, const Console *con)
{
    uint32_t total = console_log_total(con);
    if (c->taken > total) c->taken = 0; // a new console
    if (total - c->taken > CONSOLE_LOG_LINES - 1) c->taken = total - (CONSOLE_LOG_LINES - 1);
    for (; c->taken < total; c->taken++) {
        int back = (int)(total - 1 - c->taken);
        const char *text = console_log_line(con, back);
        if (!text || !text[0]) continue;
        Rgba color;
        if (!console_log_color(con, back, &color)) color = HUD_COLOR_DEFAULT;
        add(&c->main, text, color);
        add(&c->big, text, color);
    }
}

void consoles_tick(Consoles *c)
{
    if (++c->main.scroll_tick == c->main.scroll_tick_max) scroll(&c->main);
    if (++c->big.scroll_tick == c->big.scroll_tick_max) scroll(&c->big);
}

char *consoles_big_text(const Consoles *c)
{
    size_t size = 1;
    for (int i = 0; i < c->big.count; i++) size += strlen(c->big.lines[i].text) + 1;
    char *all = malloc(size);
    if (!all) return NULL;
    all[0] = '\0';
    for (int i = 0; i < c->big.count; i++) {
        strcat(all, c->big.lines[i].text);
        strcat(all, "\n");
    }
    return all;
}

int consoles_scroll_max(const Consoles *c)
{
    int past = c->big.count - c->big.visible;
    return past > 0 ? past : 0;
}

void consoles_fill(const Consoles *c, HudData *d, bool typing, int scroll)
{
    const HudConsole *h = typing ? &c->big : &c->main;
    int n = h->count < h->visible ? h->count : h->visible;
    int back = typing ? scroll : 0;
    if (back > h->count - n) back = h->count - n;
    if (back < 0) back = 0;
    int start = h->count - n - back;
    d->console_count = n;
    for (int i = 0; i < n; i++) d->console[i] = h->lines[start + i];
}
