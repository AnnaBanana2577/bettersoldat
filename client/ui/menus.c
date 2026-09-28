#include "ui/menus.h"

#include <stdio.h>
#include <string.h>

#define PRIMARY_WEAPONS 10
#define MAIN_WEAPONS 14
#define GAME_HEIGHT_UNITS 480.0f

static void button_init(GameMenu *menu, int i, const char *caption, float x, float y, float w, float h, bool active)
{
    MenuButton *b = &menu->buttons[i];
    b->active = active;
    b->x1 = menu->x + x;
    b->y1 = menu->y + y;
    b->x2 = menu->x + x + w;
    b->y2 = menu->y + y + h;
    snprintf(b->caption, sizeof(b->caption), "%s", caption);
    if (i >= menu->button_count) menu->button_count = i + 1;
}

// The original's InitGameMenus, its numbers.
void menus_init(GameMenus *m, float game_width, const Weapons *weapons)
{
    memset(m, 0, sizeof(*m));
    m->hovered_menu = m->hovered_button = -1;
    m->kick_index = 0;
    for (int i = 0; i < WEAPON_COUNT; i++) m->weapons_active[i] = true;
    char caption[MENU_CAPTION];

    GameMenu *esc = &m->menus[MENU_ESC];
    esc->w = 300;
    esc->h = 200;
    esc->x = roundf((game_width - esc->w) / 2);
    esc->y = roundf((GAME_HEIGHT_UNITS - esc->h) / 2);
    button_init(esc, 0, "1 Exit to menu", 5, 1 * 25, 240, 25, true);
    button_init(esc, 1, "2 Change map", 5, 2 * 25, 240, 25, true);
    button_init(esc, 2, "3 Kick player", 5, 3 * 25, 240, 25, true);
    button_init(esc, 3, "4 Change team", 5, 4 * 25, 240, 25, true);

    GameMenu *team = &m->menus[MENU_TEAM];
    button_init(team, 0, "0 0 Player", 40, 140 + 40 * 1, 215, 35, true);
    button_init(team, 1, "1 Alpha Team", 40, 140 + 40 * 1, 215, 35, true);
    button_init(team, 2, "2 Bravo Team", 40, 140 + 40 * 2, 215, 35, true);
    button_init(team, 3, "3 Charlie Team", 40, 140 + 40 * 3, 215, 35, true);
    button_init(team, 4, "4 Delta Team", 40, 140 + 40 * 4, 215, 35, true);
    button_init(team, 5, "5 Spectator", 40, 140 + 40 * 5, 215, 35, true);

    GameMenu *limbo = &m->menus[MENU_LIMBO];
    for (int i = 0; i < MAIN_WEAPONS; i++) {
        const char *name = weapons->info[i + 1].name;
        if (i < PRIMARY_WEAPONS) snprintf(caption, sizeof(caption), "%d %s", (i + 1) % 10, name);
        else snprintf(caption, sizeof(caption), "%s", name);
        button_init(limbo, i, caption, 35, (float)(154 + 18 * (i + (i >= PRIMARY_WEAPONS))), 235, 16, true);
    }

    GameMenu *kick = &m->menus[MENU_KICK];
    kick->w = 370;
    kick->h = 90;
    kick->x = 125;
    kick->y = 355;
    button_init(kick, 0, "<<<<", 15, 35, 90, 25, true);
    button_init(kick, 1, ">>>>", 265, 35, 90, 25, true);
    button_init(kick, 2, "Kick", 105, 55, 90, 25, true);
    button_init(kick, 3, "Ban", 195, 55, 80, 25, false); // not supported, as the original's

    GameMenu *map = &m->menus[MENU_MAP];
    map->w = 370;
    map->h = 90;
    map->x = 125;
    map->y = 355;
    button_init(map, 0, "<<<<", 15, 35, 90, 25, true);
    button_init(map, 1, ">>>>", 265, 35, 90, 25, true);
    button_init(map, 2, "Select", 120, 55, 90, 25, true);
}

void menus_hide_all(GameMenus *m)
{
    for (int i = 0; i < MENU_COUNT; i++) m->menus[i].active = false;
}

bool menus_any_active(const GameMenus *m)
{
    for (int i = 0; i < MENU_COUNT; i++) {
        if (m->menus[i].active) return true;
    }
    return false;
}

void menus_show(GameMenus *m, MenuId id, bool show, HudGameMode mode, int player_count)
{
    GameMenu *menu = &m->menus[id];
    bool skip = false;
    switch (id) {
    case MENU_ESC:
        if (show) {
            if (m->menus[MENU_LIMBO].active) m->limbo_was_active = true;
            menus_hide_all(m);
        } else {
            menus_hide_all(m);
            m->noob_show = false;
            if (m->limbo_was_active) m->menus[MENU_LIMBO].active = true;
        }
        break;
    case MENU_TEAM:
        if (show) {
            menus_hide_all(m);
            MenuButton *b = menu->buttons;
            bool teams = mode == HUD_MODE_CTF || mode == HUD_MODE_INF || mode == HUD_MODE_HTF;
            bool four = mode == HUD_MODE_TEAMMATCH;
            b[0].active = !teams && !four;
            b[1].active = b[2].active = teams || four;
            b[3].active = b[4].active = four;
        }
        break;
    case MENU_MAP:
        if (show) m->menus[MENU_KICK].active = false;
        break;
    case MENU_KICK:
        if (show) {
            m->kick_index = 0;
            m->menus[MENU_MAP].active = false;
            if (player_count < 1) skip = true;
        }
        break;
    case MENU_LIMBO:
        menu->active = false;
        if (!show) m->limbo_was_active = false;
        break;
    default: break;
    }
    if (!skip) menu->active = show;
    m->hovered_menu = m->hovered_button = -1;
}

void menus_mouse_move(GameMenus *m, Vec2 cursor)
{
    m->hovered_menu = m->hovered_button = -1;
    for (int i = 0; i < MENU_COUNT; i++) {
        const GameMenu *menu = &m->menus[i];
        if (!menu->active) continue;
        for (int j = 0; j < menu->button_count; j++) {
            const MenuButton *b = &menu->buttons[j];
            if (b->active && cursor.x > b->x1 && cursor.x < b->x2 && cursor.y > b->y1 && cursor.y < b->y2) {
                m->hovered_menu = i;
                m->hovered_button = j;
                return;
            }
        }
    }
}

// The original's GameMenuAction: what a button does.
static MenuAction menu_action(GameMenus *m, MenuId id, int button)
{
    MenuAction none = {MENU_ACTION_NONE, 0};
    GameMenu *menu = &m->menus[id];
    if (button < 0 || button >= menu->button_count || !menu->buttons[button].active) return none;

    switch (id) {
    case MENU_ESC:
        switch (button) {
        case 0: return (MenuAction){MENU_ACTION_QUIT, 0};
        case 1: menus_show(m, MENU_MAP, !m->menus[MENU_MAP].active, 0, 1); return (MenuAction){MENU_ACTION_CLOSED, 0};
        case 2: menus_show(m, MENU_KICK, !m->menus[MENU_KICK].active, 0, 1); return (MenuAction){MENU_ACTION_CLOSED, 0};
        case 3: return (MenuAction){MENU_ACTION_OPEN_TEAM_MENU, 0};
        default: return none;
        }
    case MENU_TEAM:
        menus_show(m, MENU_TEAM, false, 0, 0);
        return (MenuAction){MENU_ACTION_PICK_TEAM, button};
    case MENU_KICK:
        switch (button) {
        case 0: m->kick_index = (m->kick_index + MAX_PLAYERS - 1) % MAX_PLAYERS; return (MenuAction){MENU_ACTION_CLOSED, 0};
        case 1: m->kick_index = (m->kick_index + 1) % MAX_PLAYERS; return (MenuAction){MENU_ACTION_CLOSED, 0};
        case 2: menus_show(m, MENU_ESC, false, 0, 0); return (MenuAction){MENU_ACTION_KICK, m->kick_index};
        default: return none;
        }
    case MENU_MAP:
        switch (button) {
        case 0:
            if (m->map_index > 0) m->map_index--;
            return (MenuAction){MENU_ACTION_CLOSED, 0};
        case 1: m->map_index++; return (MenuAction){MENU_ACTION_CLOSED, 0};
        case 2: menus_show(m, MENU_ESC, false, 0, 0); return (MenuAction){MENU_ACTION_VOTE_MAP, m->map_index};
        default: return none;
        }
    case MENU_LIMBO: {
        WeaponId weapon = (WeaponId)(button + 1);
        if (!m->weapons_active[weapon]) return none;
        if (button < PRIMARY_WEAPONS) {
            menus_show(m, MENU_LIMBO, false, 0, 0);
            return (MenuAction){MENU_ACTION_PICK_PRIMARY, weapon};
        }
        return (MenuAction){MENU_ACTION_PICK_SECONDARY, weapon};
    }
    default: return none;
    }
}

MenuAction menus_click(GameMenus *m, bool weapon_chosen)
{
    if (m->hovered_button >= 0) return menu_action(m, (MenuId)m->hovered_menu, m->hovered_button);
    if (weapon_chosen && m->menus[MENU_LIMBO].active) {
        menus_show(m, MENU_LIMBO, false, 0, 0);
        return (MenuAction){MENU_ACTION_CLOSED, 0};
    }
    return (MenuAction){MENU_ACTION_NONE, 0};
}

MenuAction menus_number_key(GameMenus *m, int digit)
{
    if (m->menus[MENU_LIMBO].active) return menu_action(m, MENU_LIMBO, digit == 0 ? 9 : digit - 1);
    if (m->menus[MENU_TEAM].active) return menu_action(m, MENU_TEAM, digit);
    if (m->menus[MENU_ESC].active && digit >= 1) return menu_action(m, MENU_ESC, digit - 1);
    return (MenuAction){MENU_ACTION_NONE, 0};
}
