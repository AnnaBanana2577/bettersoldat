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
// or `+connect localhost` to join a server (net/client_net.c), or `+host` to host one
// here on the sv_* and bots_* cvars and join it, as the menu's Local Play does (after
// the cvars it reads: `+bots_random_noteam 3 +host`). The world stays the local sandbox
// until the server's state comes down the line.
//
// The binds below are the fallback for a missing config.cfg; the file's are the ones
// that count. V is the radio menu (+radio), opened and shut by a press: a call by its
// number, then a place by its, said to the team from the radio_* cvars. Alt with a
// letter is a taunt in the config (say, say_team). The view's: Escape the menu, Tab the
// weapons, M the teams, F1 the scoreboard, F2 the weapon stats, F3 the minimap
// (ui_minimap), F5 the FPS line (ui_info), F7 the names (ui_playernames). F9 is a window or
// fullscreen (togglewindow), Ctrl+F9 the wireframe (r_wireframe), F10 the debug overlay
// (r_debug), F4 vsync (r_swapeffect, off as the original's default). There is no zoom:
// everyone sees the same 480 units of height.
// A demo playing (net/demo.h): F6 pauses it, F8 runs it fast, as the original's F10 and F8;
// the left and right arrows take it ten seconds back or on.

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
#include "net/demo.h"
#include "http.h" // the launcher's HTTPS, for the browser's list
#include "render/interface.h"
#include "render/render.h"
#include "render/textures.h"
#include "render/scale_data.h"
#include <ctype.h>
#include <math.h>
#include <time.h>

#include "audio/audio.h"
#include "host.h" // the server's, built in for Local Play
#include "rounds.h"
#include "ui/consoles.h"
#include "ui/feed.h"
#include "ui/mainmenu.h"
#include "ui/menus.h"

#ifndef SOLDATRELOADED_VERSION
#define SOLDATRELOADED_VERSION "dev" // xmake.lua sets it from set_version
#endif
#define MAX_FRAME 0.25 // a stall never turns into a burst of ticks
#define CONFIG "config.cfg"
#define SCREENSHOT_FRAME 60
#define RADIO_CALLS 3  // the radio menu's first choices, and each one's second choices
#define RADIO_COOLDOWN (3 * TICK_RATE) // a radio call heard, the next stays quiet this long (RadioCooldown)
#define CURSORSPRITE_DISTANCE 15.0f // the original's: how near the cursor names a player
#define SPECTATORAIMDIST 30.0f      // the original's: the free camera's speed, by the cursor's offset

// The original's frame pacing, its defaults: vsync off (r_swapeffect 0), frames no closer
// than 1/r_maxfps s while r_fpslimit is on (500 a second), and a millisecond's sleep
// after each so the loop never spins flat out (r_sleeptime).
#define MAXFPS_MIN 10
#define MAXFPS_MAX 1000
#define SLEEP_AFTER_FRAME_MS 1

// The view's keys, bound to the cvars and commands below: the config's defaults.
static const char *VIEW_BINDS =
    "bind escape escmenu; bind tab weaponsmenu; bind m teammenu; bind f1 fragsmenu; bind f2 statsmenu;"
    "bind f3 \"toggle ui_minimap\"; bind f4 \"toggle r_swapeffect\"; bind f5 \"toggle ui_info\";"
    "bind f7 \"toggle ui_playernames\"; bind f9 togglewindow; bind ctrl+f9 \"toggle r_wireframe\"; bind f10 \"toggle r_debug\";"
    "bind f6 demo_pause; bind f8 demo_fast; bind leftarrow \"demo_tick_r -600\"; bind rightarrow \"demo_tick_r 600\";"
    "bind v +radio; bind t chat; bind y teamchat; bind slash cmd; bind f12 \"say /yes\"; bind f11 \"say /no\"";

typedef struct App {
    Console *console; // large; on the heap
    Cvar *assets;     // the opensoldat base assets: maps/, anims/, objects/, textures/...
    Cvar *map;
    Cvar *width, *height; // the window
    Cvar *swapeffect;     // vsync
    Cvar *fpslimit, *maxfps; // r_fpslimit: frames no closer than 1/r_maxfps s; off, as fast as they come
    Cvar *fullscreen;     // 0 windowed, 1 fullscreen, 2 borderless
    int fullscreen_left;  // the fullscreen mode togglewindow left, to go back to
    Cvar *server;         // the address the main menu joins
    Cvar *password;       // and the password it says there (cl_password)
    Cvar *lobby;          // the lobby the server browser asks (cl_lobby)
    Cvar *sensitivity;
    Cvar *wireframe, *debug;
    Cvar *scenery;        // r_scenery: the props behind the map drawn
    Cvar *trails;         // r_trails: the streaks behind the rounds
    Cvar *weather;        // r_weathereffects: the map's rain, sand or snow, and its wind
    Cvar *track_shot;     // cl_trackshot: the camera follows a scoped Barrett shot
    Cvar *forcebg, *forcebg_color1, *forcebg_color2; // the sky in colours of my own instead of the map's (r_forcebg)
    Cvar *minimap, *info, *player_names, *console_length;
    Cvar *player_name;
    Cvar *grenade_color;
    Cvar *cursor_color, *crosshair_color, *cursor_size, *crosshair_size;
    Cvar *shirt, *pants, *skin, *hair, *jet;      // the look's colours, "RRGGBB"
    Cvar *hair_style, *head_style, *chain_style;  // and its styles, by number
    Cvar *style;                                  // the gostek: 0 male, 1 female, 2 waifu, 3 rat
    Cvar *gear;                                   // the gear at the next spawn: jets, or a rope
    Cvar *primary, *secondary;                    // the loadout at the next spawn
    Cvar *smooth;                                 // milliseconds a correction of another player is smoothed over
    Cvar *interp;                                 // ticks the view keeps behind the newest snapshot, at least (cl_interp)
    Cvar *netstats;                               // a line a second on the console of how the line is doing (cl_netstats)
    Cvar *rope_debug;                             // cl_rope_debug: each soldier's rope each half second, and changes at once
    Cvar *volume;                                 // snd_volume, 0 to 100
    Cvar *effects_battle, *effects_explosions;    // snd_effects_battle, snd_effects_explosions: the original's, off as it has them
    Cvar *radio_first[RADIO_CALLS];               // the radio menu's calls
    Cvar *radio_second[RADIO_CALLS][RADIO_CALLS]; // and each call's places
    Cvar *hud_demo;       // the HUD full of sample data, to see every part of it: page 1, 2 or 3
    Cvar *menu_page;      // the main menu's page at start: for a screenshot of it
    char screenshot[512]; // a PNG of the 60th frame, then quit
    // Local Play's: the server's own cvars, which the main menu edits and the config
    // keeps, so a dedicated server started beside this config plays the same game.
    Cvar *sv_port, *sv_maps, *sv_hostname, *sv_gamemode, *sv_timelimit, *sv_killlimit;
    Cvar *bots_noteam, *bots_alpha, *bots_bravo, *bots_difficulty, *bots_chat, *votepercent;
    Cvar *rope; // sv_rope: whether the rope is allowed in a game hosted here (Local Play)
    Host *host; // the game hosted here (the `host` command), joined over the loopback; NULL for none

    // Demos (net/demo.h): the game joined recorded, or a demo played in its place.
    Cvar *demo_autorecord; // a demo of every round joined
    Cvar *demo_speed;      // a demo's playback, times its own speed
    DemoRecorder recorder;
    bool record_asked;     // `record` said: begun once the frame's packets are in
    char record_name[128]; // and its name, empty for the date and the map
    bool round_recorded;   // a demo of this round was begun: demo_autorecord begins no other
    DemoPlayer player;
    bool playing;          // a demo plays: the line is the demo's, my soldier its recorder's
    char play_name[256];   // `playdemo` said: opened in the loop, where the world is
    bool demo_paused;
    bool demo_ticked;      // `demo_tick` holds the tick to run next
    DemoTick demo_tick;    // the demo's tick on hand: my command and soldier
    bool seeking;          // the demo runs on, unseen and unheard, to `seek_to` (demo_seek)
    uint32_t seek_to;
    DemoListing demos[128]; // demos/, for the main menu's Demos page: read as the page opens
    int demo_count;
    bool demos_shown;       // the page was up last frame

    Game *game; // large; on the heap
    int me;     // my soldier: 0 in the local sandbox, the slot the server gave me online
    SDL_Window *window;
    Input input;
    ClientNet net; // the line to a server, once `connect` opens one
    Browser browser; // the main menu's server list (the `browse` command)
    uint32_t seq; // my commands, numbered
    // cl_rope_debug's memory: each soldier's rope as of the last line, to log changes
    struct {
        uint8_t rope, wraps;
        Vec2 pos;
    } rope_seen[MAX_PLAYERS];
    uint32_t rope_dropped, rope_held_back; // the stream's counters, as of the last line
    bool chat_just_opened; // the key that opened the prompt is not its first letter
    int chat_completing;   // Tab: the player last completed to, index + 1; 0 when not completing
    int chat_complete_from;
    char chat_complete_base[HUD_TEXT];
    char chat_last[HUD_TEXT]; // the last line sent, for "//" to bring back
    int radio_cooldown;       // ticks before another radio call is heard (RadioCooldown)
    HudChatType chat_last_type;
    Consoles consoles;        // the HUD's two consoles, fed from the game console's scrollback
    int console_scroll;       // how far back the big console is paged while a line is typed
    bool vote_reason_typing;  // the prompt takes a kick vote's reason (the kick window's OK)
    int kick_target;          // the player it is about
    uint32_t vote_seen;       // the vote the box last came up for (ClientNet.vote_seq)
    bool vote_hidden;         // the box put away: I answered, or it is my own kick vote
    int map_query_index;      // the map window's question last asked of the server, -1 for none
    bool map_window_open;     // as of the last frame
    char maps[128][64];       // the maps under assets, for the map window
    int map_count;
    bool was_dead;         // my soldier as of the last tick, for the weapons menu at death
    bool was_watching;     // dead or a spectator as of the last tick, for the camera's first target
    int seen_life;         // my latest life, -1 before the first, which opens the weapons menu
    bool team_asked;       // the team menu shown for this round's join
    // Watching: the player the camera follows (-1 for me), or the free camera, moved by
    // the cursor's offset from the middle, as the original's spectator has it.
    int camera_follow;
    bool free_camera;
    bool tracking;          // the camera rides my scoped Barrett shot (track_shot)
    uint32_t tracking_shot; // which shot: its number
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
    // the stream's counters as of the last second's report (cl_netstats)
    struct {
        uint32_t late, misses, held, skipped, resyncs, applies;
        float correction;
    } net_seen;
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

// togglewindow: a window, or back to the fullscreen it came from (F9); from a window
// first, fullscreen.
static void cmd_togglewindow(Console *con, int argc, char **argv, void *user)
{
    (void)argc, (void)argv;
    App *app = user;
    int mode = app->fullscreen->integer;
    if (mode != 0) app->fullscreen_left = mode;
    cvar_set(con, "r_fullscreen", mode != 0 ? "0" : app->fullscreen_left == 2 ? "2" : "1");
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
// ("*SERVER*: text") or a line of the game's (who came and went, a vote) in the
// colour the original gives its `kind`.
static Rgba chat_kind_color(ChatKind kind)
{
    switch (kind) {
    case CHAT_ALPHA: return HUD_COLOR_ALPHAJ;
    case CHAT_BRAVO: return HUD_COLOR_BRAVOJ;
    case CHAT_SPECTATOR: return HUD_COLOR_DELTAJ;
    case CHAT_CLIENT: return HUD_COLOR_CLIENT;
    case CHAT_GAME: return HUD_COLOR_GAME;
    case CHAT_VOTE: return HUD_COLOR_VOTE;
    case CHAT_SCRIPT: return HUD_COLOR_SCRIPT;
    default: return HUD_COLOR_ENTER;
    }
}

// `own` is a script line's own colour, when it chose one (alpha above 0).
static void chat_heard(App *app, int slot, bool team, ChatKind kind, Rgba own, const char *text)
{
    Console *con = app->console;
    if (slot == MAX_PLAYERS) {
        if (kind == CHAT_SERVER) console_print_color(con, HUD_COLOR_SERVER, "*SERVER*: %s\n", text);
        else if (kind == CHAT_SCRIPT && own.a > 0) console_print_color(con, own, "%s\n", text);
        else console_print_color(con, chat_kind_color(kind), "%s\n", text);
        return;
    }
    char name[HUD_NAME];
    player_name(app, slot, name, sizeof name);
    bool spectator = app->game->world.soldiers[slot].team == TEAM_SPECTATOR;
    // A radio call (NetworkClientMessages.pas): team chat that begins '*', the call and
    // the place, said as (RADIO) without them, its words over the head as any chat's;
    // its call is heard over the radio (PlayRadioSound), a few seconds apart at most.
    bool radio = team && text[0] == '*' && text[1] >= '1' && text[1] <= '3' && text[2] >= '1' && text[2] <= '3';
    if (radio) {
        static const char *const CALLS[] = {"efc", "ffc", "es"}, *const PLACES[] = {"up", "mid", "down"};
        if (app->radio_cooldown <= 0) {
            char sound[32];
            snprintf(sound, sizeof sound, "radio/%s%s.wav", CALLS[text[1] - '1'], PLACES[text[2] - '1']);
            audio_flat(&app->audio, sound);
            app->radio_cooldown = RADIO_COOLDOWN;
        }
        text += 3;
        team = false; // the bubble over the head is not marked the team's
    }
    Rgba color = team || radio ? HUD_COLOR_TEAMCHAT : spectator ? HUD_COLOR_SPECTATOR_CHAT : HUD_COLOR_CHAT;
    const char *prefix = radio ? "(RADIO) " : team ? "(TEAM) " : "";
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
// The original's word to me on my own yes to the vote on (ControlGame.pas), in the
// vote's colour. Starting a vote says nothing: its box says it (the original's "You have
// voted to kick ... from the game" is in a branch of StartVote that never runs).
static void vote_said(App *app, const char *text)
{
    const MsgVote *v = &app->net.vote;
    if (strcmp(text, "/yes") == 0) {
        if (v->kind == VOTE_MAP) console_print_color(app->console, HUD_COLOR_VOTE, "You have voted on %s\n", v->target);
        else if (v->kind == VOTE_KICK) console_print_color(app->console, HUD_COLOR_VOTE, "You have voted to kick %s\n", v->target);
    }
}

static void say(App *app, bool team, const char *text)
{
    if (!text[0]) return;
    // F12 and F11 (ControlGame.pas): an answer to the vote's box, while it is up. A yes
    // goes to the server; a no is mine alone. Either puts the box away.
    bool box_up = app->hud_data.vote != HUD_VOTE_NONE;
    if (strcmp(text, "/yes") == 0 || strcmp(text, "/no") == 0) {
        if (!box_up || !client_net_joined(&app->net)) return;
        app->vote_hidden = true;
        if (strcmp(text, "/no") == 0) return;
    }
    if (text[0] == '/' && client_net_joined(&app->net)) vote_said(app, text);
    if (client_net_say(&app->net, text, team)) return;
    chat_heard(app, app->me, team, CHAT_SERVER, (Rgba){0}, text);
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
    app->console_scroll = 0;
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
    bool reason = app->vote_reason_typing; // read before the close clears it
    chat_close(app);
    if (reason) { // the kick window's reason (ControlGame.pas): the vote, with it, if enough was typed
        // the reason as typed, its leading space and all, as the original's box shows it
        // ("Reason:" then " afk"); the prompt held it to REASON_CHARS - 1
        if (strlen(line) > 3) {
            char text[HUD_TEXT];
            snprintf(text, sizeof text, "/votekick %d %.*s", app->kick_target, NET_REASON_SIZE - 1, line);
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
    // a kick's reason holds REASON_CHARS - 1, its leading space among them (ControlGame.pas)
    size_t max = app->vote_reason_typing ? NET_REASON_SIZE - 1 : MAXCHATTEXT;
    if (len >= max) return;
    if (len + add > max) add = max - len;
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
    if (e->type == SDL_MOUSEWHEEL) { // the big console pages with the wheel
        app->console_scroll = clampi(app->console_scroll + (e->wheel.y > 0 ? 3 : -3), 0, consoles_scroll_max(&app->consoles));
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
    case SDL_SCANCODE_PAGEUP: app->console_scroll = clampi(app->console_scroll + 3, 0, consoles_scroll_max(&app->consoles)); break;
    case SDL_SCANCODE_PAGEDOWN: app->console_scroll = clampi(app->console_scroll - 3, 0, consoles_scroll_max(&app->consoles)); break;
    default: break;
    }
    d->chat_changed_at = app->time;
    return true;
}

// --- demos (net/demo.h) --------------------------------------------------------------

static void host_stop(App *app);

static void demo_stop_recording(App *app)
{
    if (!demo_recording(&app->recorder)) return;
    uint32_t seconds = app->recorder.ticks / TICK_RATE;
    demo_record_close(&app->recorder);
    console_print_color(app->console, HUD_COLOR_CLIENT, "Demo saved: %s (%u:%02u)\n", app->recorder.path, seconds / 60, seconds % 60);
}

// The demo playing, stopped: the line it was is closed, and the main menu comes back
// as for any line lost.
static void demo_stop_playback(App *app)
{
    if (!app->playing) return;
    app->playing = false;
    app->demo_paused = false;
    app->demo_ticked = false;
    app->seeking = false;
    demo_play_close(&app->player);
    client_net_disconnect(&app->net, app->console);
    console_print_color(app->console, HUD_COLOR_CLIENT, "Demo ended\n");
}

// Every message the line brings, into the recording. A Map ends it, as the original's
// map change does: the round it was of is over, and demo_autorecord begins the next.
static void demo_tap(void *user, const uint8_t *data, size_t size, MsgKind kind)
{
    App *app = user;
    if (!demo_recording(&app->recorder)) return;
    if (kind == MSG_MAP) demo_stop_recording(app);
    else demo_record_packet(&app->recorder, data, size);
}

// A demo of the round joined, from now: into demos/ as `name`, or as the date and the
// map with none.
static void demo_start_recording(App *app, const char *name)
{
    char stem[192], path[256];
    if (name && name[0]) snprintf(stem, sizeof stem, "%s", name);
    else demo_default_name(stem, sizeof stem, app->net.map);
    demo_path(path, sizeof path, stem);
    DemoHeader h = {.date = (uint32_t)time(NULL), .slot = (uint8_t)app->net.slot};
    snprintf(h.name, sizeof h.name, "%s", app->player_name->value);
    snprintf(h.map, sizeof h.map, "%s", app->net.map);
    if (!demo_record_open(&app->recorder, path, &h)) {
        console_print_color(app->console, HUD_COLOR_WARNING, "could not write the demo %s\n", path);
        return;
    }
    demo_record_join(&app->recorder, &app->net);
    console_print_color(app->console, HUD_COLOR_CLIENT, "Recording demo: %s\n", path);
}

// The demo `playdemo` named, in place of whatever game was on: once it is found good,
// the line and the game hosted here closed, the demo's own line opened.
static void demo_start_playback(App *app)
{
    char path[256], error[320];
    demo_path(path, sizeof path, app->play_name);
    app->play_name[0] = '\0';
    demo_stop_playback(app);
    if (!demo_play_open(&app->player, path, error, sizeof error)) {
        console_print_color(app->console, HUD_COLOR_WARNING, "%s\n", error);
        return;
    }
    const DemoHeader *h = &app->player.header;
    char file[NET_MAP_SIZE + 8], map[512];
    snprintf(file, sizeof file, "%s.pms", h->map);
    path_join(map, sizeof map, app->assets->value, "maps", file);
    if (!file_exists(map)) { // its world couldn't be made: it isn't played
        console_print_color(app->console, HUD_COLOR_WARNING, "the demo's map %s is not here\n", h->map);
        demo_play_close(&app->player);
        return;
    }
    demo_stop_recording(app);
    client_net_disconnect(&app->net, app->console);
    host_stop(app);
    client_net_play(&app->net, h->slot);
    app->playing = true;
    app->accumulator = 0;
    menus_hide_all(&app->menus);
    uint32_t seconds = h->ticks / TICK_RATE;
    console_print_color(app->console, HUD_COLOR_CLIENT, "Playing demo %s: %s on %s, %u:%02u\n", app->player.name, h->name, h->map,
                        seconds / 60, seconds % 60);
}

// record [name]: the game joined, recorded from now until the round ends, into demos/
// (the original's record). stop: the recording stopped, or the demo playing.
static void cmd_record(Console *con, int argc, char **argv, void *user)
{
    App *app = user;
    if (app->playing || !client_net_joined(&app->net)) {
        console_print(con, "record: join a game first\n");
        return;
    }
    demo_stop_recording(app);
    snprintf(app->record_name, sizeof app->record_name, "%s", argc > 1 ? argv[1] : "");
    app->record_asked = true; // begun in the loop, once the frame's packets are in
}

static void cmd_stop(Console *con, int argc, char **argv, void *user)
{
    (void)argc, (void)argv;
    App *app = user;
    app->record_asked = false;
    if (demo_recording(&app->recorder)) demo_stop_recording(app);
    else if (app->playing) demo_stop_playback(app);
    else console_print(con, "no demo is being recorded or played\n");
}

// playdemo <name>: a demo from demos/ played, by its name without the extension, or a
// path to one.
static void cmd_playdemo(Console *con, int argc, char **argv, void *user)
{
    App *app = user;
    if (argc != 2) {
        console_print(con, "usage: playdemo <name>\n");
        return;
    }
    snprintf(app->play_name, sizeof app->play_name, "%s", argv[1]); // opened in the loop, where the world is
}

// demo_pause: the demo playing held, or let go (F6). demo_fast: run at eight times its
// speed, or back at its own (F8, the original's).
static void cmd_demo_pause(Console *con, int argc, char **argv, void *user)
{
    (void)con, (void)argc, (void)argv;
    App *app = user;
    if (app->playing) app->demo_paused = !app->demo_paused;
}

static void cmd_demo_fast(Console *con, int argc, char **argv, void *user)
{
    (void)argc, (void)argv;
    App *app = user;
    if (app->playing) cvar_set(con, "demo_speed", app->demo_speed->number > 1.0f ? "1" : "8");
}

// The demo's records up to its next tick: its packets heard, and at each frame's end
// what they began taken (net_take). False at the demo's end.
static void net_take(App *app);
static bool demo_feed(App *app)
{
    const uint8_t *data;
    size_t size;
    for (;;) {
        switch (demo_play_next(&app->player, &data, &size, &app->demo_tick)) {
        case DEMO_NEXT_PACKET: client_net_feed(&app->net, app->console, app->game, data, size); break;
        case DEMO_NEXT_FRAME: net_take(app); break;
        case DEMO_NEXT_TICK: app->demo_ticked = true; return true;
        default: return false;
        }
        if (!app->game) return false; // a map that couldn't be loaded
    }
}

// My soldier as the demo's tick left it. It stepped on my command, so my shots flew as
// they flew; then its owned half is put back as it was, so it stands (or lies) where it
// stood whatever the steps made of it, and its look, loadout and typing are as recorded.
static void demo_apply_self(App *app)
{
    const DemoTick *t = &app->demo_tick;
    Soldier *s = &app->game->world.soldiers[app->me];
    if (!t->soldier || !s->active) return;
    s->look = t->self.look;
    s->gear = t->self.gear;
    s->primary_choice = t->self.primary_choice;
    s->secondary_choice = t->self.secondary_choice;
    s->typing = t->self.typing;
    s->hit_spray = t->self.hit_spray; // my aim as disturbed then: the shots of the next tick spread as they did
    if (t->self.life == s->life) soldier_copy_owned(app->game->ctx.anims, s, &t->self);
}

// The ticks of the demo playing that have run.
static uint32_t demo_at(const App *app) { return app->player.tick - (app->demo_ticked ? 1 : 0); }

// The demo playing taken to its tick `to` (the original's demo_tick): on from here when
// it lies ahead, else from its start again, the world made anew from its first Map. The
// ticks between run in the frames that follow as fast as they go, unseen and unheard.
static void demo_seek(App *app, int64_t to)
{
    if (!app->playing) return;
    to = to < 0 ? 0 : to > (int64_t)app->player.header.ticks ? (int64_t)app->player.header.ticks : to;
    if ((uint32_t)to < demo_at(app)) {
        demo_play_rewind(&app->player);
        client_net_play(&app->net, app->player.header.slot);
        app->demo_ticked = false;
        if (!demo_feed(app)) {
            demo_stop_playback(app);
            return;
        }
    }
    app->seek_to = (uint32_t)to;
    app->seeking = app->seek_to > demo_at(app);
}

// demo_tick <tick>: the demo playing taken to that tick, 60 a second. demo_tick_r
// <ticks>: that many on, or back with a minus (the arrows: ten seconds).
static void cmd_demo_tick(Console *con, int argc, char **argv, void *user)
{
    App *app = user;
    bool relative = strcmp(argv[0], "demo_tick_r") == 0;
    if (argc != 2) {
        console_print(con, relative ? "usage: demo_tick_r <ticks>, minus for back\n" : "usage: demo_tick <tick>\n");
        return;
    }
    if (!app->playing) return;
    int64_t n = strtoll(argv[1], NULL, 10);
    demo_seek(app, relative ? (int64_t)(app->seeking ? app->seek_to : demo_at(app)) + n : n);
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
    demo_stop_playback(app);
    char address[128];
    snprintf(address, sizeof address, "%s", argv[1]);
    uint16_t port = NET_DEFAULT_PORT;
    char *colon = strrchr(address, ':');
    if (colon) {
        *colon = '\0';
        port = (uint16_t)atoi(colon + 1);
    }
    client_net_connect(&app->net, con, address, port, app->player_name->value, app->password->value);
}

// The game hosted here is over: everyone on it is let go, the port freed.
static void host_stop(App *app)
{
    if (!app->host) return;
    host_close(app->host);
    free(app->host);
    app->host = NULL;
    console_print(app->console, "no longer hosting\n");
}

// disconnect: the line closed, and the game hosted here, if any, with it.
static void cmd_disconnect(Console *con, int argc, char **argv, void *user)
{
    (void)argc, (void)argv;
    App *app = user;
    demo_stop_recording(app);
    demo_stop_playback(app);
    client_net_disconnect(&app->net, con);
    host_stop(app);
}

// browse: the server list asked for anew (net/browser.h), for the main menu's Servers page.
static void cmd_browse(Console *con, int argc, char **argv, void *user)
{
    (void)con, (void)argc, (void)argv;
    App *app = user;
    browser_refresh(&app->browser, app->lobby->value, app->time);
}

// host: a server here on the sv_* and bots_* cvars (Local Play), then join it over the
// loopback. The first map of sv_maps begins, or `map` with no rotation.
static void cmd_host(Console *con, int argc, char **argv, void *user)
{
    (void)argc, (void)argv;
    App *app = user;
    if (app->host) {
        console_print(con, "already hosting on port %d\n", app->host->settings.port);
        return;
    }
    demo_stop_playback(app);
    if (client_net_joined(&app->net)) client_net_disconnect(&app->net, con);
    HostSettings s = {
        .port = (uint16_t)app->sv_port->integer,
        .mode = app->sv_gamemode->integer == 1 ? MATCH_DEATHMATCH : app->sv_gamemode->integer == 2 ? MATCH_CTF : MATCH_MODE_COUNT,
        .time_limit = app->sv_timelimit->integer,
        .score_limit = app->sv_killlimit->integer,
        .bots_noteam = clampi(app->bots_noteam->integer, 0, MAX_PLAYERS),
        .bots_alpha = clampi(app->bots_alpha->integer, 0, MAX_PLAYERS),
        .bots_bravo = clampi(app->bots_bravo->integer, 0, MAX_PLAYERS),
        .bots_difficulty = app->bots_difficulty->integer,
        .bots_chat = app->bots_chat->integer != 0,
        .vote_percent = app->votepercent->integer,
        .quiet = true, // the client's own console says what happens, as online
        .rope = app->rope->integer != 0,
    };
    snprintf(s.assets, sizeof s.assets, "%s", app->assets->value);
    snprintf(s.maps, sizeof s.maps, "%s", app->sv_maps->value);
    snprintf(s.hostname, sizeof s.hostname, "%s", app->sv_hostname->value);
    rounds_next_map(s.maps, "", s.map, sizeof s.map); // the rotation's first, or the map cvar's
    if (!s.map[0]) snprintf(s.map, sizeof s.map, "%s", app->map->value);

    app->host = calloc(1, sizeof(Host));
    if (!app->host || !host_open(app->host, con, &s)) {
        console_print_color(con, HUD_COLOR_WARNING, "could not host %s on port %d\n", s.map, s.port);
        free(app->host);
        app->host = NULL;
        return;
    }
    const Cvar *password = cvar_find(con, "sv_password"); // the hosted game's own, to be let in
    client_net_connect(&app->net, con, "127.0.0.1", s.port, app->player_name->value, password ? password->value : "");
}

// addbot [name] / addbot1 [name] / addbot2 [name]: a bot into the game hosted here, on
// the emptier side, on alpha, or on bravo (the original's server commands).
static void cmd_addbot(Console *con, int argc, char **argv, void *user)
{
    App *app = user;
    if (!app->host) {
        console_print(con, "not hosting: bots join a game hosted here (Local Play, or `host`)\n");
        return;
    }
    Team team = argv[0][6] == '1' ? TEAM_ALPHA : argv[0][6] == '2' ? TEAM_BRAVO : TEAM_NONE;
    host_add_bot(app->host, team, argc > 1 ? argv[1] : NULL);
}

// +radio: the radio menu opened, or shut, as the original's TAction.Radio has it
// (ControlGame.pas): a press flips it and starts the choice over, and it stays open
// without the key held; not while typing, nor for a spectator. -radio does nothing.
// The digits 1 to 3 choose (menu_event): a call, then its place, which sends it.
static void cmd_radio(Console *con, int argc, char **argv, void *user)
{
    (void)con, (void)argc;
    App *app = user;
    if (argv[0][0] != '+') return;
    const Soldier *me = &app->game->world.soldiers[app->me];
    if (app->hud_data.chat_type != HUD_CHAT_NONE || !me->active || me->team == TEAM_SPECTATOR) return;
    app->hud_data.radio_menu = !app->hud_data.radio_menu;
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
    // the original's radio line: '*', the call and the place as digits, then the words
    // (ClientSendStringMessage, MSGTYPE_RADIO), to the team; chat_heard reads it back
    char text[CONSOLE_TEXT_SIZE];
    snprintf(text, sizeof text, "say_team \"*%d%d%s %s\"", d->radio_state, digit, app->radio_first[d->radio_state - 1]->value,
             app->radio_second[d->radio_state - 1][digit - 1]->value);
    console_execute(app->console, text);
    d->radio_menu = false;
    d->radio_state = 0;
}

// radio <call> <place> [words]: the radio menu's two digits at once. With words after
// them, those are said to the team as the call, with its sound — one message, a
// taunt's with a radio call attached (chat_heard plays the sound for the '*', the
// digits and the words after them). Without, the menu's own words, as its choices
// would be (radio_choose).
static void cmd_radio_call(Console *con, int argc, char **argv, void *user)
{
    App *app = user;
    if (argc < 3 || strlen(argv[1]) != 1 || strlen(argv[2]) != 1 || argv[1][0] < '1' || argv[1][0] > '0' + RADIO_CALLS ||
        argv[2][0] < '1' || argv[2][0] > '0' + RADIO_CALLS) {
        console_print(con, "usage: %s <call> <place> [words], 1 to %d each\n", argv[0], RADIO_CALLS);
        return;
    }
    const Soldier *me = &app->game->world.soldiers[app->me];
    if (app->hud_data.chat_type != HUD_CHAT_NONE || !me->active || me->team == TEAM_SPECTATOR) return;
    app->hud_data.radio_menu = false; // the menu open, its choice starts over
    app->hud_data.radio_state = 0;
    if (argc > 3) {
        char line[CONSOLE_TEXT_SIZE];
        snprintf(line, sizeof line, "say_team \"*%s%s", argv[1], argv[2]);
        for (int i = 3; i < argc; i++) { // the words, joined with single spaces as cmd_say does
            size_t used = strlen(line);
            snprintf(line + used, sizeof line - used, "%s%s", i > 3 ? " " : "", argv[i]);
        }
        snprintf(line + strlen(line), sizeof line - strlen(line), "\"");
        console_execute(con, line);
        return;
    }
    radio_choose(app, argv[1][0] - '0');
    radio_choose(app, argv[2][0] - '0');
}

static void cmd_freecam(Console *con, int argc, char **argv, void *user);

static HudGameMode hud_mode(const App *app);

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
    if (strcmp(name, "escmenu") == 0) {
        // the kick or map window open, Escape goes back to the escape menu alone (ControlGame.pas)
        if (m->menus[MENU_KICK].active || m->menus[MENU_MAP].active) {
            menus_show(m, MENU_KICK, false, hud_mode(app), 1);
            menus_show(m, MENU_MAP, false, hud_mode(app), 1);
        } else {
            menus_show(m, MENU_ESC, !m->menus[MENU_ESC].active, hud_mode(app), 1);
        }
    }
    else if (strcmp(name, "weaponsmenu") == 0) {
        // ControlGame.pas (TAction.Weapons): dead, the key opens and closes the menu, and
        // closed that way it stays closed through the spawn (the lock) until opened again.
        // Alive, the key never opens it: it closes one left open, and toggles the lock, so
        // the menu is not seen again until the next death.
        const Soldier *me = &app->game->world.soldiers[app->me];
        if (m->menus[MENU_ESC].active || !me->active || me->team == TEAM_SPECTATOR) return;
        if (me->dead) {
            menus_show(m, MENU_LIMBO, !m->menus[MENU_LIMBO].active, hud_mode(app), 1);
            app->limbo_lock = !m->menus[MENU_LIMBO].active;
        } else {
            bool armed = me->weapon.id != WEAPON_NONE && me->secondary.id != WEAPON_NONE;
            if (m->menus[MENU_LIMBO].active && !armed) return;
            menus_show(m, MENU_LIMBO, false, hud_mode(app), 1);
            app->limbo_lock = !app->limbo_lock;
        }
        console_print_color(con, HUD_COLOR_GAME, app->limbo_lock ? "Weapons menu disabled\n" : "Weapons menu active\n");
    }
    else if (strcmp(name, "teammenu") == 0) menus_show(m, MENU_TEAM, !m->menus[MENU_TEAM].active, hud_mode(app), 1);
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
    app->fpslimit = cvar_register(con, "r_fpslimit", "1", CVAR_ARCHIVE, "1: frames are drawn at most r_maxfps a second; 0: as fast as they come");
    app->maxfps = cvar_register(con, "r_maxfps", "500", CVAR_ARCHIVE, "the frames drawn a second at most, while r_fpslimit is on");
    app->fullscreen = cvar_register(con, "r_fullscreen", "0", CVAR_ARCHIVE, "0 windowed, 1 fullscreen, 2 borderless window");
    app->server = cvar_register(con, "cl_server", "127.0.0.1:23073", CVAR_ARCHIVE, "the server the main menu joins, host:port");
    app->password = cvar_register(con, "cl_password", "", CVAR_ARCHIVE, "the password the main menu joins with; empty for none");
    app->lobby = cvar_register(con, "cl_lobby", QUERY_LOBBY_URL, CVAR_ARCHIVE, "the lobby the server browser asks for its list");
    app->sensitivity = cvar_register(con, "cl_sensitivity", "1", CVAR_ARCHIVE, "the mouse's speed");
    app->forcebg = cvar_register(con, "r_forcebg", "0", CVAR_ARCHIVE, "1: the sky in r_forcebg_color1 and 2 on every map instead of the map's own colours");
    app->forcebg_color1 = cvar_register(con, "r_forcebg_color1", "000000", CVAR_ARCHIVE, "the forced sky's colour at the top, RRGGBB");
    app->forcebg_color2 = cvar_register(con, "r_forcebg_color2", "000000", CVAR_ARCHIVE, "the forced sky's colour at the bottom, RRGGBB");
    app->scenery = cvar_register(con, "r_scenery", "1", CVAR_ARCHIVE, "the scenery behind the map; 0 leaves it out, the middle and front scenery stay");
    app->trails = cvar_register(con, "r_trails", "1", CVAR_ARCHIVE, "the streaks behind the bullets, grenades and rockets; 0 leaves them out");
    app->weather = cvar_register(con, "r_weathereffects", "1", CVAR_ARCHIVE, "the map's weather: its rain, sandstorm or snow, and the wind; 0 leaves them out");
    app->wireframe = cvar_register(con, "r_wireframe", "0", 0, "draw the map's polygons as lines");
    app->debug = cvar_register(con, "r_debug", "0", 0, "spawn points, colliders, special polys, bones");
    app->minimap = cvar_register(con, "ui_minimap", "0", CVAR_ARCHIVE, "the minimap");
    app->info = cvar_register(con, "ui_info", "0", CVAR_ARCHIVE, "the FPS and ping line");
    app->track_shot = cvar_register(con, "cl_trackshot", "1", CVAR_ARCHIVE, "the camera follows a Barrett shot fired scoped, until you stand up");
    app->player_names = cvar_register(con, "ui_playernames", "1", CVAR_ARCHIVE, "teammates' names at the screen's edge when out of view (everyone's, spectating), and the ping dot");
    app->console_length =
        cvar_register(con, "ui_console_length", "6", CVAR_ARCHIVE, "how many console lines the HUD shows");
    app->player_name = cvar_register(con, "cl_player_name", "Player", CVAR_ARCHIVE, "my name");
    app->grenade_color = cvar_register(con, "cl_grenade_color", "", CVAR_ARCHIVE, "the grenades in this colour, RRGGBB, flat and solid; empty for their own art");
    app->cursor_color = cvar_register(con, "cl_cursor_color", "FFFFFF", CVAR_ARCHIVE, "the menu cursor's colour, RRGGBB");
    app->crosshair_color = cvar_register(con, "cl_crosshair_color", "FFFFFF", CVAR_ARCHIVE, "the aiming crosshair's colour, RRGGBB");
    app->cursor_size = cvar_register(con, "cl_cursor_size", "100", CVAR_ARCHIVE, "the menu cursor's size, percent");
    app->crosshair_size = cvar_register(con, "cl_crosshair_size", "100", CVAR_ARCHIVE, "the aiming crosshair's size, percent");
    app->shirt = cvar_register(con, "cl_player_shirt", "304289", CVAR_ARCHIVE, "the shirt's colour, RRGGBB");
    app->pants = cvar_register(con, "cl_player_pants", "FF0000", CVAR_ARCHIVE, "the pants' colour, RRGGBB");
    app->skin = cvar_register(con, "cl_player_skin", "E6B478", CVAR_ARCHIVE, "the skin's colour, RRGGBB");
    app->hair = cvar_register(con, "cl_player_hair", "000000", CVAR_ARCHIVE, "the hair's colour, RRGGBB");
    app->jet = cvar_register(con, "cl_player_jet", "00008B", CVAR_ARCHIVE, "the jet flame's colour, RRGGBB");
    app->hair_style = cvar_register(con, "cl_player_hairstyle", "0", CVAR_ARCHIVE,
                                    "0 army, 1-4 the male's (dreadlocks, punk, Mr. T, normal), 5-6 the waifu's (fringe, bob); the rat and the furry wear only army, punk and Mr. T");
    app->head_style = cvar_register(con, "cl_player_headstyle", "0", CVAR_ARCHIVE,
                                    "0 none, 1-2 the male's (helmet, hat), 3 the waifu's; the rat and the furry wear none");
    app->chain_style = cvar_register(con, "cl_player_chainstyle", "0", CVAR_ARCHIVE, "0 none, 1 dog tags, 2 gold chain");
    app->style = cvar_register(con, "cl_player_style", "0", CVAR_ARCHIVE, "the gostek: 0 male, 1 female, 2 waifu, 3 rat, 4 furry");
    app->primary = cvar_register(con, "cl_player_wep", "1", CVAR_ARCHIVE, "the primary at the next spawn, 1 to 10");
    app->secondary = cvar_register(con, "cl_player_secwep", "1", CVAR_ARCHIVE, "0 USSOCOM, 1 knife, 2 chainsaw, 3 LAW");
    app->gear = cvar_register(con, "cl_player_gear", "0", CVAR_ARCHIVE, "the gear at the next spawn, 0 jets, 1 rope");
    app->smooth = cvar_register(con, "cl_smooth", "100", CVAR_ARCHIVE,
                                "milliseconds a correction of another player is smoothed over; 0 snaps");
    app->interp = cvar_register(con, "cl_interp", "0", CVAR_ARCHIVE,
                                "ticks the others are shown behind the newest snapshot, at least, so jitter doesn't show; raised by itself while snapshots come late");
    app->netstats = cvar_register(con, "cl_netstats", "0", 0,
                                  "1: a line a second on the console: ping, frames in hand, snapshots late and missed, the view clock's nudges, and how far the others were corrected");
    app->rope_debug = cvar_register(con, "cl_rope_debug", "0", 0,
                                    "log each soldier's rope each half second and changes at once, with the snapshots dropped");
    app->volume = cvar_register(con, "snd_volume", "50", CVAR_ARCHIVE, "the sound's volume, 0 to 100");
    app->effects_battle = cvar_register(con, "snd_effects_battle", "0", CVAR_ARCHIVE, "1: a far shot or blast also plays its distant sound");
    app->effects_explosions = cvar_register(con, "snd_effects_explosions", "0", CVAR_ARCHIVE, "1: a blast next to you rings your ears and muffles the rest for a few seconds");
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
    app->menu_page = cvar_register(con, "ui_menupage", "0", 0, "the main menu's page at start, 0 servers to 8 graphics (for screenshots)");
    // the server's, for Local Play; saved, as the menu sets them
    app->sv_port = cvar_register(con, "sv_port", "23073", CVAR_ARCHIVE, "the UDP port a game hosted here listens on");
    app->sv_maps = cvar_register(con, "sv_maps", "", CVAR_ARCHIVE, "the maps in rotation, space-separated; the first plays first");
    app->sv_hostname = cvar_register(con, "sv_hostname", "SoldatReloaded server", CVAR_ARCHIVE, "the hosted game's name, on the scoreboard");
    cvar_register(con, "sv_password", "", CVAR_ARCHIVE, "the hosted game's password; empty for none");
    app->sv_gamemode = cvar_register(con, "sv_gamemode", "0", CVAR_ARCHIVE, "0 the map's own, 1 deathmatch, 2 capture the flag");
    app->sv_timelimit = cvar_register(con, "sv_timelimit", "15", CVAR_ARCHIVE, "minutes a round lasts");
    app->sv_killlimit = cvar_register(con, "sv_killlimit", "10", CVAR_ARCHIVE, "the score that wins a round: kills, or captures in CTF");
    app->rope = cvar_register(con, "sv_rope", "0", CVAR_ARCHIVE, "1: the rope is allowed in a game hosted here, an experimental gear in place of the jets; 0 gives everyone jets");
    app->bots_noteam = cvar_register(con, "bots_random_noteam", "0", CVAR_ARCHIVE, "bots in a deathmatch");
    app->bots_alpha = cvar_register(con, "bots_random_alpha", "0", CVAR_ARCHIVE, "bots on alpha in capture the flag");
    app->bots_bravo = cvar_register(con, "bots_random_bravo", "0", CVAR_ARCHIVE, "bots on bravo in capture the flag");
    app->bots_difficulty = cvar_register(con, "bots_difficulty", "100", CVAR_ARCHIVE, "300 stupid, 200 poor, 100 normal, 50 hard, 10 impossible");
    app->bots_chat = cvar_register(con, "bots_chat", "1", CVAR_ARCHIVE, "whether the bots talk");
    app->votepercent = cvar_register(con, "sv_votepercent", "60", CVAR_ARCHIVE, "the percentage of players whose yes passes a vote");
    app->demo_autorecord = cvar_register(con, "demo_autorecord", "0", CVAR_ARCHIVE, "1: a demo of every round joined, into demos/");
    app->demo_speed = cvar_register(con, "demo_speed", "1", 0, "a demo's playback speed: 1 its own, 0.5 half, 8 eight times");
    console_add_command(con, "record", cmd_record, app, "record the game joined, until the round ends, into demos/: record [name]");
    console_add_command(con, "stop", cmd_stop, app, "stop the demo being recorded, or played");
    console_add_command(con, "playdemo", cmd_playdemo, app, "play a demo from demos/: playdemo <name>");
    console_add_command(con, "demo_pause", cmd_demo_pause, app, "pause the demo playing, or go on");
    console_add_command(con, "demo_fast", cmd_demo_fast, app, "the demo playing at eight times its speed, or back at its own");
    console_add_command(con, "demo_tick", cmd_demo_tick, app, "take the demo playing to a tick, 60 a second: demo_tick <tick>");
    console_add_command(con, "demo_tick_r", cmd_demo_tick, app, "take the demo playing on, or back with a minus: demo_tick_r <ticks>");
    console_add_command(con, "quit", cmd_quit, app, "leave the game");
    console_add_command(con, "togglewindow", cmd_togglewindow, app, "a window, or back to the fullscreen it came from");
    console_add_command(con, "screenshot", cmd_screenshot, app, "write the 60th frame from now to a PNG, then quit");
    console_add_command(con, "escmenu", cmd_menu, app, "the escape menu");
    console_add_command(con, "weaponsmenu", cmd_menu, app, "the weapons menu");
    console_add_command(con, "freecam", cmd_freecam, app, "the free camera while dead or watching, or a demo plays; jump does the same");
    console_add_command(con, "teammenu", cmd_menu, app, "the team menu");
    console_add_command(con, "fragsmenu", cmd_menu, app, "the scoreboard");
    console_add_command(con, "statsmenu", cmd_menu, app, "the weapon stats");
    console_add_command(con, "say", cmd_say, app, "say something to everyone");
    console_add_command(con, "say_team", cmd_say, app, "say something to the team");
    console_add_command(con, "radio", cmd_radio_call, app, "say a radio call: radio <call> <place>, 1 to 3 each");
    console_add_command(con, "chat", cmd_chat, app, "type a line to everyone");
    console_add_command(con, "teamchat", cmd_chat, app, "type a line to the team");
    console_add_command(con, "cmd", cmd_chat, app, "type a command: a cvar or command here, or a word for the server");
    console_add_command(con, "votemap", cmd_vote, app, "start a vote to change the map: votemap <map>");
    console_add_command(con, "votekick", cmd_vote, app, "start a vote to kick a player: votekick <name or slot>");
    console_add_command(con, "+radio", cmd_radio, app, "open the radio menu, or shut it");
    console_add_command(con, "-radio", cmd_radio, app, NULL);
    console_add_command(con, "connect", cmd_connect, app, "join a server: connect <address[:port]>");
    console_add_command(con, "disconnect", cmd_disconnect, app, "leave the server, and stop hosting one here");
    console_add_command(con, "host", cmd_host, app, "host a game here on the sv_* and bots_* cvars, and join it");
    console_add_command(con, "browse", cmd_browse, app, "ask the lobby (cl_lobby) for its servers, and each server what it is playing");
    console_add_command(con, "addbot", cmd_addbot, app, "a bot into the game hosted here: addbot [name]");
    console_add_command(con, "addbot1", cmd_addbot, app, "a bot into alpha of the game hosted here: addbot1 [name]");
    console_add_command(con, "addbot2", cmd_addbot, app, "a bot into bravo of the game hosted here: addbot2 [name]");
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

// The mode as the HUD and the menus take it, from the match as it is now. A menu opened
// on a join's first snapshot goes by this, not by the HUD's copy, which is built only
// as a frame is drawn and still holds the last match's mode (or none) until then: the
// team menu offered "0 Player" and Spectator in a CTF game.
static HudGameMode hud_mode(const App *app) { return team_game(app) ? HUD_MODE_CTF : HUD_MODE_DEATHMATCH; }

// cl_grenade_color as the drawing takes it: alpha 0, the grenades' own art, while it
// holds no colour (empty, its default); else that colour, solid.
static Rgba grenade_color(const Cvar *cv)
{
    Rgba color = {0};
    if (!rgba_parse_hex(cv->value, &color)) return (Rgba){0};
    color.a = 255;
    return color;
}

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
        .hair_style = (uint8_t)clampi(app->hair_style->integer, 0, 6),
        .head_style = (uint8_t)clampi(app->head_style->integer, 0, 3),
        .chain_style = (uint8_t)clampi(app->chain_style->integer, 0, 2),
        .style = (uint8_t)clampi(app->style->integer, 0, GOSTEK_STYLE_COUNT - 1),
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

static Rect window_rect(const App *app);

static void apply_cvars(App *app)
{
    if (app->fullscreen->modified || app->width->modified || app->height->modified) apply_window_mode(app);
    if (app->swapeffect->modified) {
        gfx_vsync(app->swapeffect->integer != 0);
        app->swapeffect->modified = false;
    }
    app->input.sensitivity = app->sensitivity->number;
    app->consoles.main.count_max = app->consoles.main.visible = clampi(app->console_length->integer, 1, HUD_CONSOLE_LINES);
    // the original's curve: 50 is a quarter of the way up, and it is quiet enough there
    float v = clampf(app->volume->number / 100.0f, 0.0f, 1.0f);
    audio_volume(&app->audio, v * v * 0.48f);
    app->render_options.wireframe = app->wireframe->integer != 0;
    app->render_options.debug = app->debug->integer != 0;
    app->render_options.scenery = app->scenery->integer != 0;
    app->render_options.trails = app->trails->integer != 0;
    app->audio.weather_off = !app->weather->integer;
    app->audio.battle = app->effects_battle->integer != 0;
    app->audio.explosions = app->effects_explosions->integer != 0;
    // the sky's colours: the map's, or mine; the minimap carries them too, so it is built again on a change
    if (map_view_force_background(&app->render.map_view, app->forcebg->integer != 0, cvar_color(app->forcebg_color1), cvar_color(app->forcebg_color2)))
        map_view_build_minimap(&app->render.map_view, window_rect(app).height);
    if (app->playing) return; // my soldier is the demo's recorder, dressed and armed as it was
    Soldier *me = &app->game->world.soldiers[app->me];
    me->look = look_from_cvars(app);
    me->gear = (Gear)clampi(app->gear->integer, GEAR_JETS, GEAR_ROPE);
    if (!app->game->world.rules.rope && me->gear == GEAR_ROPE) me->gear = GEAR_JETS; // sv_rope off: the boots are jets
    me->primary_choice = (WeaponId)clampi(app->primary->integer, WEAPON_EAGLE, WEAPON_MINIGUN);
    me->secondary_choice = (WeaponId)(WEAPON_COLT + clampi(app->secondary->integer, 0, WEAPON_LAW - WEAPON_COLT));
    app->net.look = me->look; // what the Hello says of me
    app->net.gear = me->gear;
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
    MatchSettings settings = match_settings_for_map(g->ctx.map);
    settings.rope = local ? app->rope->integer != 0 : app->net.rope; // sv_rope: the host's word, heard with the map
    game_init(g, 1, settings);
    app->menus.rope = g->world.rules.rope; // what the boots row of the weapons menu may offer
    g->world.authority = local;
    for (int i = 0; i < MAX_PLAYERS; i++) g->world.soldiers[i].look = look_from_cvars(app);
    if (!local) return true;

    Soldier *me = &g->world.soldiers[app->me];
    Team team = team_game(app) ? TEAM_ALPHA : TEAM_NONE;
    Vec2 at = spawn_point(g->ctx.map, team, &g->world.rng);
    Gear gear = (Gear)clampi(app->gear->integer, GEAR_JETS, GEAR_ROPE);
    if (!g->world.rules.rope && gear == GEAR_ROPE) gear = GEAR_JETS; // no rope in this game: the boots are jets
    WeaponId primary = (WeaponId)clampi(app->primary->integer, WEAPON_EAGLE, WEAPON_MINIGUN);
    WeaponId secondary = (WeaponId)(WEAPON_COLT + clampi(app->secondary->integer, 0, WEAPON_LAW - WEAPON_COLT));
    soldier_spawn(&g->ctx, me, at, team, gear, primary, secondary);
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
#ifndef _WIN32
    // the window's class, the launcher's menu entry's (launcher/desktop.h), unless the
    // player has given one
    setenv("SDL_VIDEO_X11_WMCLASS", "soldatreloaded", 0);
    setenv("SDL_VIDEO_WAYLAND_WMCLASS", "soldatreloaded", 0);
#endif
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    // a controller works the main menu; without the subsystem the game goes on without one
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) fprintf(stderr, "no controllers: %s\n", SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    app->window = SDL_CreateWindow("Soldat Reloaded", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, app->width->integer,
                                   app->height->integer, SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!app->window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
#ifndef _WIN32
    // the badge as the window's icon; on Windows SDL gives it the executable's own
    GfxImage icon;
    if (gfx_image_load(&icon, "assets/icon.png")) {
        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(icon.rgba, icon.width, icon.height, 32, icon.width * 4,
                                                                  SDL_PIXELFORMAT_RGBA32);
        if (surface) {
            SDL_SetWindowIcon(app->window, surface);
            SDL_FreeSurface(surface);
        }
        gfx_image_free(&icon);
    }
#endif
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
    if (i == app->me) snprintf(name, size, "%s", app->playing ? app->player.header.name : app->player_name->value);
    else if (heard[0]) snprintf(name, size, "%s", heard);
    else snprintf(name, size, "Player %d", i + 1);
}

// The next player to watch, from the one watched: alive, no spectator, and a teammate
// unless I am watching from outside (GetCameraTarget). False with nobody to watch. A
// demo's watcher is outside, and its recorder, alive or dead, is among those watched.
static bool camera_next(App *app, bool backwards)
{
    const World *w = &app->game->world;
    const Soldier *me = &w->soldiers[app->me];
    bool outside = app->playing || me->team == TEAM_SPECTATOR || !team_game(app);
    int from = app->camera_follow < 0 ? app->me : app->camera_follow;
    for (int n = 1; n <= MAX_PLAYERS; n++) {
        int j = ((from + (backwards ? -n : n)) % MAX_PLAYERS + MAX_PLAYERS) % MAX_PLAYERS;
        const Soldier *s = &w->soldiers[j];
        if (j == app->me && app->playing && s->active && s->team != TEAM_SPECTATOR) {
            app->camera_follow = -1;
            app->free_camera = false;
            return true;
        }
        if (j == app->me || !s->active || s->dead || s->team == TEAM_SPECTATOR) continue;
        if (!outside && s->team != me->team) continue;
        app->camera_follow = j;
        app->free_camera = false;
        return true;
    }
    return false;
}

// The free camera, pushed about by the cursor: the original's for a dead player who
// presses jump, and for anyone watching with nobody to watch.
static void camera_free(App *app)
{
    app->camera_follow = -1;
    app->free_camera = true;
}

// freecam: the free camera, for a player who is dead or watching; bindable, though the
// jump key does the same while dead, as the original's does.
static void cmd_freecam(Console *con, int argc, char **argv, void *user)
{
    (void)con, (void)argc, (void)argv;
    App *app = user;
    const Soldier *me = &app->game->world.soldiers[app->me];
    bool watching = me->active && (me->dead || me->team == TEAM_SPECTATOR) && !app->menus.menus[MENU_LIMBO].active;
    if (watching || app->playing) camera_free(app);
}

static void snapshot_tick(App *app)
{
    app->previous = app->latest;
    tick_snapshot_capture(&app->latest, &app->game->world);
}

// cl_rope_debug: each tick a soldier's rope changed, and every 30th tick every
// soldier's, one line of the state as this machine has it; a snapshot dropped or held
// back since the last line too. With a friend's rope missing, their lines stay phase 0
// here while the friend's own client shows a rope out — the two consoles side by side
// say where it goes wrong.
static void rope_debug_tick(App *app)
{
    if (!app->rope_debug->integer) return;
    World *w = &app->game->world;
    bool periodic = w->tick % 30 == 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Soldier *s = &w->soldiers[i];
        if (!s->active) continue;
        bool changed = s->rope != app->rope_seen[i].rope || s->rope_wraps_count != app->rope_seen[i].wraps ||
                       vec2_length(vec2_sub(s->pos, app->rope_seen[i].pos)) > 30.0f;
        if (!periodic && !changed) continue;
        app->rope_seen[i].rope = (uint8_t)s->rope;
        app->rope_seen[i].wraps = s->rope_wraps_count;
        app->rope_seen[i].pos = s->pos;
        console_print(app->console, "rope %d%s t%u %d tip (%.0f,%.0f) len %.0f wraps %d pos (%.0f,%.0f) vel (%.1f,%.1f)\n",
                      i, i == app->me ? " me" : (s->remote ? "" : " local"), w->tick, s->rope, s->rope_tip.x,
                      s->rope_tip.y, s->rope_len, s->rope_wraps_count, s->pos.x, s->pos.y, s->vel.x, s->vel.y);
    }
    if (app->net.stream.dropped != app->rope_dropped) {
        console_print(app->console, "rope: %u snapshots dropped\n", app->net.stream.dropped);
        app->rope_dropped = app->net.stream.dropped;
    }
    if (app->net.stream.held_back != app->rope_held_back) {
        console_print(app->console, "rope: %u snapshots held back\n", app->net.stream.held_back);
        app->rope_held_back = app->net.stream.held_back;
    }
}

// One tick of the game on this frame's input. Online, the snapshot of the tick on show
// goes onto the world first (client_stream_begin_tick), everyone else steps on the
// keys they were last heard with (stream_command), and my state goes to the server.
// My bullet `shot`, while it flies; NULL once it is gone.
static const Bullet *my_shot(const App *app, uint32_t shot)
{
    const World *w = &app->game->world;
    for (int i = 0; i < MAX_BULLETS; i++) {
        const Bullet *b = &w->bullets[i];
        if (b->active && b->owner == app->me && b->shot_id == shot) return b;
    }
    return NULL;
}

// The camera rides a Barrett shot fired scoped (the original's bullet Tracking,
// Bullets.pas): a shot of mine this tick, scoped before it, is followed (cl_trackshot),
// the newest if there are several, until it is gone, or I stand up or die.
static void track_shot(App *app, bool scoped)
{
    const Events *events = &app->game->events;
    if (scoped && app->track_shot->integer) {
        for (int i = 0; i < events->count; i++) {
            const Event *e = &events->items[i];
            if (e->type == EVENT_SHOT && e->shot.player == app->me && e->shot.weapon == WEAPON_BARRETT) {
                app->tracking = true;
                app->tracking_shot = e->shot.shot;
            }
        }
    }
    const Soldier *me = &app->game->world.soldiers[app->me];
    if (app->tracking && (!app->track_shot->integer || !me->active || me->dead || me->stance == STANCE_STAND || !my_shot(app, app->tracking_shot)))
        app->tracking = false;
}

static void tick(App *app)
{
    World *w = &app->game->world;
    bool online = client_net_joined(&app->net);
    // a demo playing: the tick shown is the demo's, and my command and soldier its recorder's
    bool playing = app->playing;
    Command cmds[MAX_PLAYERS] = {0};
    if (!playing) w->soldiers[app->me].typing = app->hud_data.chat_type != HUD_CHAT_NONE; // the dots over my head, for the others
    if (playing) app->net.stream.view_at = app->demo_tick.view;
    if (online) client_stream_begin_tick(&app->net.stream, app->game, app->me, app->interp->integer);
    uint32_t view = w->tick; // the tick on show, for a demo being recorded
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Soldier *s = &w->soldiers[i];
        s->remote = online && i != app->me;
        if (s->remote) cmds[i] = stream_command(s, client_stream_quiet(&app->net.stream, i));
    }
    Command input = input_command(&app->input, ++app->seq);
    cmds[app->me] = playing ? app->demo_tick.cmd : input;
    // scoped before the tick: the shot snaps the sniper view back within it
    const Soldier *shooter = &w->soldiers[app->me];
    bool scoped = shooter->active && !shooter->dead && shooter->aim_dist < DEFAULT_AIM_DIST;
    game_tick(app->game, cmds);
    if (playing) demo_apply_self(app);
    track_shot(app, scoped);
    if (app->game->match.state != MATCH_PAUSED && !app->seeking) { // paused, the sparks hang too; a seek makes none
        render_tick(&app->render, &app->game->ctx, &app->game->world, &app->game->events);
        // the map's weather over the view (WeatherEffects.pas), while r_weathereffects is
        // on; none is made as a round ends, as the original's UpdateFrame makes none then
        if (app->weather->integer && app->game->match.state == MATCH_PLAYING)
            sparks_weather(&app->render.sparks, app->game->ctx.map->weather, app->camera.pos, camera_view_size(&app->camera),
                           app->game->world.tick);
    }
    // the listener is whom the camera follows: me, the player I watch while dead, or the free camera
    int followed = app->free_camera ? -1 : app->camera_follow >= 0 ? app->camera_follow : app->me;
    if (!app->seeking) audio_tick(&app->audio, app->game, app->me, followed, app->camera.pos, &app->render.sparks);
    if (online) client_net_tick(&app->net, app->game);
    if (demo_recording(&app->recorder)) {
        const Soldier *s = &w->soldiers[app->me];
        demo_record_tick(&app->recorder, view, &cmds[app->me], app->input.cursor, s->active ? s : NULL);
    }
    rope_debug_tick(app);
    input_clear(&app->input);
    snapshot_tick(app);
    char names[MAX_PLAYERS][HUD_NAME];
    for (int i = 0; i < MAX_PLAYERS; i++) player_name(app, i, names[i], sizeof names[i]);
    feed_tick(&app->feed, app->console, app->game, names, team_game(app), app->me);
    consoles_tick(&app->consoles);

    // The weapons menu (NetworkClientSprite.pas, NetworkUtils.pas): it opens at my
    // death, unless I closed it while dead (the lock), and at my first life, before I
    // have picked anything. It stays through the spawn, to pick with, until I pick a
    // primary or move; the weaponsmenu key will not bring it back while I live. A game
    // with teams asks the team first: the server keeps me watching until I say.
    const Soldier *me = &w->soldiers[app->me];
    bool spectator = me->active && me->team == TEAM_SPECTATOR;
    bool dead = me->active && me->dead && !spectator;
    bool limbo = app->menus.menus[MENU_LIMBO].active, esc = app->menus.menus[MENU_ESC].active;
    bool first_life = me->active && !spectator && app->seen_life < 0;
    if (me->active && !spectator) app->seen_life = me->life;
    if (!playing) { // a demo's recorder picks nothing here
        if ((first_life || (dead && !app->was_dead)) && !app->limbo_lock && !limbo && !esc) {
            menus_show(&app->menus, MENU_LIMBO, true, hud_mode(app), 1);
        }
        const Buttons moving = BUTTON_LEFT | BUTTON_RIGHT | BUTTON_JUMP | BUTTON_CROUCH | BUTTON_PRONE | BUTTON_JET | BUTTON_FIRE | BUTTON_THROW;
        if (limbo && !dead && (cmds[app->me].buttons & moving)) menus_show(&app->menus, MENU_LIMBO, false, hud_mode(app), 1);
        if (spectator && team_game(app) && !app->team_asked && !esc) {
            menus_show(&app->menus, MENU_TEAM, true, hud_mode(app), 1);
            app->team_asked = true;
        }
    }
    app->was_dead = dead;

    // Watching (LocalInput.pas, "change camera when dead"): as I die the camera stays on
    // my body, as the original's CameraFollowSprite stays on mine; joining as a spectator,
    // with no body, it goes to the first player up. Then, with no weapons menu open, fire
    // follows the next player and jet the one before, among those alive I may watch (my
    // team's in a team game); jump, or the freecam command, is the free camera, which the
    // cursor pushes; and fire with nobody to follow is that too. Alive, the camera is
    // mine again.
    // A demo playing is watched from outside, by my own keys and at any time: fire and jet
    // go round the players and its recorder, jump is the free camera.
    Buttons keys = playing ? input.buttons : cmds[app->me].buttons;
    Buttons pressed = (Buttons)(keys & ~app->camera_keys);
    app->camera_keys = keys;
    bool watching = me->active && (me->dead || spectator);
    if (playing) {
        if (!menus_any_active(&app->menus)) {
            if (pressed & BUTTON_JUMP) camera_free(app);
            else if ((pressed & (BUTTON_FIRE | BUTTON_JET)) && !camera_next(app, (pressed & BUTTON_JET) != 0)) {
                app->camera_follow = -1;
                app->free_camera = false;
            }
        }
    } else if (watching) {
        if (!app->was_watching) {
            app->camera_follow = -1;
            app->free_camera = false;
            if (spectator && !camera_next(app, false)) camera_free(app);
        } else if (!limbo) {
            if (pressed & BUTTON_JUMP) camera_free(app);
            else if ((pressed & (BUTTON_FIRE | BUTTON_JET)) && !camera_next(app, (pressed & BUTTON_JET) != 0)) camera_free(app);
        }
    } else {
        app->camera_follow = -1;
        app->free_camera = false;
    }
    app->was_watching = watching;
    for (int i = 0; i < MAX_PLAYERS; i++) // what was said fades
        if (app->hud_data.players[i].chat_delay > 0) app->hud_data.players[i].chat_delay--;
    if (app->radio_cooldown > 0) app->radio_cooldown--;
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
    if (action.kind != MENU_ACTION_NONE) audio_flat(&app->audio, "menuclick.wav"); // the original's, on anything a menu did
    switch (action.kind) {
    case MENU_ACTION_QUIT: // exit to the main menu: the line closed, the menus down
        console_execute(app->console, "disconnect");
        menus_hide_all(&app->menus);
        chat_close(app);
        input_release_all(&app->input);
        mainmenu_show(&app->mainmenu, true);
        break;
    case MENU_ACTION_OPEN_TEAM_MENU:
        menus_show(&app->menus, MENU_TEAM, true, hud_mode(app), 1);
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
    case MENU_ACTION_PICK_GEAR: {
        char number[8];
        snprintf(number, sizeof number, "%d", action.value);
        cvar_set(app->console, "cl_player_gear", number);
        if (!me->dead) me->rope = ROPE_NONE; // a gear change cuts the rope; the switch itself is apply_cvars'
        break;
    }
    case MENU_ACTION_KICK: // the reason first, typed at the prompt; the vote goes with it
        if (action.value < 0 || action.value >= MAX_PLAYERS || !app->hud_data.players[action.value].active) break;
        app->kick_target = action.value;
        chat_open(app, HUD_CHAT_PUBLIC);
        app->vote_reason_typing = app->hud_data.chat_type != HUD_CHAT_NONE;
        app->hud_data.vote_reason_typing = app->vote_reason_typing;
        break;
    case MENU_ACTION_VOTE_MAP: { // the map the window shows: the server's, as it answered
        char text[HUD_TEXT];
        if (client_net_joined(&app->net)) {
            if (!app->net.map_reply.map[0]) break;
            snprintf(text, sizeof text, "/votemap %s", app->net.map_reply.map);
        } else {
            if (app->map_count == 0) break;
            snprintf(text, sizeof text, "/votemap %s", app->maps[clampi(action.value, 0, app->map_count - 1)]);
        }
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

// An open menu takes the keys and clicks the original gives it (ControlGame.pas): a
// digit chooses, Ctrl and a digit chooses a secondary in the weapons menu, a left click
// picks. The radio menu takes the digits too. True if it took the event.
static bool menu_event(App *app, const SDL_Event *e)
{
    GameMenus *m = &app->menus;
    bool digit_down = e->type == SDL_KEYDOWN && !e->key.repeat && e->key.keysym.scancode >= SDL_SCANCODE_1 &&
                      e->key.keysym.scancode <= SDL_SCANCODE_0;
    int digit = e->key.keysym.scancode == SDL_SCANCODE_0 ? 0 : e->key.keysym.scancode - SDL_SCANCODE_1 + 1;
    // The radio menu (ControlGame.pas): Escape shuts it, and the escape menu waits for
    // the next press; 1 to 3 with no modifier choose, the rest of the keys going on to
    // their binds. An open menu takes the digits first, as the original's do.
    bool plain = e->type == SDL_KEYDOWN && !(e->key.keysym.mod & (KMOD_CTRL | KMOD_SHIFT | KMOD_ALT | KMOD_GUI));
    if (app->hud_data.radio_menu && plain && !e->key.repeat && e->key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
        app->hud_data.radio_menu = false;
        app->hud_data.radio_state = 0;
        return true;
    }
    if (app->hud_data.radio_menu && !menus_any_active(m) && plain && digit_down && digit >= 1 && digit <= RADIO_CALLS) {
        radio_choose(app, digit);
        return true;
    }
    if (!menus_any_active(m)) return false;
    if (digit_down) {
        bool ctrl = (e->key.keysym.mod & KMOD_CTRL) != 0;
        apply_menu_action(app, ctrl ? menus_secondary_key(m, digit) : menus_number_key(m, digit));
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
        case SDL_CONTROLLERDEVICEADDED: // opened, so its buttons come as events (SDL closes it as it goes)
            if (SDL_IsGameController(e.cdevice.which)) SDL_GameControllerOpen(e.cdevice.which);
            break;
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

    d->mode = hud_mode(app);
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
    // The vote on, as the server last said, and its box: up until I answer (F12, F11).
    // A kick vote of my own too: the original's StartVote means to send its starter's
    // yes and hide the box, but it reads the vote's kind before it is set, so in play
    // the starter sees the box and presses F12 as everyone does; this does as it plays.
    const MsgVote *v = &app->net.vote;
    bool box = v->kind != VOTE_NONE && !app->vote_hidden;
    d->vote = !box ? HUD_VOTE_NONE : v->kind == VOTE_KICK ? HUD_VOTE_KICK : HUD_VOTE_MAP;
    snprintf(d->vote_target, sizeof d->vote_target, "%s", v->target);
    snprintf(d->vote_starter, sizeof d->vote_starter, "%s", v->starter);
    snprintf(d->vote_reason, sizeof d->vote_reason, "%s", v->reason);
    snprintf(d->hostname, sizeof(d->hostname), "%s", client_net_joined(&app->net) ? app->net.hostname : "SoldatReloaded");
    // the map window's offer: the server's last answer (GameMenus.pas shows VoteMapName,
    // whichever map it was asked for), paged within the count it gave; the maps here
    // only with no server to ask
    if (client_net_joined(&app->net)) {
        const MsgMapReply *r = &app->net.map_reply;
        app->menus.map_count = r->count;
        if (r->count > 0) app->menus.map_index = clampi(app->menus.map_index, 0, r->count - 1);
        snprintf(d->map_offered, sizeof d->map_offered, "%s", r->map);
    } else if (app->map_count > 0) {
        app->menus.map_count = app->map_count;
        app->menus.map_index = clampi(app->menus.map_index, 0, app->map_count - 1);
        snprintf(d->map_offered, sizeof d->map_offered, "%s", app->maps[app->menus.map_index]);
    } else {
        app->menus.map_count = 1;
        snprintf(d->map_offered, sizeof d->map_offered, "%s", app->map->value);
    }
    // the kick window's: who is on, and which is me
    for (int i = 0; i < MAX_PLAYERS; i++) app->menus.players_active[i] = g->world.soldiers[i].active;
    app->menus.me = app->me;
    d->kill_limit = g->match.settings.score_limit;
    d->time_left_min = g->match.time_left / TICK_RATE / 60;
    d->time_left_sec = g->match.time_left / TICK_RATE % 60;
    for (int t = 0; t < HUD_TEAMS && t < TEAM_COUNT; t++) d->team_kills[t] = g->match.scores[t];
    d->paused = g->match.state == MATCH_PAUSED;
    d->round_over = g->match.state == MATCH_ENDED && g->match.counter > 0;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Soldier *s = &g->world.soldiers[i];
        HudPlayer *p = &d->players[i];
        p->active = s->active;
        if (!s->active) continue;
        player_name(app, i, p->name, sizeof p->name);
        p->team = s->team;
        p->dead = s->dead;
        p->holding_flag = s->held && thing_is_flag(g->world.things[s->held - 1].style);
        p->bot = s->bot;
        // the team's shirt in a team game, as the original's ShirtColor is then (and the roster's colour)
        p->shirt = d->team_game && s->team != TEAM_SPECTATOR ? team_shirt(s->team) : s->look.shirt;
        p->kills = s->kills;
        p->deaths = s->deaths;
        p->flags = s->flags;
        p->ping = s->ping;
        p->bot = s->bot;
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
    // the demo being recorded, or played
    d->recording = demo_recording(&app->recorder);
    snprintf(d->demo_name, sizeof d->demo_name, "%s", d->recording ? app->recorder.name : "");
    d->demo_playing = app->playing;
    d->demo_tick = demo_at(app);
    d->demo_seeking = app->seeking;
    d->demo_ticks = app->player.header.ticks;
    d->demo_paused = app->demo_paused;
    d->demo_speed = app->demo_speed->number;
    // the weapons menu's green lines: what the next spawn gets, picked last life or in the
    // config, as the original's SelWeapon and cl_player_secwep
    d->selected_weapon = me->primary_choice;
    d->selected_secondary = me->secondary_choice;
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
    consoles_fill(&app->consoles, d, d->chat_type != HUD_CHAT_NONE, app->console_scroll);
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

    const char *console[] = {"Crow joined the game.", "Mabuse joined the game.", "Welcome to SoldatReloaded"};
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

// How the line did over the last second (cl_netstats): the ping, how many frames the
// view had in hand, the snapshots that came too late or not at all, the view clock's
// nudges, and how far the snapshots moved the others from where stepping had them,
// which is the jitter the picture would show unsmoothed.
static void report_net(App *app)
{
    if (!client_net_joined(&app->net) || app->netstats->integer == 0) return;
    const ClientStream *c = &app->net.stream;
    const World *w = &app->game->world;
    uint32_t applies = c->applies - app->net_seen.applies;
    float corrected = applies ? (c->correction - app->net_seen.correction) / (float)applies : 0.0f;
    console_print_color(app->console, HUD_COLOR_CLIENT,
                        "net: ping %u ms, %d frames in hand (interp %d), late %u, missed %u, held %u, skipped %u, resync %u, corrected %.2f units a frame\n",
                        w->soldiers[app->me].ping, (int32_t)(c->newest - w->tick), c->interp, c->late - app->net_seen.late,
                        c->misses - app->net_seen.misses, c->held - app->net_seen.held, c->skipped - app->net_seen.skipped,
                        c->resyncs - app->net_seen.resyncs, corrected);
    app->net_seen.late = c->late;
    app->net_seen.misses = c->misses;
    app->net_seen.held = c->held;
    app->net_seen.skipped = c->skipped;
    app->net_seen.resyncs = c->resyncs;
    app->net_seen.applies = c->applies;
    app->net_seen.correction = c->correction;
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
    report_net(app);
}

// The fonts, the minimap and the menus, sized to the window; again whenever it changes.
static void interface_open(App *app)
{
    Rect r = window_rect(app);
    if (!fonts_load(app->assets->value, r.height)) fprintf(stderr, "no fonts: the HUD draws without text\n");
    map_view_build_minimap(&app->render.map_view, r.height);
    menus_init(&app->menus, GAME_HEIGHT * r.width / r.height, &app->game->ctx.weapons);
    menus_mouse_move(&app->menus, app->input.cursor); // the cursor stays where it was
    app->menus.rope = app->game->world.rules.rope; // the boots offer what this game allows (sv_rope)
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
    app->was_watching = false;
    app->seen_life = -1;
    app->team_asked = false;
    app->camera_follow = -1;
    app->free_camera = false;
    // the scoreboard the round's end put up, and the stats, go with the old round (the
    // original's map change: FragsMenuShow and StatsMenuShow off)
    app->hud_data.frags_menu = false;
    app->hud_data.stats_menu = false;
    return true;
}

// What the line's messages began, taken: after each frame's poll, and as a demo plays at
// each of its frames' ends.
static void net_take(App *app)
{
    if (client_net_take_map(&app->net)) {
        // a round on the server's map: the world made anew for its snapshots, my slot its
        if (!world_reload(app, app->net.map)) {
            fprintf(stderr, "could not load the server's map '%s'\n", app->net.map);
            app->quit = true;
        }
        app->me = app->net.slot;
        app->round_recorded = false;
    }
    // The round is over (ClientHandleMapChange): the scoreboard comes up and stays
    // through the countdown, the weapons menu and the stats go, and with no teams the
    // camera goes to the winner, the cursor to the middle.
    if (client_net_take_map_change(&app->net)) {
        app->hud_data.frags_menu = true;
        app->hud_data.stats_menu = false;
        menus_show(&app->menus, MENU_LIMBO, false, hud_mode(app), 1);
        if (!team_game(app)) {
            int best = -1;
            for (int i = 0; i < MAX_PLAYERS; i++) {
                const Soldier *s = &app->game->world.soldiers[i];
                if (s->active && s->team != TEAM_SPECTATOR && (best < 0 || s->kills > app->game->world.soldiers[best].kills)) best = i;
            }
            if (best >= 0 && best != app->me) app->camera_follow = best;
            if (!app->menus.menus[MENU_ESC].active) app->input.cursor = vec2_scale(app->input.view, 0.5f);
        }
    }
    // a vote begun: its box comes up (ClientHandleVoteOn), the stats go
    if (app->net.vote_seq != app->vote_seen) {
        app->vote_seen = app->net.vote_seq;
        app->vote_hidden = false;
        app->hud_data.stats_menu = false;
    }
    MsgChat heard;
    while (client_net_take_chat(&app->net, &heard)) chat_heard(app, heard.slot, heard.team, (ChatKind)heard.kind, heard.color, heard.text);
}

int main(int argc, char *argv[])
{
    App app = {0};

    if (!client_net_init(&app.net)) fprintf(stderr, "ENet wouldn't start: no connecting\n");
    app.net.tap = demo_tap; // what the line brings, into the demo being recorded
    app.net.tap_user = &app;
    browser_init(&app.browser);
    http_init();
    http_set_agent("soldatreloaded/" SOLDATRELOADED_VERSION);
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

    scale_data_load(&app.scales, app.assets->value); // the scales the interface loads with
    render_init(&app.render, app.assets->value, &app.game->ctx);
    audio_init(&app.audio, app.assets->value);
    interface_load(&app.hud, app.assets->value, &app.scales);
    interface_open(&app);
    if (app.hud_demo->integer == 2) {
        app.menus.gear = app.game->world.soldiers[app.me].gear;
        menus_show(&app.menus, MENU_LIMBO, true, HUD_MODE_CTF, 1);
    }
    if (app.hud_demo->integer == 3) {
        menus_show(&app.menus, MENU_ESC, true, HUD_MODE_CTF, 1);
        app.menus.noob_show = true;
    }

    snapshot_tick(&app);
    snapshot_tick(&app); // both snapshots start as the world before the first tick
    app.camera = (GameCamera){.pos = app.game->world.soldiers[app.me].pos, .viewport = window_rect(&app)};
    input_start(&app.input, view_size(&app));
    mainmenu_show(&app.mainmenu, app.net.state == CLIENT_NET_OFF); // unless the command line is already connecting
    if (app.mainmenu.shown && app.menu_page->integer > 0) mainmenu_open_page(&app.mainmenu, (MainPage)clampi(app.menu_page->integer, 0, MAIN_PAGE_COUNT - 1));
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
        // the game hosted here ticks first, so its snapshot of this frame is on the line
        // before the client listens; a map it can't load ends it
        if (app.host && !host_pump(app.host, dt)) {
            console_print_color(app.console, HUD_COLOR_WARNING, "the hosted game could not go on\n");
            console_execute(app.console, "disconnect");
        }
        browser_pump(&app.browser, app.time); // the server list, while the menu asks for it
        if (app.play_name[0]) { // its first frames, up to its first tick, at once: its world made
            demo_start_playback(&app);
            if (app.playing && !demo_feed(&app)) demo_stop_playback(&app);
        }
        client_net_poll(&app.net, app.console, app.game);
        net_take(&app);
        // A demo: begun as `record` asked, or by demo_autorecord once a round; stopped
        // when the line is lost; the frame's packets marked as all in, before its ticks.
        if (!app.playing && client_net_joined(&app.net) && app.net.round && !demo_recording(&app.recorder) &&
            (app.record_asked || (app.demo_autorecord->integer && !app.round_recorded))) {
            demo_start_recording(&app, app.record_asked ? app.record_name : NULL);
            app.record_asked = false;
            app.round_recorded = true;
        }
        if (demo_recording(&app.recorder) && !client_net_joined(&app.net)) demo_stop_recording(&app);
        demo_record_frame(&app.recorder);
        // the map window asks the server for the map it shows, as it opens and as it pages
        {
            bool open = app.menus.menus[MENU_MAP].active && client_net_joined(&app.net);
            if (open && (!app.map_window_open || app.menus.map_index != app.map_query_index)) {
                app.map_query_index = app.menus.map_index;
                client_net_map_query(&app.net, app.map_query_index);
            }
            if (!open) app.map_query_index = -1;
            app.map_window_open = open;
        }
        // the menu goes as a server takes us, and comes back when the line is lost
        if (app.net.state != app.net_state_seen) {
            if (app.net.state == CLIENT_NET_JOINED) mainmenu_show(&app.mainmenu, false);
            else if (app.net.state == CLIENT_NET_OFF && app.net_state_seen == CLIENT_NET_JOINED) mainmenu_show(&app.mainmenu, true);
            app.net_state_seen = app.net.state;
        }
        apply_cvars(&app);
        app.camera.viewport = window_rect(&app);
        input_sample(&app.input, screen_to_world(&app.camera, cursor(&app)));

        // A demo plays at demo_speed, held while paused or while the escape menu is up (the
        // original's); each tick runs on the demo's records up to it. A seek runs its ticks
        // as fast as they go, a slice of the frame at a time so the window keeps answering;
        // then the demo goes on from there at its pace.
        if (app.playing && app.seeking) {
            Uint64 began = SDL_GetPerformanceCounter(), budget = SDL_GetPerformanceFrequency() / 40;
            while (app.seeking && SDL_GetPerformanceCounter() - began < budget) {
                if (!app.demo_ticked && !demo_feed(&app)) {
                    demo_stop_playback(&app);
                    break;
                }
                tick(&app);
                app.demo_ticked = false;
                if (demo_at(&app) >= app.seek_to) app.seeking = false;
            }
            app.accumulator = 0;
        }
        double speed = 1.0;
        if (app.playing) speed = app.demo_paused || app.menus.menus[MENU_ESC].active ? 0.0 : clampf(app.demo_speed->number, 0.0f, 10.0f);
        if (app.seeking) speed = 0.0; // the seek has the ticks
        int ticks = ticks_owed(&app, dt * speed);
        for (int i = 0; i < ticks; i++) {
            if (app.playing && !app.demo_ticked && !demo_feed(&app)) {
                demo_stop_playback(&app);
                break;
            }
            tick(&app);
            app.demo_ticked = false;
        }
        client_net_flush(&app.net); // what the ticks said goes out now, not a tick late

        // the world ticks every pass; a frame is drawn only once the last is old enough
        since_frame += dt;
        double frame_min = app.fpslimit->integer ? 1.0 / clampi(app.maxfps->integer, MAXFPS_MIN, MAXFPS_MAX) : 0.0;
        if (since_frame >= frame_min) {
            float alpha = (float)(app.accumulator / TICK_SECONDS); // how far into the next tick this frame is
            bool online = client_net_joined(&app.net);
            if (online) client_stream_smooth(&app.net.stream, (float)since_frame, app.smooth->number / 1000.0f);
            build_render_state(&app.frame, &app.game->ctx, &app.previous, &app.latest, alpha, app.me,
                               team_game(&app), online ? app.net.stream.blend : NULL);
            Vec2 target = app.frame.focus;
            if (app.camera_follow >= 0 && app.frame.soldiers[app.camera_follow].active) target = app.frame.soldiers[app.camera_follow].pos;
            const RenderSoldier *watched = &app.frame.soldiers[app.camera_follow >= 0 ? app.camera_follow : app.me];
            // the cursor, drawn and led by the camera: a demo's recorder's own, while the
            // camera is on the recorder and no menu wants mine
            Vec2 shown_cursor = app.input.cursor;
            if (app.playing && app.camera_follow < 0 && !app.free_camera && !menus_any_active(&app.menus)) shown_cursor = app.demo_tick.cursor;
            Vec2 lead = vec2_scale(shown_cursor, app.camera.viewport.height / GAME_HEIGHT);
            const Bullet *tracked = app.tracking ? my_shot(&app, app.tracking_shot) : NULL;
            if (tracked) { // ahead of my scoped shot, where it is drawn, by five ticks of its flight
                Vec2 at = vec2_add(tracked->old_pos, vec2_scale(vec2_sub(tracked->pos, tracked->old_pos), alpha));
                app.camera.pos = vec2_add(at, vec2_scale(tracked->vel, 5.0f));
            } else if (!app.free_camera) {
                camera_follow(&app.camera, target, lead, watched->aim_dist, since_frame);
            } else { // the cursor pushes the free camera, per frame at the tick's rate so it glides
                Vec2 off = vec2_sub(app.input.cursor, vec2_scale(app.input.view, 0.5f));
                if (fabsf(off.x) > 10.0f || fabsf(off.y) > 10.0f) {
                    app.camera.pos = vec2_add(app.camera.pos, vec2_scale(off, (float)since_frame * TICK_RATE / SPECTATORAIMDIST));
                }
            }

            gfx_viewport(0, 0, (int)app.camera.viewport.width, (int)app.camera.viewport.height);
            if (!app.mainmenu.shown) {
                render_draw(&app.render, &app.frame, &app.camera, app.render_options, grenade_color(app.grenade_color), app.time);
                hud_data_build(&app);
                interface_draw(&app.hud, &app.hud_data, &app.menus, &app.frame, &app.game->ctx, &app.render.map_view,
                               &app.camera, shown_cursor, app.camera.viewport, cvar_color(app.cursor_color),
                               cvar_color(app.crosshair_color), clampi(app.cursor_size->integer, 50, 200) / 100.0f,
                               clampi(app.crosshair_size->integer, 50, 200) / 100.0f);
            } else { // the menu on a background of its own
                gfx_clear((Rgba){0, 0, 0, 255});
                Rect r = app.camera.viewport;
                bool demos = app.mainmenu.page == MAIN_DEMOS; // demos/ read as its page opens, so a new one shows
                if (demos && !app.demos_shown) app.demo_count = demo_list(app.demos, (int)(sizeof app.demos / sizeof app.demos[0]));
                app.demos_shown = demos;
                mainmenu_draw(&app.mainmenu, app.console, &app.hud, &app.render.gostek, &app.game->ctx,
                              app.input.cursor, GAME_HEIGHT * r.width / r.height, GAME_HEIGHT / r.height, app.time,
                              console_log_line(app.console, 0), client_net_joined(&app.net), app.host != NULL, app.maps, app.map_count, &app.browser,
                              app.demos, app.demo_count);
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

    demo_stop_recording(&app);
    demo_play_close(&app.player);
    if (app.host) {
        client_net_disconnect(&app.net, app.console);
        host_stop(&app);
    }
    browser_close(&app.browser);
    http_cleanup();
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
