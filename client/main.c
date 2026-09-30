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
// App; nothing else is global. On the way out the binds and archived cvars are saved to
// config.cfg.
//
//   client [+assets <opensoldat base dir>] [+map <name>] [+<cvar> <value>] [+<command> <args>...]
//
// so `client +map ctf_Ash +r_screenwidth 1920 +r_screenheight 1080`, or `+hud_demo 2`
// for the HUD full of sample data, or `+screenshot out.png` for a PNG of the 60th frame.
//
// The view's default binds are the config's: Escape the menu, Tab the weapons, M the
// teams, F1 the scoreboard, F2 the weapon stats, F3 the minimap (ui_minimap), F5 the
// FPS line (ui_info), F7 the names (ui_playernames). F9 toggles the wireframe
// (r_wireframe), F10 the debug overlay (r_debug), F4 vsync (r_swapeffect, off as the
// original's default); the wheel zooms (zoomin, zoomout).

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
#include "render/interface.h"
#include "render/render.h"
#include "render/scale_data.h"
#include "ui/menus.h"

#define ME 0
#define MAX_FRAME 0.25 // a stall never turns into a burst of ticks
#define ZOOM_STEP 1.15f
#define CONFIG "config.cfg"
#define SCREENSHOT_FRAME 60

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
    "bind mwheelup zoomin; bind mwheeldown zoomout";

typedef struct App {
    Console *console; // large; on the heap
    Cvar *assets;     // the opensoldat base assets: maps/, anims/, objects/, textures/...
    Cvar *map;
    Cvar *width, *height; // the window
    Cvar *swapeffect;     // vsync
    Cvar *sensitivity;
    Cvar *wireframe, *debug;
    Cvar *minimap, *info, *player_names, *console_length;
    Cvar *hud_demo;       // the HUD full of sample data, to see every part of it: page 1, 2 or 3
    char screenshot[512]; // a PNG of the 60th frame, then quit

    Game *game; // large; on the heap
    SDL_Window *window;
    Input input;
    uint32_t seq; // my commands, numbered
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

// zoomin / zoomout: at the cursor.
static void cmd_zoom(Console *con, int argc, char **argv, void *user)
{
    (void)con, (void)argc;
    App *app = user;
    float scale = app->camera.viewport.height / GAME_HEIGHT;
    camera_zoom_at(&app->camera, strcmp(argv[0], "zoomin") == 0 ? ZOOM_STEP : 1.0f / ZOOM_STEP,
                   vec2_scale(app->input.cursor, scale));
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
    else if (strcmp(name, "weaponsmenu") == 0) menus_show(m, MENU_LIMBO, !m->menus[MENU_LIMBO].active, d->mode, 1);
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
// defaults, what was saved, and the command line over both.
static bool console_open(App *app, int argc, char *argv[])
{
    Console *con = app->console = console_create(print_stdout, NULL);
    if (!con) return false;

    app->assets = cvar_register(con, "assets", "assets", 0, "the opensoldat base assets directory");
    app->map = cvar_register(con, "map", "Arena", 0, "the map to load");
    app->width = cvar_register(con, "r_screenwidth", "1280", CVAR_ARCHIVE, "the window's width");
    app->height = cvar_register(con, "r_screenheight", "960", CVAR_ARCHIVE, "the window's height");
    app->swapeffect = cvar_register(con, "r_swapeffect", "0", CVAR_ARCHIVE, "wait for the display's refresh (vsync)");
    app->sensitivity = cvar_register(con, "cl_sensitivity", "1", CVAR_ARCHIVE, "the mouse's speed");
    app->wireframe = cvar_register(con, "r_wireframe", "0", 0, "draw the map's polygons as lines");
    app->debug = cvar_register(con, "r_debug", "0", 0, "spawn points, colliders, special polys, bones");
    app->minimap = cvar_register(con, "ui_minimap", "0", CVAR_ARCHIVE, "the minimap");
    app->info = cvar_register(con, "ui_info", "0", CVAR_ARCHIVE, "the FPS and ping line");
    app->player_names = cvar_register(con, "ui_playernames", "1", CVAR_ARCHIVE, "the names over the players");
    app->console_length =
        cvar_register(con, "ui_console_length", "6", CVAR_ARCHIVE, "how many console lines the HUD shows");
    app->hud_demo = cvar_register(con, "hud_demo", "0", 0, "fill the HUD with sample data: page 1, 2 or 3");
    console_add_command(con, "quit", cmd_quit, app, "leave the game");
    console_add_command(con, "zoomin", cmd_zoom, app, "zoom in at the cursor");
    console_add_command(con, "zoomout", cmd_zoom, app, "zoom out at the cursor");
    console_add_command(con, "screenshot", cmd_screenshot, app, "write the 60th frame from now to a PNG, then quit");
    console_add_command(con, "escmenu", cmd_menu, app, "the escape menu");
    console_add_command(con, "weaponsmenu", cmd_menu, app, "the weapons menu");
    console_add_command(con, "teammenu", cmd_menu, app, "the team menu");
    console_add_command(con, "fragsmenu", cmd_menu, app, "the scoreboard");
    console_add_command(con, "statsmenu", cmd_menu, app, "the weapon stats");
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

// The cvars the loop reads each frame; vsync only once it changes, as it costs a call.
static void apply_cvars(App *app)
{
    if (app->swapeffect->modified) {
        gfx_vsync(app->swapeffect->integer != 0);
        app->swapeffect->modified = false;
    }
    app->input.sensitivity = app->sensitivity->number;
    app->render_options.wireframe = app->wireframe->integer != 0;
    app->render_options.debug = app->debug->integer != 0;
}

// The world, with me in it.
static bool game_open(App *app)
{
    app->game = calloc(1, sizeof(Game));
    if (!app->game || !context_load(&app->game->ctx, app->assets->value, app->map->value)) return false;

    Game *g = app->game;
    game_init(g, 1, match_default_settings());
    g->world.authority = true;

    Soldier *me = &g->world.soldiers[ME];
    Vec2 at = spawn_point(g->ctx.map, TEAM_ALPHA, &g->world.rng);
    soldier_spawn(&g->ctx, me, at, TEAM_ALPHA, WEAPON_AK74, WEAPON_COLT);
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
    app->window = SDL_CreateWindow("csoldat", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, app->width->integer,
                                   app->height->integer, SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!app->window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    if (!gfx_init(app->window)) return false;
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
static void snapshot_tick(App *app)
{
    app->previous = app->latest;
    tick_snapshot_capture(&app->latest, &app->game->world);
}

// One tick of the game on this frame's input.
static void tick(App *app)
{
    Command cmds[MAX_PLAYERS] = {0};
    cmds[ME] = input_command(&app->input, ++app->seq);
    game_tick(app->game, cmds);
    input_clear(&app->input);
    snapshot_tick(app);
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
    Soldier *me = &app->game->world.soldiers[ME];
    switch (action.kind) {
    case MENU_ACTION_QUIT: app->quit = true; break; // the main menu, when there is one
    case MENU_ACTION_OPEN_TEAM_MENU:
        menus_show(&app->menus, MENU_TEAM, true, app->hud_data.mode, 1);
        break;
    case MENU_ACTION_PICK_PRIMARY:
        app->hud_data.selected_weapon = (WeaponId)action.value;
        me->primary_choice = (WeaponId)action.value;
        if (!me->dead) me->weapon = weapon_state(&app->game->ctx, (WeaponId)action.value);
        break;
    case MENU_ACTION_PICK_SECONDARY:
        app->hud_data.selected_secondary = (WeaponId)action.value;
        me->secondary_choice = (WeaponId)action.value;
        if (!me->dead) me->secondary = weapon_state(&app->game->ctx, (WeaponId)action.value);
        break;
    case MENU_ACTION_PICK_TEAM: // a change of team goes to the server, once there is one
    case MENU_ACTION_KICK:
    case MENU_ACTION_VOTE_MAP:
    default: break;
    }
}

// An open menu takes the keys and clicks the original gives it: a digit chooses, a left
// click picks. True if it took the event.
static bool menu_event(App *app, const SDL_Event *e)
{
    GameMenus *m = &app->menus;
    if (!menus_any_active(m)) return false;
    if (e->type == SDL_KEYDOWN && !e->key.repeat) {
        SDL_Scancode key = e->key.keysym.scancode;
        if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_0) {
            int digit = key == SDL_SCANCODE_0 ? 0 : key - SDL_SCANCODE_1 + 1;
            apply_menu_action(app, menus_number_key(m, digit));
            return true;
        }
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
            if (!menu_event(app, &e)) input_event(app->console, &e);
            break;
        }
    }
}

// What the HUD shows that the frame does not carry. Today a local match of one: the
// map's mode by its name, the match's limits and scores, me on the roster, and the
// newest lines of the console's scrollback. The kill console, the chat, the big messages
// and the pings stay empty until what feeds them is ported.
static void hud_data_build(App *app)
{
    HudData *d = &app->hud_data;
    const Game *g = app->game;
    const Soldier *me = &g->world.soldiers[ME];

    d->mode = strncmp(app->map->value, "ctf_", 4) == 0 ? HUD_MODE_CTF : HUD_MODE_DEATHMATCH;
    d->team_game = d->mode == HUD_MODE_CTF;
    snprintf(d->hostname, sizeof(d->hostname), "csoldat");
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
        if (!p->name[0]) snprintf(p->name, sizeof(p->name), "Player %d", i + 1);
        p->team = s->team;
        p->dead = s->dead;
        p->holding_flag = s->holding_flag;
        p->shirt = (Rgba){199, 56, 51, 255}; // the gostek's, until players carry colours
    }
    d->me = ME;
    d->camera_follow = -1;
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

    // the console's newest lines, oldest first; they don't fade yet as the original's do
    int lines = clampi(app->console_length->integer, 0, HUD_CONSOLE_LINES);
    d->console_count = 0;
    for (int back = lines - 1; back >= 0; back--) {
        const char *line = console_log_line(app->console, back);
        if (!line) continue;
        HudLine *l = &d->console[d->console_count++];
        snprintf(l->text, sizeof l->text, "%s", line);
        l->color = (Rgba){0xC3, 0xC3, 0xC3, 0xF1};
    }
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

    const char *console[] = {"Crow joined the game.", "Mabuse joined the game.", "Welcome to csoldat"};
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

int main(int argc, char *argv[])
{
    App app = {0};

    if (!console_open(&app, argc, argv)) return 1;
    if (!game_open(&app)) {
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
    app.camera = (GameCamera){.pos = app.game->world.soldiers[ME].pos, .zoom = 1.0f, .viewport = window_rect(&app)};
    input_start(&app.input, view_size(&app));

    Uint64 last = SDL_GetPerformanceCounter();
    double since_frame = 0; // the time the frame being drawn covers
    int frames_drawn = 0;
    while (!app.quit) {
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / (double)SDL_GetPerformanceFrequency();
        last = now;
        app.time += dt;

        poll_events(&app);
        apply_cvars(&app);
        app.camera.viewport = window_rect(&app);
        input_sample(&app.input, screen_to_world(&app.camera, cursor(&app)));

        int ticks = ticks_owed(&app, dt);
        for (int i = 0; i < ticks; i++) tick(&app);

        // the world ticks every pass; a frame is drawn only once the last is old enough
        since_frame += dt;
        if (since_frame >= MIN_FRAME_SECONDS) {
            float alpha = (float)(app.accumulator / TICK_SECONDS); // how far into the next tick this frame is
            build_render_state(&app.frame, &app.game->ctx, &app.previous, &app.latest, alpha, ME);
            camera_follow(&app.camera, app.frame.focus, cursor(&app), since_frame);

            gfx_viewport(0, 0, (int)app.camera.viewport.width, (int)app.camera.viewport.height);
            render_draw(&app.render, &app.frame, &app.camera, app.render_options);
            hud_data_build(&app);
            interface_draw(&app.hud, &app.hud_data, &app.menus, &app.frame, &app.game->ctx, &app.render.map_view,
                           &app.camera, app.input.cursor, app.camera.viewport);
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

    fonts_unload();
    interface_unload(&app.hud);
    render_destroy(&app.render);
    window_close(&app);
    game_close(&app);
    console_close(&app);
    return 0;
}
