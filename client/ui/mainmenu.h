#pragma once

// The main menu: joining a server, a game hosted here (Local Play: the mode, the limits,
// the bots, the maps in rotation), the demos recorded here, the player's name and look
// with the gostek shown as it will be, the keys, the taunts (what a key says: a message
// to everyone or the team, or your own words as a radio call), the options, and what is
// drawn of the world (Graphics). OpenSoldat
// has none of this in the game (its launcher does it); this one is drawn in the HUD's
// units over the world, which goes on behind it.
//
// Everything it changes is a cvar or a bind of the console, so the config keeps it, and
// everything it asks of the game is a console command (connect, disconnect, host, playdemo, quit)
// the app takes and runs (mainmenu_take_command): nothing here touches the game or the
// line. The widgets are immediate: each draw lays the page out again and acts on the
// click, the wheel and the keys the events recorded since, so there is no widget tree
// to keep. The mouse, the keys (arrows, Enter, Escape, Tab) and a game controller's pad
// all work it: the rail down the left picks the page (the name is written at its top,
// in the menu's own faces), and the page's widgets take the focus in turn.

#include <SDL.h>

#include "console/console.h"
#include "game/game.h"
#include "net/browser.h"
#include "net/demo.h"
#include "render/gostek.h"
#include "render/interface.h"
#include "ui/taunts.h"

typedef enum MainPage {
    MAIN_SERVERS, MAIN_JOIN, MAIN_LOCAL, MAIN_DEMOS, MAIN_PLAYER, MAIN_CONTROLS, MAIN_TAUNTS, MAIN_OPTIONS, MAIN_GRAPHICS,
    MAIN_PAGE_COUNT
} MainPage;

#define MAINMENU_EDIT 256 // a field's text, up to a cvar value's (the taunts' messages are that long)
#define MAINMENU_POPUP_ITEMS 16
#define MAINMENU_SEARCH 32

typedef enum MainZone { MAIN_ZONE_RAIL, MAIN_ZONE_CONTENT } MainZone; // the rail's items, or the page

typedef enum MainPopupKind { MAIN_POPUP_NONE, MAIN_POPUP_LIST, MAIN_POPUP_COLOR } MainPopupKind;

// A list or a colour picker open over the page, under the widget that opened it. It keeps its
// own copy of what it offers, as the widget is laid out anew each draw.
typedef struct MainPopup {
    MainPopupKind kind;
    int owner;    // the widget that opened it (its place in the keys' order)
    float x, y, w, h;
    int count;    // a list's items
    int hover;    // the item (or the palette's colour) under the cursor or the keys
    char names[MAINMENU_POPUP_ITEMS][40];
    bool locked[MAINMENU_POPUP_ITEMS];
    char cvar[CONSOLE_NAME_SIZE]; // a picker's colour
    bool clearable;               // a picker's colour may be none (the art's own)
    float hue, sat, val;          // a picker's colour as it is being set: kept, so a grey keeps its hue
    int drag;                     // what of the picker the mouse holds: 0 nothing, 1 the square, 2 the hue
} MainPopup;

typedef enum ServerSort { SERVER_SORT_PLAYERS, SERVER_SORT_NAME, SERVER_SORT_MODE, SERVER_SORT_MAP, SERVER_SORT_PING } ServerSort; // the fullest first, at first

typedef struct MainMenu {
    bool shown;
    MainPage page;
    MainZone zone;                      // where the keys are: the rail, or the page
    int side;                           // the rail's item with the keys: the pages, then Resume and Quit
    int nav;                            // the page's widget with the keys, in their order
    bool keys_used;                     // the keys moved the focus since the mouse last moved: it shows
    Vec2 last_cursor;
    int key_move, key_side;             // the arrows since the last draw: up/down, left/right
    int key_page;                       // Q and E, a controller's shoulders: the pages turned
    bool key_enter, key_back;
    int axis[2];                        // the controller's stick, as a direction, so a push is one step
    float scroll, scroll_max;           // the page's, in units
    bool scroll_follow;                 // the keys moved the focus: the page scrolls to it
    bool mouse_down;                    // the left button, for a slider's drag
    int drag;                           // the slider being dragged, -1 for none
    int scroll_drag;                    // the scrollbar being dragged (the page's, a list's), -1 for none
    float scroll_grab;                  // where on its knob it was taken, from the knob's top
    MainPopup popup;
    int picked_owner, picked;           // a list's choice, for its widget to take on its next draw
    char focus_cvar[CONSOLE_NAME_SIZE]; // the text field with the keyboard: the cvar it edits, empty for none
    char edit[MAINMENU_EDIT];           // its text while typed
    int edit_max;                       // how much of it the field takes
    bool edit_select_all;               // its text is all selected: the next key, or Backspace, replaces it
    double field_click_at;              // when a text field was last clicked, for a double click on it
    char field_clicked[CONSOLE_NAME_SIZE]; // and which one, so both clicks must be the same box
    int capturing;                      // the controls row waiting for a key, -1 for none
    char capture_mod[16];               // a modifier pressed while it waits: alone when let go, else with the next key
    bool clicked;                       // a left click since the last draw, at the cursor
    int wheel;                          // the wheel's notches since the last draw, up positive
    int map_scroll;                     // the map list's first row shown
    int map_cursor;                     // the map list's row with the keys
    int server_scroll;                  // the server list's first row shown
    QueryAddress server_selected;       // the server picked in the list; port 0 for none
    double server_clicked_at;           // when it was picked, so a second click soon after joins it
    ServerSort server_sort;
    bool server_sort_up;                // ascending, against the column's natural order
    bool hide_empty, hide_full, only_compatible;
    char search[MAINMENU_SEARCH];       // the server list's filter, by name or map
    int taunt_slot;                     // the taunt the editor has loaded, -1 for none
    int taunt_mod;                      // its modifier: alt, ctrl or shift, as taunts.h names them
    TauntMode taunt_mode;               // who hears the message: everyone or the team (a radio call's goes to the team)
    int taunt_radio;                    // the radio call attached, 0 for none, else 1 to 9
    char taunt_text[CONSOLE_VALUE_SIZE]; // the message being edited, a radio call's own words
    int demo_scroll;                    // the demo list's first row shown
    char demo_selected[64];             // the demo picked in the list; empty for none
    double demo_clicked_at;             // when it was picked, so a second click soon after plays it
    char command[256];                  // for the app to run; empty for none
    double time;                        // seconds, for the caret's blink
    bool joined;                        // a server has us: Resume, and Escape, go back to it
    bool connect_asked;                 // Connect was pressed: the console's word on it shows under it
} MainMenu;

void mainmenu_show(MainMenu *m, bool shown);

// A key, a mouse button, a controller's button or text: the menu's while shown. True if
// it took the event.
bool mainmenu_event(MainMenu *m, Console *con, const SDL_Event *e);

// The menu over the frame, in the HUD's units (the view is 480 tall, `game_width` wide;
// `pixel` is one window pixel in units). `cursor` is the input's, in those units.
// `status` is a line for the join and local pages (the console's last), `joined` whether
// a server has us, `hosting` whether it is our own, `maps` the maps under assets for the
// rotation, `weapons` names the loadout, the gostek and anims draw the preview,
// `browser` is the server list (the `browse` command asks for it anew), and `demos` the
// demos kept in demos/, newest first.
void mainmenu_draw(MainMenu *m, Console *con, const Interface *hud, const Gostek *gostek, const Context *ctx,
                   Vec2 cursor, float game_width, float pixel, double time, const char *status,
                   bool joined, bool hosting, const char (*maps)[64], int map_count, const Browser *browser,
                   const DemoListing *demos, int demo_count);

// A command the menu asked for since last taken: true, with it, once.
bool mainmenu_take_command(MainMenu *m, char *out, size_t size);

// The menu on `page`, the rail with the keys.
void mainmenu_open_page(MainMenu *m, MainPage page);
