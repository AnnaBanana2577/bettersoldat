#pragma once

// The consoles the HUD shows, the original's TConsole (Console.pas) as the client keeps
// two of them: the main console in the corner, a few lines that scroll away, and the
// big console, the same lines kept long, shown in its place while a line is typed.
// Both are fed from the game console's scrollback (console.h), so whatever is printed
// there, a cvar echoed, a line of chat, the server's word, shows in the HUD.
//
// The scrolling is the original's: a line is added at the bottom and, when the console
// is full, the oldest goes; each tick a clock runs, and when it reaches its mark the
// oldest line goes once; a new line sets the clock back by a wait, so the console holds
// still for a while after every line and empties one line at a time when nothing
// comes. (The clock is never reset by the scroll itself, as in the original, so after
// a lone line only that one goes.)

#include "console/console.h"
#include "ui/hud_data.h"

typedef struct HudConsole {
    HudLine lines[HUD_CONSOLE_LINES]; // oldest first
    int count;
    int count_max;
    int scroll_tick;
    int scroll_tick_max;
    int new_message_wait;
} HudConsole;

typedef struct Consoles {
    HudConsole main, big;
    uint32_t taken; // scrollback lines taken from the game console so far
} Consoles;

// `main_length` is ui_console_length; the big console holds what fits 85% of the view.
void consoles_init(Consoles *c, int main_length);

// New lines of the game console's scrollback into both, each frame.
void consoles_pull(Consoles *c, const Console *con);

// The clocks, once per tick.
void consoles_tick(Consoles *c);

// Into the HUD: the big console while `typing`, the main one otherwise.
void consoles_fill(const Consoles *c, HudData *d, bool typing);

// The big console's lines as one text, on the heap (free it); NULL if there is no room.
char *consoles_big_text(const Consoles *c);
