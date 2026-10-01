#pragma once

// The in-game menus: the escape menu, the team menu, the weapons (limbo) menu, and the
// kick and map windows the escape menu opens. Ported from opensoldat's GameMenus.pas:
// each is a box of buttons in the interface's units, one hovered by the cursor, one
// chosen by a click or its number key. What a choice means is handed back as a
// MenuAction for the app to carry out; nothing here touches the game.

#include <stdbool.h>

#include "game/entities.h"
#include "ui/hud_data.h"

#define MENU_MAX_BUTTONS 16
#define MENU_CAPTION 48

typedef struct MenuButton {
    bool active;
    float x1, y1, x2, y2;
    char caption[MENU_CAPTION];
} MenuButton;

typedef struct GameMenu {
    bool active;
    float x, y, w, h;
    MenuButton buttons[MENU_MAX_BUTTONS];
    int button_count;
} GameMenu;

typedef enum MenuId { MENU_ESC, MENU_TEAM, MENU_LIMBO, MENU_KICK, MENU_MAP, MENU_COUNT } MenuId;

typedef struct GameMenus {
    GameMenu menus[MENU_COUNT];
    int hovered_menu, hovered_button; // -1 for none
    bool limbo_was_active;            // the weapons menu comes back when the escape menu closes
    bool noob_show;                   // the keys help in the escape menu (cl_runs < 3)
    int kick_index;                   // the player the kick window shows
    int map_index;                    // the map the map window shows
    bool weapons_active[WEAPON_COUNT]; // what the server allows (WeaponActive)
} GameMenus;

typedef enum MenuActionKind {
    MENU_ACTION_NONE,
    MENU_ACTION_QUIT,           // exit to the (not yet existing) main menu
    MENU_ACTION_PICK_PRIMARY,   // value: the WeaponId
    MENU_ACTION_PICK_SECONDARY, // value: the WeaponId
    MENU_ACTION_PICK_TEAM,      // value: the Team
    MENU_ACTION_KICK,           // value: the player
    MENU_ACTION_VOTE_MAP,       // value: the map index
    MENU_ACTION_OPEN_TEAM_MENU, // the escape menu asks for it; the app knows the mode
    MENU_ACTION_CLOSED,         // a menu changed and nothing more: the click is used up
} MenuActionKind;

typedef struct MenuAction {
    MenuActionKind kind;
    int value;
} MenuAction;

// The menus laid out for a view `game_width` units wide, with the weapons' names.
void menus_init(GameMenus *m, float game_width, const Weapons *weapons);

// Opens or closes a menu, with the original's side effects: the escape menu hides the
// rest and brings the weapons menu back when it closes; the team menu offers the
// teams the mode has.
void menus_show(GameMenus *m, MenuId id, bool show, HudGameMode mode, int player_count);
void menus_hide_all(GameMenus *m);
bool menus_any_active(const GameMenus *m);

// The cursor, in the interface's units, over the buttons.
void menus_mouse_move(GameMenus *m, Vec2 cursor);

// A click where the cursor is. A click beside the weapons menu closes it, as the
// original lets a player who left it open start moving.
MenuAction menus_click(GameMenus *m, bool weapon_chosen);

// A number key: the weapons menu's and team menu's shortcuts, the escape menu's.
MenuAction menus_number_key(GameMenus *m, int digit);

// Ctrl and a number, 1 to 4, while the weapons menu is open (the original's shortcut):
// the secondary in that place of it (colt, knife, chainsaw, LAW); nothing for one the
// server disallows, and nothing with the menu closed.
MenuAction menus_secondary_key(GameMenus *m, int digit);
