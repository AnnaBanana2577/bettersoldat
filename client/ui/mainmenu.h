#pragma once

// The main menu: joining a server, a game hosted here (Local Play: the mode, the limits,
// the bots, the maps in rotation), the player's name and look with the gostek shown as
// it will be, the keys, and the options. The original had none of this in the game (its
// launcher did it); this one is drawn in the HUD's units over whatever is behind it.
//
// Everything it changes is a cvar or a bind of the console, so the config keeps it, and
// everything it asks of the game is a console command (connect, disconnect, host, quit)
// the app takes and runs (mainmenu_take_command): nothing here touches the game or the
// line. The widgets are immediate: each draw lays the page out again and acts on the
// click and the wheel the events recorded since, so there is no widget tree to keep.

#include <SDL.h>

#include "console/console.h"
#include "game/game.h"
#include "render/gostek.h"
#include "render/interface.h"

typedef enum MainPage { MAIN_HOME, MAIN_JOIN, MAIN_LOCAL, MAIN_PLAYER, MAIN_CONTROLS, MAIN_OPTIONS } MainPage;

#define MAINMENU_EDIT 128

typedef struct MainMenu {
    bool shown;
    MainPage page;
    char focus_cvar[CONSOLE_NAME_SIZE]; // the text field with the keyboard: the cvar it edits, empty for none
    char color_picker[CONSOLE_NAME_SIZE]; // the player color whose palette is open, empty for none
    char edit[MAINMENU_EDIT];           // its text while typed
    int edit_max;                       // how much of it the field takes
    int capturing;                      // the controls row waiting for a key, -1 for none
    bool clicked;                       // a left click since the last draw, at the cursor
    int wheel;                          // the wheel's notches since the last draw, up positive
    int map_scroll;                     // the map list's first row shown
    char command[256];                  // for the app to run; empty for none
    double time;                        // seconds, for the caret's blink
    bool joined;                        // a server has us: Resume, and Escape, go back to it
} MainMenu;

void mainmenu_show(MainMenu *m, bool shown);

// A key, a mouse button or text: the menu's while shown. True if it took the event.
bool mainmenu_event(MainMenu *m, Console *con, const SDL_Event *e);

// The menu over the frame, in the HUD's units (the view is 480 tall, `game_width` wide;
// `pixel` is one window pixel in units). `cursor` is the input's, in those units.
// `status` is a line for the join and local pages (the console's last), `joined` whether
// a server has us, `hosting` whether it is our own, `maps` the maps under assets for the
// rotation, `weapons` names the loadout, and the gostek and anims draw the preview.
void mainmenu_draw(MainMenu *m, Console *con, const Interface *hud, const Gostek *gostek, const Anims *anims,
                   const Weapons *weapons, Vec2 cursor, float game_width, float pixel, double time, const char *status,
                   bool joined, bool hosting, const char (*maps)[64], int map_count);

// A command the menu asked for since last taken: true, with it, once.
bool mainmenu_take_command(MainMenu *m, char *out, size_t size);
