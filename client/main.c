// The client. Reads as what it is: each subsystem opened, the loop, each closed.
//
//   console the cvars, the commands and the binds (shared/console), run first: the
//           default binds, then config.cfg, then the command line
//   game    the world (shared/game), for now ticked here with authority: a local
//           sandbox with one soldier, until the connection to a server is ported
//   input   the keys and mouse, through the binds (input/)
//   gfx     the window's GL context and everything drawn into it (gfx/)
//   render  the world's picture: camera, map, soldiers, and the HUD over it (render/)
//
// Each tick: the game's tick on this frame's input, and a snapshot of it. Each frame: a
// RenderState built between the last two snapshots (render/render_state.h), the camera
// following me in it, and the world drawn from it. Everything the client is lives in
// App; nothing else is global.
//
// The client runs from the directory that holds config.cfg and assets/: the project's
// own in development (xmake run starts it there) and the game's own once shipped, so
// both are found by the same relative paths. config.cfg lists every cvar with its
// default and every bind, and on the way out the binds and the saved cvars are written
// back into it, in place, with its comments kept (console_save).
//
//   client [+assets <dir>] [+map <name>] [+<cvar> <value>] [+<command> <args>...]
//
// so `client +map ctf_Ash +r_screenwidth 1920 +r_screenheight 1080`, or `+hud_demo 2`
// for the HUD full of sample data, or `+screenshot out.png` for a PNG of the 60th frame,
// or `+connect localhost` to join a server (net/client_net.c). The world stays the local
// sandbox until the server's state comes down the line.
//
// The binds below are the fallback for a missing config.cfg; the file's are the ones
// that count. Alt held is the radio menu (+radio): a call by its number, then a
// place by its, said to the team from the radio_* cvars. Alt with a letter is a taunt
// in the config (say, say_team). The view's: Escape the menu, Tab the weapons, M the
// teams, F1 the scoreboard, F2 the weapon stats, F3 the minimap (ui_minimap), F5 the
// FPS line (ui_info), F7 the names (ui_playernames). F9 toggles the wireframe
// (r_wireframe), F10 the debug overlay (r_debug), F4 vsync (r_swapeffect, off as the
// original's default). There is no zoom: everyone sees the same 480 units of height.

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console/console.h"
#include "game/game.h"
#include "game/systems/systems.h"
#include "gfx/font.h"
#include "gfx/gfx.h"
#include "input/input.h"
#include "net/client_net.h"
#include "render/interface.h"
#include "render/render.h"
#include "render/textures.h"
#include "render/scale_data.h"
#include <ctype.h>
#include <math.h>

#include "audio/audio.h"
#include "ui/consoles.h"
#include "ui/feed.h"
#include "ui/mainmenu.h"
#include "ui/menus.h"

#define MAX_FRAME 0.25 // a stall never turns into a burst of ticks
#define CONFIG "config.cfg"
#define SCREENSHOT_FRAME 60
#define RADIO_CALLS 3  // the radio menu's first choices, and each one's second choices
#define CURSORSPRITE_DISTANCE 15.0f // the original's: how near the cursor names a player
#define SPECTATORAIMDIST 30.0f      // the original's: the free camera's speed, by the cursor's offset

// The original's frame pacing, its defaults: vsync off (r_swapeffect 0), frames no closer
// than 1/500 s (r_fpslimit, r_maxfps), and a millisecond's sleep after each so the loop
// never spins flat out (r_sleeptime).
#define MIN_FRAME_SECONDS (1.0 / 500.0)
#define SLEEP_AFTER_FRAME_MS 1

// The view's keys, bound to the cvars and commands below: the config's defaults.
static const char *VIEW_BINDS =
    "bind escape escmenu; bind tab weaponsmenu; bind m teammenu; bind f1 fragsmenu; bind f2 statsmenu;"
    "bind f3 \"toggle ui_minimap\"; bind f4 \"toggle r_swapeffect\"; bind f5 \"toggle ui_info\";"
    "bind f7 \"toggle ui_playernames\"; bind f9 \"toggle r_wireframe\"; bind f10 \"toggle r_debug\";"
    "bind alt +radio; bind t chat; bind y teamchat; bind slash cmd; bind f12 \"say /yes\"; bind f11 \"say /no\"";

typedef struct App {
    Console *console; // large; on the heap
    Cvar *assets;     // the opensoldat base assets: maps/, anims/, objects/, textures/...
    Cvar *map;
    Cvar *width, *height; // the window
    Cvar *swapeffect;     // vsync
    Cvar *fullscreen;     // 0 windowed, 1 fullscreen, 2 borderless
    Cvar *server;         // the address the main menu joins
    Cvar *sensitivity;
    Cvar *wireframe, *debug;
    Cvar *minimap, *info, *player_names, *console_length;
    Cvar *player_name;
    Cvar *shirt, *pants, *skin, *hair, *jet;      // the look's colours, "RRGGBB"
    Cvar *hair_style, *head_style, *chain_style;  // and its styles, by number
    Cvar *primary, *secondary;                    // the loadout at the next spawn
    Cvar *smooth;                                 // milliseconds a correction of another player is smoothed over
    Cvar *volume;                                 // snd_volume, 0 to 100
    Cvar *radio_first[RADIO_CALLS];               // the radio menu's calls
    Cvar *radio_second[RADIO_CALLS][RADIO_CALLS]; // and each call's places
    Cvar *hud_demo;       // the HUD full of sample data, to see every part of it: page 1, 2 or 3
    char screenshot[512]; // a PNG of the 60th frame, then quit

    Game *game; // large; on the heap
    int me;     // my soldier: 0 in the local sandbox, the slot the server gave me online
    SDL_Window *window;
    Input input;
    ClientNet net; // the line to a server, once `connect` opens one
    uint32_t seq; // my commands, numbered
    bool chat_just_opened; // the key that opened the prompt is not its first letter
    int chat_completing;   // Tab: the player last completed to, index + 1; 0 when not completing
    int chat_complete_from;
    char chat_complete_base[HUD_TEXT];
    char chat_last[HUD_TEXT]; // the last line sent, for "//" to bring back
    HudChatType chat_last_type;
    Consoles consoles;        // the HUD's two consoles, fed from the game console's scrollback
    bool vote_reason_typing;  // the prompt takes a kick vote's reason (the kick window's OK)
    int kick_target;          // the player it is about
    char maps[128][64];       // the maps under assets, for the map window
    int map_count;
    bool was_dead;         // my soldier as of the last tick, for the weapons menu at death
    int seen_life;         // the life the weapons menu last opened for: a new one opens it again
    bool team_asked;       // the team menu shown for this round's join
    // Watching: the player the camera follows (-1 for me), or the free camera, moved by
    // the cursor's offset from the middle, as the original's spectator has it.
    int camera_follow;
    bool free_camera;
    Buttons camera_keys; // last tick's, so a press switches once
    bool limbo_lock;       // the weapons menu closed while dead stays closed (the original's LimboLock)
    double accumulator;
    bool quit;

    // The two ticks each frame is drawn between, and the frame built from them.
    TickSnapshot previous, latest;
    RenderState frame;

    GameCamera camera;
    Render render;
    RenderOptions render_options;
    ScaleData scales; // mod.ini: how big each image is
    Interface hud;
    HudData hud_data; // what the HUD shows beyond the frame: filled here from what there is
    Feed feed;        // the kill console and the big messages, from the ticks' events
    MainMenu mainmenu;
    ClientNetState net_state_seen; // as of the last frame: the menu goes on joining, comes back on losing the line
    Audio audio;      // what is heard, from the ticks' events and the soldiers
    GameMenus menus;
    double time;      // seconds since the start

    // the frame rate, counted over each second for the title
    int frames;
    double frame_timer;
    int fps;
} App;

static void print_stdout(const char *text, void *user)
{
    (void)user;
    fputs(text, stdout);
}

static bool file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

static void cmd_quit(Console *con, int argc, char **argv, void *user)
{
    (void)con, (void)argc, (void)argv;
    ((App *)user)->quit = true;
}

// screenshot <file.png>: the 60th frame from now, then quit.
static void cmd_screenshot(Console *con, int argc, char **argv, void *user)
{
    App *app = user;
    if (argc != 2) {
        console_print(con, "usage: screenshot <file.png>\n");
        return;
    }
    snprintf(app->screenshot, sizeof app->screenshot, "%s", argv[1]);
}

// The original's chat constants (Constants.pas).
#define MORECHATTEXT 60   // a longer line is split in the console and not shown over the head
#define MAXCHATTEXT 85    // as much as the prompt takes
#define CHARDELAY 25      // ticks a line stays over the head, by its letters when it is one word,
#define SPACECHARDELAY 68 // by its words otherwise
#define MAX_CHATDELAY (7 * 60 + 40)

static void player_name(const App *app, int i, char *name, size_t size);

// A line of chat heard, from `slot` (MAX_PLAYERS for the server itself), placed as the
// original's ClientHandleChatMessage places it: to the console as "[Name] text" in the
// chat's colour, "(TEAM) [Name] text" in the team's, a long line in two; and over the
// speaker's head for a while its words decide. The server's own lines are its chat
// ("*SERVER*: text") or, marked `team`, a plain line of the game's: who came and went.
static void chat_heard(App *app, int slot, bool team, const char *text)
{
    Console *con = app->console;
    if (slot == MAX_PLAYERS) {
        if (team) console_print_color(con, HUD_COLOR_ENTER, "%s\n", text);
        else console_print_color(con, HUD_COLOR_SERVER, "*SERVER*: %s\n", text);
        return;
    }
    char name[HUD_NAME];
    player_name(app, slot, name, sizeof name);
    Rgba color = team ? HUD_COLOR_TEAMCHAT : HUD_COLOR_CHAT;
    const char *prefix = team ? "(TEAM) " : "";
    if (strlen(text) < MORECHATTEXT) console_print_color(con, color, "%s[%s] %s\n", prefix, name, text);
    else console_print_color(con, color, "%s[%s] \n %s\n", prefix, name, text);

    HudPlayer *p = &app->hud_data.players[slot];
    snprintf(p->chat, sizeof p->chat, "%s", text);
    p->chat_team = team;
    int spaces = 0;
    for (const char *s = text; *s; s++) spaces += *s == ' ';
    p->chat_delay = spaces == 0 ? (int)strlen(text) * CHARDELAY : spaces * SPACECHARDELAY;
    if (p->chat_delay > MAX_CHATDELAY) p->chat_delay = MAX_CHATDELAY;
}

// Something I say: to the server, which says it back to everyone, me among them; alone,
// straight to my own console and head.
static void say(App *app, bool team, const char *text)
{
    if (!text[0]) return;
    if (client_net_say(&app->net, text, team)) return;
    chat_heard(app, app->me, team, text);
}

// say <text...> / say_team <text...>: chat, as a command (the taunt binds use it).
static void cmd_say(Console *con, int argc, char **argv, void *user)
{
    App *app = user;
    if (argc < 2) {
        console_print(con, "usage: %s <text>\n", argv[0]);
        return;
    }
    char text[HUD_TEXT];
    size_t n = 0;
    text[0] = '\0';
    for (int i = 1; i < argc && n < sizeof text - 1; i++) {
        int w = snprintf(text + n, sizeof text - n, i > 1 ? " %s" : "%s", argv[i]);
        if (w < 0) break;
        n += (size_t)w; // past the end once it is full, and the loop ends
    }
    say(app, strcmp(argv[0], "say_team") == 0, text);
}

// votemap <map> / votekick <player>: a vote, which is a command said in the chat for
// the server to read; /yes and /no answer it (F12 and F11).
static void cmd_vote(Console *con, int argc, char **argv, void *user)
{
    App *app = user;
    if (argc != 2) {
        console_print(con, "usage: %s <%s>\n", argv[0], strcmp(argv[0], "votemap") == 0 ? "map" : "player");
        return;
    }
    char text[HUD_TEXT];
    snprintf(text, sizeof text, "/%s %s", argv[0], argv[1]);
    say(app, false, text);
}

// The prompt (ControlGame.pas StartChat, ClearChatText). Its text begins with the
// mode's own character, a space for a line said and a slash for a command, which the
// drawing shows after "Say:" or "Cmd: " and the sending drops; deleting it closes the
// prompt. The keys are the prompt's until Enter sends the line or Escape drops it.
static void chat_open(App *app, HudChatType type)
{
    HudData *d = &app->hud_data;
    if (d->chat_type != HUD_CHAT_NONE) return;
    d->chat_type = type;
    snprintf(d->chat_text, sizeof d->chat_text, "%s", type == HUD_CHAT_COMMAND ? "/" : " ");
    d->chat_cursor = (int)strlen(d->chat_text);
    d->chat_changed_at = app->time;
    app->chat_completing = 0;
    app->chat_just_opened = true;
    SDL_StartTextInput();
}

static void chat_close(App *app)
{
    app->hud_data.chat_type = HUD_CHAT_NONE;
    app->vote_reason_typing = false;
    app->hud_data.vote_reason_typing = false;
    app->hud_data.chat_text[0] = '\0';
    app->chat_completing = 0;
    SDL_StopTextInput();
}

// chat / teamchat / cmd: the original's T, Y and /.
static void cmd_chat(Console *con, int argc, char **argv, void *user)
{
    (void)con, (void)argc;
    App *app = user;
    HudChatType type = strcmp(argv[0], "teamchat") == 0 ? HUD_CHAT_TEAM
                       : strcmp(argv[0], "cmd") == 0    ? HUD_CHAT_COMMAND
                                                        : HUD_CHAT_PUBLIC;
    chat_open(app, type);
}

static bool contains_nocase(const char *haystack, const char *needle)
{
    size_t n = strlen(needle);
    for (const char *h = haystack; *h; h++) {
        size_t i = 0;
        while (i < n && h[i] && tolower((unsigned char)h[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n) return true;
    }
    return false;
}

// Tab in the prompt (ClientGame.pas TabComplete): the word the line ends with becomes
// the name of a player that contains it, another player's with each press.
static void chat_complete(App *app)
{
    HudData *d = &app->hud_data;
    char *text = d->chat_text;
    int len = (int)strlen(text);
    if (len <= 1) return; // the mode's character alone
    if (app->chat_completing == 0) { // the base: the word after the last space
        const char *sep = strrchr(text, ' ');
        int from = sep ? (int)(sep - text) + 1 : 1;
        if (from < 1) from = 1;
        app->chat_complete_from = from;
        snprintf(app->chat_complete_base, sizeof app->chat_complete_base, "%s", text + from);
    }
    for (int n = 0; n < MAX_PLAYERS; n++) {
        int i = (app->chat_completing + n) % MAX_PLAYERS; // from the one after the last completed
        if (i == app->me || !app->game->world.soldiers[i].active) continue;
        char name[HUD_NAME];
        player_name(app, i, name, sizeof name);
        if (app->chat_complete_base[0] && !contains_nocase(name, app->chat_complete_base)) continue;
        int room = MAXCHATTEXT - app->chat_complete_from;
        if (room < 0) room = 0;
        snprintf(text + app->chat_complete_from, sizeof d->chat_text - (size_t)app->chat_complete_from, "%.*s", room, name);
        d->chat_cursor = (int)strlen(text);
        app->chat_completing = i + 1;
        d->chat_changed_at = app->time;
        return;
    }
}

// Enter in the prompt: a command runs here if the console knows it (a cvar, a command),
// else it goes to the server, which reads votes and the like; a line said goes to
// everyone or the team, without the mode's character.
static void chat_send(App *app)
{
    HudData *d = &app->hud_data;
    char line[HUD_TEXT];
    snprintf(line, sizeof line, "%s", d->chat_text);
    HudChatType type = d->chat_type;
    snprintf(app->chat_last, sizeof app->chat_last, "%s", line);
    app->chat_last_type = type;
    chat_close(app);
    if (app->vote_reason_typing) { // the kick window's reason: the vote, with it, if enough was typed
        app->vote_reason_typing = false;
        app->hud_data.vote_reason_typing = false;
        if (strlen(line) > 3) {
            char text[HUD_TEXT];
            snprintf(text, sizeof text, "/votekick %d %.*s", app->kick_target, NET_REASON_SIZE - 1, line + 1);
            say(app, false, text);
        }
        return;
    }
    if (line[0] == '/') {
        char word[HUD_TEXT] = "";
        sscanf(line + 1, "%159s", word);
        if (word[0] && console_knows(app->console, word)) console_execute(app->console, line + 1);
        else if (word[0]) say(app, false, line);
        return;
    }
    if (line[1]) say(app, type == HUD_CHAT_TEAM, line + 1);
}

// Text into the prompt at the cursor, as much as fits.
static void chat_insert(App *app, const char *str)
{
    HudData *d = &app->hud_data;
    char *text = d->chat_text;
    size_t len = strlen(text), add = strlen(str);
    int at = clampi(d->chat_cursor, 0, (int)len);
    if (len >= MAXCHATTEXT) return;
    if (len + add >= sizeof d->chat_text) add = sizeof d->chat_text - 1 - len;
    memmove(text + at + add, text + at, len - (size_t)at + 1);
    memcpy(text + at, str, add);
    d->chat_cursor = at + (int)add;
    app->chat_completing = 0;
    d->chat_changed_at = app->time;
}

// Typing: the text and the keys that edit it, the original's (ControlGame.pas). Keys
// going up still reach the binds, so what was held before the prompt opened is let go
// of; nothing going down does, as the prompt has them.
static bool chat_event(App *app, const SDL_Event *e)
{
    HudData *d = &app->hud_data;
    if (d->chat_type == HUD_CHAT_NONE) return false;
    char *text = d->chat_text;
    int len = (int)strlen(text);
    int at = clampi(d->chat_cursor, 0, len);

    if (e->type == SDL_TEXTINPUT) {
        if (app->chat_just_opened) return true; // the key that opened it
        char str[SDL_TEXTINPUTEVENT_TEXT_SIZE];
        snprintf(str, sizeof str, "%s", e->text.text);
        for (char *s = str; *s; s++)
            if (*s == '\n' || *s == '\r') *s = ' ';
        // "//" brings the last line back to be sent again
        if (strcmp(text, "/") == 0 && strcmp(str, "/") == 0 && strlen(app->chat_last) > 1) {
            snprintf(text, sizeof d->chat_text, "%s", app->chat_last);
            d->chat_type = app->chat_last_type;
            d->chat_cursor = (int)strlen(text);
            app->chat_completing = 0;
            d->chat_changed_at = app->time;
            return true;
        }
        chat_insert(app, str);
        return true;
    }
    if (e->type != SDL_KEYDOWN) return false;

    bool ctrl = (e->key.keysym.mod & KMOD_CTRL) != 0;
    SDL_Scancode key = e->key.keysym.scancode;
    if (ctrl && key == SDL_SCANCODE_V) { // paste
        char *clip = SDL_GetClipboardText();
        if (clip) {
            chat_insert(app, clip);
            SDL_free(clip);
        }
        return true;
    }
    if (ctrl && key == SDL_SCANCODE_C) { // the big console to the clipboard
        char *all = consoles_big_text(&app->consoles);
        if (all && SDL_SetClipboardText(all) == 0) console_print_color(app->console, HUD_COLOR_GAME, "Copied chat contents to clipboard\n");
        else console_print_color(app->console, HUD_COLOR_DEBUG, "Failed copying chat to clipboard: %s\n", SDL_GetError());
        free(all);
        return true;
    }
    switch (key) {
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: chat_send(app); return true;
    case SDL_SCANCODE_ESCAPE: chat_close(app); return true;
    case SDL_SCANCODE_BACKSPACE:
        if (at > 1 || len == 1) {
            memmove(text + at - 1, text + at, (size_t)(len - at) + 1);
            d->chat_cursor = at - 1;
            app->chat_completing = 0;
            if (text[0] == '\0') chat_close(app);
        }
        break;
    case SDL_SCANCODE_DELETE:
        if (len > at) {
            memmove(text + at, text + at + 1, (size_t)(len - at));
            app->chat_completing = 0;
        }
        break;
    case SDL_SCANCODE_HOME: d->chat_cursor = 1; break;
    case SDL_SCANCODE_END: d->chat_cursor = len; break;
    case SDL_SCANCODE_RIGHT:
        if (ctrl) { // to the start of the next word
            while (at < len) {
                at++;
                if (at == len || (text[at - 1] == ' ' && text[at] != ' ')) break;
            }
            d->chat_cursor = at;
        } else if (len > at) {
            d->chat_cursor = at + 1;
        }
        break;
    case SDL_SCANCODE_LEFT:
        if (ctrl) { // to the start of this word, or the one before
            while (at > 1) {
                at--;
                if (text[at - 1] == ' ' && text[at] != ' ') break;
            }
            d->chat_cursor = at;
        } else if (at > 1) {
            d->chat_cursor = at - 1;
        }
        break;
    case SDL_SCANCODE_TAB: chat_complete(app); break;
    default: break;
    }
    d->chat_changed_at = app->time;
    return true;
}

// connect <address[:port]> / disconnect: the line to a server. The join and what comes
// down the line are the console's to report (net/client_net.c).
static void cmd_connect(Console *con, int argc, char **argv, void *user)
{
    App *app = user;
    if (argc != 2) {
        console_print(con, "usage: connect <address[:port]>\n");
        return;
    }
    char address[128];
    snprintf(address, sizeof address, "%s", argv[1]);
    uint16_t port = NET_DEFAULT_PORT;
    char *colon = strrchr(address, ':');
    if (colon) {
        *colon = '\0';
        port = (uint16_t)atoi(colon + 1);
    }
    client_net_connect(&app->net, con, address, port, app->player_name->value);
}

static void cmd_disconnect(Console *con, int argc, char **argv, void *user)
{
    (void)argc, (void)argv;
    client_net_disconnect(&((App *)user)->net, con);
}

// +radio / -radio: the radio menu, shown while the key is held. The digits choose
// (menu_event): a call, then its place, and the two are said to the team.
static void cmd_radio(Console *con, int argc, char **argv, void *user)
{
    (void)con, (void)argc;
    App *app = user;
    app->hud_data.radio_menu = argv[0][0] == '+';
    app->hud_data.radio_state = 0;
}

// The radio menu's digit: the call, or the place that finishes the message.
static void radio_choose(App *app, int digit)
{
    HudData *d = &app->hud_data;
    if (digit < 1 || digit > RADIO_CALLS) return;
    if (!d->radio_state) {
        d->radio_state = digit;
        return;
    }
    char text[CONSOLE_TEXT_SIZE];
    snprintf(text, sizeof text, "say_team \"%s %s\"", app->radio_first[d->radio_state - 1]->value,
             app->radio_second[d->radio_state - 1][digit - 1]->value);
    console_execute(app->console, text);
    d->radio_menu = false;
    d->radio_state = 0;
}

// escmenu / weaponsmenu / teammenu / fragsmenu / statsmenu: each toggles its menu. The
// scoreboard and the stats sit in the same place, so one closes the other, and neither
// opens over the escape menu.
static void cmd_menu(Console *con, int argc, char **argv, void *user)
{
    (void)con, (void)argc;
    App *app = user;
    HudData *d = &app->hud_data;
    GameMenus *m = &app->menus;
    const char *name = argv[0];
    if (strcmp(name, "escmenu") == 0) menus_show(m, MENU_ESC, !m->menus[MENU_ESC].active, d->mode, 1);
    else if (strcmp(name, "weaponsmenu") == 0) {
        menus_show(m, MENU_LIMBO, !m->menus[MENU_LIMBO].active, d->mode, 1);
        // closed while dead, it stays closed through the spawn; opened again, it comes back
        if (app->game->world.soldiers[app->me].dead) {
            app->limbo_lock = !m->menus[MENU_LIMBO].active;
            console_print_color(con, HUD_COLOR_GAME, app->limbo_lock ? "Weapons menu disabled\n" : "Weapons menu active\n");
        }
    }
    else if (strcmp(name, "teammenu") == 0) menus_show(m, MENU_TEAM, !m->menus[MENU_TEAM].active, d->mode, 1);
    else if (m->menus[MENU_ESC].active) return;
    else if (strcmp(name, "fragsmenu") == 0) {
        d->frags_menu = !d->frags_menu;
        if (d->frags_menu) d->stats_menu = false;
    } else if (strcmp(name, "statsmenu") == 0) {
        d->stats_menu = !d->stats_menu;
        if (d->stats_menu) d->frags_menu = false;
    }
}

// The console and what the client keeps in it, then the binds and settings: the
// built-in defaults, config.cfg over them, and the command line over both.
static bool console_open(App *app, int argc, char *argv[])
{
    Console *con = app->console = console_create(print_stdout, NULL);
    if (!con) return false;

    app->assets = cvar_register(con, "assets", "./assets", 0, "the base assets directory: maps/, anims/, objects/...");
    app->map = cvar_register(con, "map", "Arena", 0, "the map to load");
    app->width = cvar_register(con, "r_screenwidth", "1280", CVAR_ARCHIVE, "the window's width");
    app->height = cvar_register(con, "r_screenheight", "960", CVAR_ARCHIVE, "the window's height");
    app->swapeffect = cvar_register(con, "r_swapeffect", "0", CVAR_ARCHIVE, "wait for the display's refresh (vsync)");
    app->fullscreen = cvar_register(con, "r_fullscreen", "0", CVAR_ARCHIVE, "0 windowed, 1 fullscreen, 2 borderless window");
    app->server = cvar_register(con, "cl_server", "127.0.0.1:23073", CVAR_ARCHIVE, "the server the main menu joins, host:port");
    app->sensitivity = cvar_register(con, "cl_sensitivity", "1", CVAR_ARCHIVE, "the mouse's speed");
    app->wireframe = cvar_register(con, "r_wireframe", "0", 0, "draw the map's polygons as lines");
    app->debug = cvar_register(con, "r_debug", "0", 0, "spawn points, colliders, special polys, bones");
    app->minimap = cvar_register(con, "ui_minimap", "0", CVAR_ARCHIVE, "the minimap");
    app->info = cvar_register(con, "ui_info", "0", CVAR_ARCHIVE, "the FPS and ping line");
    app->player_names = cvar_register(con, "ui_playernames", "1", CVAR_ARCHIVE, "the names over the players");
    app->console_length =
        cvar_register(con, "ui_console_length", "6", CVAR_ARCHIVE, "how many console lines the HUD shows");
    app->player_name = cvar_register(con, "cl_player_name", "Player", CVAR_ARCHIVE, "my name");
    app->shirt = cvar_register(con, "cl_player_shirt", "304289", CVAR_ARCHIVE, "the shirt's colour, RRGGBB");
    app->pants = cvar_register(con, "cl_player_pants", "FF0000", CVAR_ARCHIVE, "the pants' colour, RRGGBB");
    app->skin = cvar_register(con, "cl_player_skin", "E6B478", CVAR_ARCHIVE, "the skin's colour, RRGGBB");
    app->hair = cvar_register(con, "cl_player_hair", "000000", CVAR_ARCHIVE, "the hair's colour, RRGGBB");
    app->jet = cvar_register(con, "cl_player_jet", "00008B", CVAR_ARCHIVE, "the jet flame's colour, RRGGBB");
    app->hair_style = cvar_register(con, "cl_player_hairstyle", "0", CVAR_ARCHIVE,
                                    "0 army, 1 dreadlocks, 2 punk, 3 Mr. T, 4 normal");
    app->head_style = cvar_register(con, "cl_player_headstyle", "0", CVAR_ARCHIVE, "0 none, 1 helmet, 2 hat");
    app->chain_style = cvar_register(con, "cl_player_chainstyle", "0", CVAR_ARCHIVE, "0 none, 1 dog tags, 2 gold chain");
    app->primary = cvar_register(con, "cl_player_wep", "1", CVAR_ARCHIVE, "the primary at the next spawn, 1 to 10");
    app->secondary = cvar_register(con, "cl_player_secwep", "1", CVAR_ARCHIVE, "0 USSOCOM, 1 knife, 2 chainsaw, 3 LAW");
    app->smooth = cvar_register(con, "cl_smooth", "100", CVAR_ARCHIVE,
                                "milliseconds a correction of another player is smoothed over; 0 snaps");
    app->volume = cvar_register(con, "snd_volume", "50", CVAR_ARCHIVE, "the sound's volume, 0 to 100");
    const char *calls[RADIO_CALLS] = {"Enemy flagger", "Friendly flagger", "Enemy spotted"};
    const char *places[RADIO_CALLS] = {"up!", "middle!", "down!"};
    for (int i = 0; i < RADIO_CALLS; i++) {
        char name[CONSOLE_NAME_SIZE];
        snprintf(name, sizeof name, "radio_%d", i + 1);
        app->radio_first[i] = cvar_register(con, name, calls[i], CVAR_ARCHIVE, "a call of the radio menu");
        for (int j = 0; j < RADIO_CALLS; j++) {
            snprintf(name, sizeof name, "radio_%d_%d", i + 1, j + 1);
            app->radio_second[i][j] = cvar_register(con, name, places[j], CVAR_ARCHIVE, "a place of that call");
        }
    }
    app->hud_demo = cvar_register(con, "hud_demo", "0", 0, "fill the HUD with sample data: page 1, 2 or 3");
    console_add_command(con, "quit", cmd_quit, app, "leave the game");
    console_add_command(con, "screenshot", cmd_screenshot, app, "write the 60th frame from now to a PNG, then quit");
    console_add_command(con, "escmenu", cmd_menu, app, "the escape menu");
    console_add_command(con, "weaponsmenu", cmd_menu, app, "the weapons menu");
    console_add_command(con, "teammenu", cmd_menu, app, "the team menu");
    console_add_command(con, "fragsmenu", cmd_menu, app, "the scoreboard");
    console_add_command(con, "statsmenu", cmd_menu, app, "the weapon stats");
    console_add_command(con, "say", cmd_say, app, "say something to everyone");
    console_add_command(con, "say_team", cmd_say, app, "say something to the team");
    console_add_command(con, "chat", cmd_chat, app, "type a line to everyone");
    console_add_command(con, "teamchat", cmd_chat, app, "type a line to the team");
    console_add_command(con, "cmd", cmd_chat, app, "type a command: a cvar or command here, or a word for the server");
    console_add_command(con, "votemap", cmd_vote, app, "start a vote to change the map: votemap <map>");
    console_add_command(con, "votekick", cmd_vote, app, "start a vote to kick a player: votekick <name or slot>");
    console_add_command(con, "+radio", cmd_radio, app, "hold the radio menu open");
    console_add_command(con, "-radio", cmd_radio, app, NULL);
    console_add_command(con, "connect", cmd_connect, app, "join a server: connect <address[:port]>");
    console_add_command(con, "disconnect", cmd_disconnect, app, "leave the server");
    input_init(&app->input, con);

    input_default_binds(con);
    console_execute(con, VIEW_BINDS);
    if (file_exists(CONFIG)) console_execute_file(con, CONFIG);
    console_execute_args(con, argc, argv);
    return true;
}

static void console_close(App *app)
{
    if (!app->console) return;
    if (!console_save(app->console, CONFIG)) fprintf(stderr, "could not save %s\n", CONFIG);
    console_destroy(app->console);
    app->console = NULL;
}

// A team game: the match's mode says, which the map decides alone and the server's
// snapshots carry online.
static bool team_game(const App *app) { return match_has_teams(&app->game->match); }

// A colour cvar's colour; its default's if what it holds isn't one.
static Rgba cvar_color(const Cvar *cv)
{
    Rgba color = {255, 255, 255, 255};
    if (!rgba_parse_hex(cv->value, &color)) rgba_parse_hex(cv->default_value, &color);
    return color;
}

// My look, from the cl_player_* cvars. In a team game the team's shirt goes over it where
// it is drawn (render_state.c), as the team is the server's to give.
static PlayerLook look_from_cvars(const App *app)
{
    PlayerLook look = {
        .shirt = cvar_color(app->shirt),
        .pants = cvar_color(app->pants),
        .skin = cvar_color(app->skin),
        .hair = cvar_color(app->hair),
        .jet = cvar_color(app->jet),
        .hair_style = (uint8_t)clampi(app->hair_style->integer, 0, 4),
        .head_style = (uint8_t)clampi(app->head_style->integer, 0, 2),
        .chain_style = (uint8_t)clampi(app->chain_style->integer, 0, 2),
    };
    return look;
}

// The cvars the loop reads each frame; vsync only once it changes, as it costs a call.
// r_fullscreen: the window as the cvar says, and its size when windowed.
static void apply_window_mode(App *app)
{
    int mode = clampi(app->fullscreen->integer, 0, 2);
    Uint32 flags = mode == 1 ? SDL_WINDOW_FULLSCREEN : mode == 2 ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
    // full screen is the display's own resolution, whatever the window's size was: no
    // mode switch, and the picture matches the screen; the borderless window is that by nature
    if (mode == 1) {
        SDL_DisplayMode desktop;
        int display = SDL_GetWindowDisplayIndex(app->window);
        if (display >= 0 && SDL_GetDesktopDisplayMode(display, &desktop) == 0) SDL_SetWindowDisplayMode(app->window, &desktop);
    }
    if (SDL_SetWindowFullscreen(app->window, flags) != 0) fprintf(stderr, "window mode %d: %s\n", mode, SDL_GetError());
    if (mode == 0) {
        SDL_SetWindowSize(app->window, app->width->integer, app->height->integer);
        SDL_SetWindowPosition(app->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    app->fullscreen->modified = app->width->modified = app->height->modified = false;
}

static void apply_cvars(App *app)
{
    if (app->fullscreen->modified || app->width->modified || app->height->modified) apply_window_mode(app);
    if (app->swapeffect->modified) {
        gfx_vsync(app->swapeffect->integer != 0);
        app->swapeffect->modified = false;
    }
    app->input.sensitivity = app->sensitivity->number;
    app->consoles.main.count_max = clampi(app->console_length->integer, 1, HUD_CONSOLE_LINES);
    // the original's curve: 50 is a quarter of the way up, and it is quiet enough there
    float v = clampf(app->volume->number / 100.0f, 0.0f, 1.0f);
    audio_volume(&app->audio, v * v * 0.48f);
    app->render_options.wireframe = app->wireframe->integer != 0;
    app->render_options.debug = app->debug->integer != 0;
    Soldier *me = &app->game->world.soldiers[app->me];
    me->look = look_from_cvars(app);
    me->primary_choice = (WeaponId)clampi(app->primary->integer, WEAPON_EAGLE, WEAPON_MINIGUN);
    me->secondary_choice = (WeaponId)(WEAPON_COLT + clampi(app->secondary->integer, 0, WEAPON_LAW - WEAPON_COLT));
    app->net.look = me->look; // what the Hello says of me
    app->net.primary = me->primary_choice;
    app->net.secondary = me->secondary_choice;
}

// The world: with me in it, dressed and armed as the cvars say, when `local`; empty,
// for a server's snapshots to fill, when not.
static bool game_open(App *app, bool local)
{
    app->game = calloc(1, sizeof(Game));
    if (!app->game || !context_load(&app->game->ctx, app->assets->value, app->map->value)) return false;

    Game *g = app->game;
    game_init(g, 1, match_settings_for_map(g->ctx.map));
    g->world.authority = local;
    for (int i = 0; i < MAX_PLAYERS; i++) g->world.soldiers[i].look = look_from_cvars(app);
    if (!local) return true;

    Soldier *me = &g->world.soldiers[app->me];
    Team team = team_game(app) ? TEAM_ALPHA : TEAM_NONE;
    Vec2 at = spawn_point(g->ctx.map, team, &g->world.rng);
    WeaponId primary = (WeaponId)clampi(app->primary->integer, WEAPON_EAGLE, WEAPON_MINIGUN);
    WeaponId secondary = (WeaponId)(WEAPON_COLT + clampi(app->secondary->integer, 0, WEAPON_LAW - WEAPON_COLT));
    soldier_spawn(&g->ctx, me, at, team, primary, secondary);
    return true;
}

static void game_close(App *app)
{
    if (!app->game) return;
    context_destroy(&app->game->ctx);
    free(app->game);
    app->game = NULL;
}

// The window and the GL context on it, as the original's InitGameGraphics.
static bool window_open(App *app)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    app->window = SDL_CreateWindow("bettersoldat", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, app->width->integer,
                                   app->height->integer, SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!app->window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    if (!gfx_init(app->window)) return false;
    apply_window_mode(app);
    apply_cvars(app);
    return true;
}

static void window_close(App *app)
{
    gfx_destroy();
    if (app->window) SDL_DestroyWindow(app->window);
    SDL_Quit();
}

// The tick just run becomes the latest snapshot; the one before it the previous.
// A player's name: mine from the cvar, the others' from the server's roster, or a
// number until it is heard.
static void player_name(const App *app, int i, char *name, size_t size)
{
    const char *heard = app->net.stream.names[i];
    if (i == app->me) snprintf(name, size, "%s", app->player_name->value);
    else if (heard[0]) snprintf(name, size, "%s", heard);
    else snprintf(name, size, "Player %d", i + 1);
}

// The next player to watch, from the one watched: alive, no spectator, and a teammate
// unless I am watching from outside (GetCameraTarget). Nobody: the free camera.
static void camera_next(App *app, bool backwards)
{
    const World *w = &app->game->world;
    const Soldier *me = &w->soldiers[app->me];
    bool outside = me->team == TEAM_SPECTATOR || !team_game(app);
    int from = app->camera_follow < 0 ? app->me : app->camera_follow;
    for (int n = 1; n <= MAX_PLAYERS; n++) {
        int j = ((from + (backwards ? -n : n)) % MAX_PLAYERS + MAX_PLAYERS) % MAX_PLAYERS;
        const Soldier *s = &w->soldiers[j];
        if (j == app->me || !s->active || s->dead || s->team == TEAM_SPECTATOR) continue;
        if (!outside && s->team != me->team) continue;
        app->camera_follow = j;
        app->free_camera = false;
        return;
    }
    app->camera_follow = -1;
    app->free_camera = true;
}

static void snapshot_tick(App *app)
{
    app->previous = app->latest;
    tick_snapshot_capture(&app->latest, &app->game->world);
}

// One tick of the game on this frame's input.
// One tick of the game on this frame's input. Online, everyone else steps on the
// keys they were last heard with (stream_command), and my state goes to the server.
static void tick(App *app)
{
    World *w = &app->game->world;
    bool online = client_net_joined(&app->net);
    Command cmds[MAX_PLAYERS] = {0};
    w->soldiers[app->me].typing = app->hud_data.chat_type != HUD_CHAT_NONE; // the dots over my head, for the others
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Soldier *s = &w->soldiers[i];
        s->remote = online && i != app->me;
        if (s->remote) cmds[i] = stream_command(s, client_stream_quiet(&app->net.stream, i));
    }
    cmds[app->me] = input_command(&app->input, ++app->seq);
    game_tick(app->game, cmds);
    render_tick(&app->render, &app->game->ctx, &app->game->world, &app->game->events);
    audio_tick(&app->audio, app->game, app->me, app->camera.pos);
    if (online) client_net_tick(&app->net, app->game);
    input_clear(&app->input);
    snapshot_tick(app);
    char names[MAX_PLAYERS][HUD_NAME];
    for (int i = 0; i < MAX_PLAYERS; i++) player_name(app, i, names[i], sizeof names[i]);
    feed_tick(&app->feed, app->console, app->game, names, team_game(app), app->me);
    consoles_tick(&app->consoles);

    // The weapons menu opens at my death and with every new life, the first included, and
    // stays through the spawn, to pick with, until I move or fire, or pick; unless I
    // closed it while dead. A game with teams asks the team first: the server keeps me
    // watching until I say.
    const Soldier *me = &w->soldiers[app->me];
    bool spectator = me->active && me->team == TEAM_SPECTATOR;
    bool dead = me->active && me->dead && !spectator;
    bool limbo = app->menus.menus[MENU_LIMBO].active, esc = app->menus.menus[MENU_ESC].active;
    bool new_life = me->active && !spectator && (int)me->life != app->seen_life;
    if (new_life) app->seen_life = me->life;
    if ((new_life || (dead && !app->was_dead)) && !app->limbo_lock && !limbo && !esc) {
        menus_show(&app->menus, MENU_LIMBO, true, app->hud_data.mode, 1);
    }
    const Buttons moving = BUTTON_LEFT | BUTTON_RIGHT | BUTTON_JUMP | BUTTON_CROUCH | BUTTON_PRONE | BUTTON_JET | BUTTON_FIRE | BUTTON_THROW;
    if (limbo && !dead && (cmds[app->me].buttons & moving)) menus_show(&app->menus, MENU_LIMBO, false, app->hud_data.mode, 1);
    app->was_dead = dead;
    if (spectator && team_game(app) && !app->team_asked && !esc) {
        menus_show(&app->menus, MENU_TEAM, true, app->hud_data.mode, 1);
        app->team_asked = true;
    }

    // Watching (LocalInput.pas): dead or a spectator, fire or jump follows the next player
    // and jet the one before, among those alive I may watch; with nobody, the free
    // camera, which the cursor pushes. Alive, the camera is mine again.
    Buttons pressed = (Buttons)(cmds[app->me].buttons & ~app->camera_keys);
    app->camera_keys = cmds[app->me].buttons;
    if (me->active && (me->dead || spectator)) {
        if (!limbo && (pressed & (BUTTON_FIRE | BUTTON_JUMP | BUTTON_JET))) camera_next(app, (pressed & BUTTON_JET) != 0);
        if (app->free_camera) {
            Vec2 off = vec2_sub(app->input.cursor, vec2_scale(app->input.view, 0.5f));
            if (fabsf(off.x) > 10.0f || fabsf(off.y) > 10.0f) app->camera.pos = vec2_add(app->camera.pos, vec2_scale(off, 1.0f / SPECTATORAIMDIST));
        }
    } else {
        app->camera_follow = -1;
        app->free_camera = false;
    }
    for (int i = 0; i < MAX_PLAYERS; i++) // what was said fades
        if (app->hud_data.players[i].chat_delay > 0) app->hud_data.players[i].chat_delay--;
}

// How many ticks this frame owes: a whole tick comes out per tick, and the rest waits
// for the next frame.
static int ticks_owed(App *app, double dt)
{
    app->accumulator += dt;
    if (app->accumulator > MAX_FRAME) app->accumulator = MAX_FRAME;
    int n = (int)(app->accumulator / TICK_SECONDS);
    app->accumulator -= n * TICK_SECONDS;
    return n;
}

// The window's pixels, which the camera draws into.
static Rect window_rect(const App *app)
{
    int w, h;
    SDL_GL_GetDrawableSize(app->window, &w, &h);
    return (Rect){0, 0, (float)w, (float)h};
}

// The view in the cursor's units: the original's GameWidth x GameHeight.
static Vec2 view_size(const App *app)
{
    Rect r = window_rect(app);
    return (Vec2){GAME_HEIGHT * r.width / r.height, GAME_HEIGHT};
}

// The game's cursor in window pixels.
static Vec2 cursor(const App *app)
{
    float scale = app->camera.viewport.height / GAME_HEIGHT;
    return vec2_scale(app->input.cursor, scale);
}

static void interface_open(App *app);
static void hud_data_demo(HudData *d, int page);

// What a menu's choice does: the original's GameMenuAction, on this side of it.
static void apply_menu_action(App *app, MenuAction action)
{
    Soldier *me = &app->game->world.soldiers[app->me];
    switch (action.kind) {
    case MENU_ACTION_QUIT: // exit to the main menu: the line closed, the menus down
        console_execute(app->console, "disconnect");
        menus_hide_all(&app->menus);
        chat_close(app);
        input_release_all(&app->input);
        mainmenu_show(&app->mainmenu, true);
        break;
    case MENU_ACTION_OPEN_TEAM_MENU:
        menus_show(&app->menus, MENU_TEAM, true, app->hud_data.mode, 1);
        break;
    case MENU_ACTION_PICK_PRIMARY: {
        // the choice is the cvar's, which the soldier follows (apply_cvars) and the config keeps
        char number[8];
        snprintf(number, sizeof number, "%d", action.value);
        cvar_set(app->console, "cl_player_wep", number);
        app->hud_data.selected_weapon = (WeaponId)action.value;
        if (!me->dead) me->weapon = weapon_state(&app->game->ctx, (WeaponId)action.value);
        break;
    }
    case MENU_ACTION_PICK_SECONDARY: {
        char number[8];
        snprintf(number, sizeof number, "%d", action.value - WEAPON_COLT);
        cvar_set(app->console, "cl_player_secwep", number);
        app->hud_data.selected_secondary = (WeaponId)action.value;
        if (!me->dead) me->secondary = weapon_state(&app->game->ctx, (WeaponId)action.value);
        break;
    }
    case MENU_ACTION_KICK: // the reason first, typed at the prompt; the vote goes with it
        if (action.value < 0 || action.value >= MAX_PLAYERS || !app->hud_data.players[action.value].active) break;
        app->kick_target = action.value;
        chat_open(app, HUD_CHAT_PUBLIC);
        app->vote_reason_typing = app->hud_data.chat_type != HUD_CHAT_NONE;
        app->hud_data.vote_reason_typing = app->vote_reason_typing;
        break;
    case MENU_ACTION_VOTE_MAP: {
        if (app->map_count == 0) break;
        char text[HUD_TEXT];
        snprintf(text, sizeof text, "/votemap %s", app->maps[clampi(action.value, 0, app->map_count - 1)]);
        say(app, false, text);
        break;
    }
    case MENU_ACTION_PICK_TEAM: { // the server places me on it, or among the watchers
        char text[HUD_TEXT];
        snprintf(text, sizeof text, "/team %d", action.value);
        say(app, false, text);
        break;
    }
    default: break;
    }
}

// An open menu takes the keys and clicks the original gives it: a digit chooses, a left
// click picks. The radio menu takes the digits too. True if it took the event.
static bool menu_event(App *app, const SDL_Event *e)
{
    GameMenus *m = &app->menus;
    bool digit_down = e->type == SDL_KEYDOWN && !e->key.repeat && e->key.keysym.scancode >= SDL_SCANCODE_1 &&
                      e->key.keysym.scancode <= SDL_SCANCODE_0;
    int digit = e->key.keysym.scancode == SDL_SCANCODE_0 ? 0 : e->key.keysym.scancode - SDL_SCANCODE_1 + 1;
    if (app->hud_data.radio_menu && !menus_any_active(m)) {
        if (digit_down) radio_choose(app, digit);
        return digit_down;
    }
    if (!menus_any_active(m)) return false;
    if (digit_down) {
        apply_menu_action(app, menus_number_key(m, digit));
        return true;
    }
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        apply_menu_action(app, menus_click(m, app->hud_data.selected_weapon != WEAPON_NONE));
        return true;
    }
    return false;
}

// This frame's events: the window's, the mouse's motion, then the keys and buttons: an
// open menu's first, and the rest through their binds.
static void poll_events(App *app)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: app->quit = true; break;
        case SDL_MOUSEMOTION:
            if (SDL_GetWindowFlags(app->window) & SDL_WINDOW_INPUT_FOCUS) {
                input_mouse_motion(&app->input, &e.motion);
                menus_mouse_move(&app->menus, app->input.cursor);
            }
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                input_resize(&app->input, view_size(app));
                interface_open(app);
            }
            break;
        default:
            if (app->mainmenu.shown) {
                mainmenu_event(&app->mainmenu, app->console, &e);
                break;
            }
            if (chat_event(app, &e)) break;
            if (!menu_event(app, &e)) input_event(&app->input, app->console, &e);
            break;
        }
    }
    app->chat_just_opened = false;
}

// What the HUD shows that the frame does not carry. Today a local match of one: the
// map's mode by its name, the match's limits and scores, me on the roster, and the
// newest lines of the console's scrollback. The kill console, the chat, the big messages
// and the pings stay empty until what feeds them is ported.
static void hud_data_build(App *app)
{
    HudData *d = &app->hud_data;
    const Game *g = app->game;
    const Soldier *me = &g->world.soldiers[app->me];

    d->mode = team_game(app) ? HUD_MODE_CTF : HUD_MODE_DEATHMATCH;
    d->team_game = d->mode == HUD_MODE_CTF;
    // the flags, for the team box: known once both are placed, at home unless away or held
    d->flags_known = false;
    for (int t = 0; t < HUD_TEAMS; t++) d->flag_in_base[t] = true;
    int flags = 0;
    for (int i = 0; i < MAX_THINGS; i++) {
        const Thing *t = &g->world.things[i];
        if (t->style != THING_ALPHA_FLAG && t->style != THING_BRAVO_FLAG) continue;
        d->flag_in_base[t->style == THING_ALPHA_FLAG ? TEAM_ALPHA : TEAM_BRAVO] = t->in_base && t->holder == 0;
        flags++;
    }
    d->flags_known = flags == 2;
    // the vote on, as the server last said
    const MsgVote *v = &app->net.vote;
    d->vote = v->kind == VOTE_KICK ? HUD_VOTE_KICK : v->kind == VOTE_MAP ? HUD_VOTE_MAP : HUD_VOTE_NONE;
    snprintf(d->vote_target, sizeof d->vote_target, "%s", v->target);
    snprintf(d->vote_starter, sizeof d->vote_starter, "%s", v->starter);
    snprintf(d->vote_reason, sizeof d->vote_reason, "%s", v->reason);
    snprintf(d->hostname, sizeof(d->hostname), "%s", client_net_joined(&app->net) ? app->net.hostname : "bettersoldat");
    // the map window's offer, kept within the list
    if (app->map_count > 0) {
        app->menus.map_index = clampi(app->menus.map_index, 0, app->map_count - 1);
        snprintf(d->map_offered, sizeof d->map_offered, "%s", app->maps[app->menus.map_index]);
    } else {
        snprintf(d->map_offered, sizeof d->map_offered, "%s", app->map->value);
    }
    d->kill_limit = g->match.settings.score_limit;
    d->time_left_min = g->match.time_left / TICK_RATE / 60;
    d->time_left_sec = g->match.time_left / TICK_RATE % 60;
    for (int t = 0; t < HUD_TEAMS && t < TEAM_COUNT; t++) d->team_kills[t] = g->match.scores[t];
    d->paused = g->match.state == MATCH_PAUSED;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Soldier *s = &g->world.soldiers[i];
        HudPlayer *p = &d->players[i];
        p->active = s->active;
        if (!s->active) continue;
        player_name(app, i, p->name, sizeof p->name);
        p->team = s->team;
        p->dead = s->dead;
        p->holding_flag = s->held && thing_is_flag(g->world.things[s->held - 1].style);
        p->shirt = s->look.shirt;
        p->kills = s->kills;
        p->deaths = s->deaths;
        p->flags = s->flags;
        p->ping = s->ping;
        p->typing = i != app->me && s->typing;
        p->spectator = s->team == TEAM_SPECTATOR;
    }
    d->ping = me->ping;
    d->bonus = me->bonus == BONUS_PREDATOR ? HUD_BONUS_PREDATOR : me->bonus == BONUS_BERSERKER ? HUD_BONUS_BERSERKER
               : me->bonus == BONUS_FLAME_GOD ? HUD_BONUS_FLAMEGOD : HUD_BONUS_NONE;
    d->bonus_time = me->bonus_time;
    // the player under the cursor (UpdateFrame.pas): named, with its health if a teammate
    d->cursor_text[0] = '\0';
    d->cursor_friendly = false;
    for (int j = 0; j < MAX_PLAYERS && me->active; j++) {
        const Soldier *s = &g->world.soldiers[j];
        bool teammate = d->team_game && s->team == me->team;
        if (j == app->me || !s->active || s->team == TEAM_SPECTATOR || s->bonus == BONUS_PREDATOR) continue;
        if (!(s->stance == STANCE_STAND || teammate || me->dead || s->dead)) continue;
        if (vec2_length(vec2_sub(app->input.aim, s->pos)) >= CURSORSPRITE_DISTANCE) continue;
        char name[HUD_NAME];
        player_name(app, j, name, sizeof name);
        if (teammate) {
            snprintf(d->cursor_text, sizeof d->cursor_text, "%s %d%%", name, (int)roundf(s->health / DEFAULT_HEALTH * 100.0f));
            d->cursor_friendly = true;
        } else {
            snprintf(d->cursor_text, sizeof d->cursor_text, "%s", name);
        }
        break;
    }
    d->me = app->me;
    d->camera_follow = app->camera_follow;
    d->free_camera = app->free_camera;
    d->selected_weapon = me->weapon.id;
    d->selected_secondary = me->secondary.id;
    d->respawn_counter = me->respawn_counter;
    d->cease_fire_counter = me->cease_fire_counter;
    d->fps = app->fps;
    d->time = app->time;
    d->tick = (int)g->world.tick;
    d->minimap = app->minimap->integer != 0;
    d->show_info = app->info->integer != 0;
    d->player_names = app->player_names->integer != 0;

    // the radio menu's columns: the calls, and the places of the call chosen
    int call = d->radio_state ? d->radio_state - 1 : 0;
    for (int i = 0; i < RADIO_CALLS; i++) {
        snprintf(d->radio_first[i], sizeof d->radio_first[i], "%s", app->radio_first[i]->value);
        snprintf(d->radio_second[i], sizeof d->radio_second[i], "%s", app->radio_second[call][i]->value);
    }

    // the consoles: the main one, or the big one while a line is typed
    consoles_pull(&app->consoles, app->console);
    consoles_fill(&app->consoles, d, d->chat_type != HUD_CHAT_NONE);
    feed_fill(&app->feed, d, &g->ctx.weapons);
    if (app->hud_demo->integer) hud_data_demo(d, app->hud_demo->integer);
}

// Sample data in every part of the HUD, for looking at it before the game fills it.
static void hud_data_demo(HudData *d, int page)
{
    const char *names[] = {"Player 1", "Crow", "Mabuse", "Ceres", "Spec"};
    const Team teams[] = {TEAM_ALPHA, TEAM_ALPHA, TEAM_BRAVO, TEAM_BRAVO, TEAM_SPECTATOR};
    const Rgba shirts[] = {{199, 56, 51, 255}, {255, 200, 60, 255}, {64, 107, 204, 255}, {120, 220, 255, 255}, {0}};
    for (int i = 0; i < 5; i++) {
        HudPlayer *p = &d->players[i];
        p->active = true;
        snprintf(p->name, sizeof(p->name), "%s", names[i]);
        p->team = teams[i];
        p->spectator = teams[i] == TEAM_SPECTATOR;
        p->kills = 12 - 3 * i;
        p->deaths = 2 + i;
        p->flags = i == 1 ? 2 : 0;
        p->ping = 40 + 37 * i;
        p->shirt = shirts[i];
    }
    d->ping = 43;
    d->team_kills[TEAM_ALPHA] = 3;
    d->team_kills[TEAM_BRAVO] = 1;
    d->flags_known = true;
    d->flag_in_base[TEAM_ALPHA] = true;
    d->flag_in_base[TEAM_BRAVO] = false;
    d->time_left_min = 12;
    d->time_left_sec = 34;
    snprintf(d->info, sizeof(d->info), "a sample match");
    d->frags_menu = true;
    d->show_info = true;

    const char *console[] = {"Crow joined the game.", "Mabuse joined the game.", "Welcome to bettersoldat"};
    const Rgba console_colors[] = {{0xC3, 0xC3, 0xC3, 0xF1}, {0xC3, 0xC3, 0xC3, 0xF1}, {0x71, 0xF9, 0x81, 0xEE}};
    d->console_count = 3;
    for (int i = 0; i < 3; i++) {
        snprintf(d->console[i].text, sizeof(d->console[i].text), "%s", console[i]);
        d->console[i].color = console_colors[i];
    }
    d->kill_count = 2;
    snprintf(d->kills[0].text, sizeof(d->kills[0].text), "Crow");
    d->kills[0].color = (Rgba){0xEA, 0x35, 0x30, 0xFF};
    d->kills[0].weapon = WEAPON_AK74;
    d->kills[0].has_icon = true;
    snprintf(d->kills[1].text, sizeof(d->kills[1].text), "Mabuse");
    d->kills[1].color = (Rgba){0x31, 0x31, 0xDF, 0xFF};

    d->big_count = 1;
    snprintf(d->big[0].text, sizeof(d->big[0].text), "Alpha Flag Captured!");
    d->big[0].color = (Rgba){0xD3, 0xCA, 0x34, 0xFF};
    d->big[0].scale = 0.0625f;
    d->big[0].delay = 200;
    d->big[0].x = 80;
    d->big[0].y = 240;

    d->chat_type = HUD_CHAT_PUBLIC;
    snprintf(d->chat_text, sizeof(d->chat_text), "gg");
    d->chat_cursor = 2;
    d->players[0].chat_delay = 20;
    snprintf(d->players[0].chat, sizeof(d->players[0].chat), "hello");

    if (page >= 2) { // the stats, a vote, the radio, a shot, the rest
        d->frags_menu = false;
        d->stats_menu = true;
        d->weapon_stat_count = 2;
        d->weapon_stats[0] = (HudWeaponStat){WEAPON_AK74, "Ak-74", 120, 40, 5, 1};
        d->weapon_stats[1] = (HudWeaponStat){WEAPON_COLT, "USSOCOM", 30, 12, 2, 0};
        d->vote = HUD_VOTE_KICK;
        snprintf(d->vote_target, sizeof(d->vote_target), "Mabuse");
        snprintf(d->vote_starter, sizeof(d->vote_starter), "Crow");
        snprintf(d->vote_reason, sizeof(d->vote_reason), " afk");
        d->radio_menu = true;
        d->radio_state = 1;
        const char *first[] = {"Enemy flagger", "Friendly flagger", "Enemy spotted"};
        const char *second[] = {"up!", "middle!", "down!"};
        for (int i = 0; i < 3; i++) {
            snprintf(d->radio_first[i], sizeof(d->radio_first[i]), "%s", first[i]);
            snprintf(d->radio_second[i], sizeof(d->radio_second[i]), "%s", second[i]);
        }
        d->recording = true;
        d->shot_distance_shown = true;
        d->shot_distance = 42.5f;
        d->shot_airtime = 1.2f;
        d->shot_ricochets = 1;
        d->minimap = true;
        d->chat_type = HUD_CHAT_NONE;
    }
    if (page >= 3) { // the bonus, watching someone
        d->bonus = HUD_BONUS_BERSERKER;
        d->camera_follow = 1;
        d->stats_menu = false;
        d->minimap = true;
    }
}

// The frame rate, counted over each second: the original's FrameTiming.Fps.
static void count_frame(App *app, double dt)
{
    app->frames++;
    app->frame_timer += dt;
    if (app->frame_timer < 1.0) return;
    app->fps = app->frames;
    app->frames = 0;
    app->frame_timer = 0;
}

// The fonts, the minimap and the menus, sized to the window; again whenever it changes.
static void interface_open(App *app)
{
    Rect r = window_rect(app);
    if (!fonts_load(app->assets->value, r.height)) fprintf(stderr, "no fonts: the HUD draws without text\n");
    map_view_build_minimap(&app->render.map_view, r.height);
    menus_init(&app->menus, GAME_HEIGHT * r.width / r.height, &app->game->ctx.weapons);
}

// A server's map: the world and its picture made anew for it, nobody in it until the
// snapshots say. False if the map can't be loaded, which leaves no world at all.
static bool world_reload(App *app, const char *map)
{
    cvar_set(app->console, "map", map);
    render_destroy(&app->render);
    game_close(app);
    if (!game_open(app, false)) return false;
    render_init(&app->render, app->assets->value, &app->game->ctx);
    interface_open(app);
    app->previous = app->latest = (TickSnapshot){0};
    app->limbo_lock = false;
    app->was_dead = false;
    app->seen_life = -1;
    app->team_asked = false;
    app->camera_follow = -1;
    app->free_camera = false;
    return true;
}

int main(int argc, char *argv[])
{
    App app = {0};

    if (!client_net_init(&app.net)) fprintf(stderr, "ENet wouldn't start: no connecting\n");
    if (!console_open(&app, argc, argv)) return 1;
    consoles_init(&app.consoles, app.console_length->integer);
    app.seen_life = -1;
    app.camera_follow = -1;
    {
        char dir[512];
        snprintf(dir, sizeof dir, "%s/maps", app.assets->value);
        app.map_count = list_files(dir, ".pms", app.maps, (int)(sizeof app.maps / sizeof app.maps[0]));
    }
    if (!game_open(&app, true)) {
        fprintf(stderr, "could not load map '%s' from '%s'\nusage: client +assets <dir> +map <name>\n",
                app.map->value, app.assets->value);
        game_close(&app);
        console_destroy(app.console);
        return 1;
    }
    if (!window_open(&app)) {
        window_close(&app);
        game_close(&app);
        console_destroy(app.console);
        return 1;
    }

    render_init(&app.render, app.assets->value, &app.game->ctx);
    audio_init(&app.audio, app.assets->value);
    scale_data_load(&app.scales, app.assets->value);
    interface_load(&app.hud, app.assets->value, &app.scales);
    interface_open(&app);
    if (app.hud_demo->integer == 2) menus_show(&app.menus, MENU_LIMBO, true, HUD_MODE_CTF, 1);
    if (app.hud_demo->integer == 3) {
        menus_show(&app.menus, MENU_ESC, true, HUD_MODE_CTF, 1);
        app.menus.noob_show = true;
    }

    snapshot_tick(&app);
    snapshot_tick(&app); // both snapshots start as the world before the first tick
    app.camera = (GameCamera){.pos = app.game->world.soldiers[app.me].pos, .viewport = window_rect(&app)};
    input_start(&app.input, view_size(&app));
    mainmenu_show(&app.mainmenu, app.net.state == CLIENT_NET_OFF); // unless the command line is already connecting
    app.net_state_seen = app.net.state;

    Uint64 last = SDL_GetPerformanceCounter();
    double since_frame = 0; // the time the frame being drawn covers
    int frames_drawn = 0;
    while (!app.quit) {
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / (double)SDL_GetPerformanceFrequency();
        last = now;
        app.time += dt;

        poll_events(&app);
        client_net_poll(&app.net, app.console, app.game);
        if (client_net_take_map(&app.net)) {
            // a round on the server's map: the world made anew for its snapshots, my slot its
            if (!world_reload(&app, app.net.map)) {
                fprintf(stderr, "could not load the server's map '%s'\n", app.net.map);
                app.quit = true;
            }
            app.me = app.net.slot;
        }
        MsgChat heard;
        while (client_net_take_chat(&app.net, &heard)) chat_heard(&app, heard.slot, heard.team, heard.text);
        // the menu goes as a server takes us, and comes back when the line is lost
        if (app.net.state != app.net_state_seen) {
            if (app.net.state == CLIENT_NET_JOINED) mainmenu_show(&app.mainmenu, false);
            else if (app.net.state == CLIENT_NET_OFF && app.net_state_seen == CLIENT_NET_JOINED) mainmenu_show(&app.mainmenu, true);
            app.net_state_seen = app.net.state;
        }
        apply_cvars(&app);
        app.camera.viewport = window_rect(&app);
        input_sample(&app.input, screen_to_world(&app.camera, cursor(&app)));

        int ticks = ticks_owed(&app, dt);
        for (int i = 0; i < ticks; i++) tick(&app);
        client_net_flush(&app.net); // what the ticks said goes out now, not a tick late

        // the world ticks every pass; a frame is drawn only once the last is old enough
        since_frame += dt;
        if (since_frame >= MIN_FRAME_SECONDS) {
            float alpha = (float)(app.accumulator / TICK_SECONDS); // how far into the next tick this frame is
            bool online = client_net_joined(&app.net);
            if (online) client_stream_smooth(&app.net.stream, (float)since_frame, app.smooth->number / 1000.0f);
            build_render_state(&app.frame, &app.game->ctx, &app.previous, &app.latest, alpha, app.me,
                               team_game(&app), online ? app.net.stream.blend : NULL);
            Vec2 target = app.frame.focus;
            if (app.camera_follow >= 0 && app.frame.soldiers[app.camera_follow].active) target = app.frame.soldiers[app.camera_follow].pos;
            const RenderSoldier *watched = &app.frame.soldiers[app.camera_follow >= 0 ? app.camera_follow : app.me];
            if (!app.free_camera) camera_follow(&app.camera, target, cursor(&app), watched->aim_dist, since_frame);

            gfx_viewport(0, 0, (int)app.camera.viewport.width, (int)app.camera.viewport.height);
            if (!app.mainmenu.shown) {
                render_draw(&app.render, &app.frame, &app.camera, app.render_options, app.time);
                hud_data_build(&app);
                interface_draw(&app.hud, &app.hud_data, &app.menus, &app.frame, &app.game->ctx, &app.render.map_view,
                               &app.camera, app.input.cursor, app.camera.viewport);
            } else { // the menu on its own background: the game is not watched from here
                gfx_clear((Rgba){0, 0, 0, 255});
                Rect r = app.camera.viewport;
                mainmenu_draw(&app.mainmenu, app.console, &app.hud, &app.render.gostek, app.game->ctx.anims, &app.game->ctx.weapons,
                              app.input.cursor, GAME_HEIGHT * r.width / r.height, GAME_HEIGHT / r.height, app.time,
                              console_log_line(app.console, 0), client_net_joined(&app.net));
                char command[256];
                if (mainmenu_take_command(&app.mainmenu, command, sizeof command)) console_execute(app.console, command);
            }
            if (app.screenshot[0] && ++frames_drawn == SCREENSHOT_FRAME) {
                Rect r = app.camera.viewport;
                if (!gfx_save_screen(app.screenshot, (int)r.width, (int)r.height)) {
                    fprintf(stderr, "could not write %s\n", app.screenshot);
                }
                app.quit = true;
            }
            gfx_present(app.window);
            count_frame(&app, since_frame);
            since_frame = 0;
        }
        SDL_Delay(SLEEP_AFTER_FRAME_MS);
    }

    client_net_shutdown(&app.net);
    audio_shutdown(&app.audio);
    fonts_unload();
    interface_unload(&app.hud);
    render_destroy(&app.render);
    window_close(&app);
    game_close(&app);
    console_close(&app);
    return 0;
}
