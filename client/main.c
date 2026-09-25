// The client. Reads as what it is: each subsystem opened, the loop, each closed.
//
//   game    the world (shared/game), for now ticked here with authority: a local
//           sandbox with one soldier, until the connection to a server is ported
//   input   the keys and mouse (input/)
//   gfx     the window's GL context and everything drawn into it (gfx/)
//   render  the world's picture: camera, map, soldiers, and the HUD over it (render/)
//
// Each tick: the game's tick on this frame's input, and a snapshot of it. Each frame: a
// RenderState built between the last two snapshots (render/render_state.h), the camera
// following me in it, and the world drawn from it. Everything the client is lives in
// App; nothing else is global.
//
//   client [-assets <opensoldat base dir>] [-map <name>] [-size <width>x<height>] [-hud-demo]
//          [-screenshot <file.png>]
//
// Tab toggles the wireframe, F3 the debug overlay, F4 vsync (off, as the original's
// default), F1 the scoreboard, F5 the FPS line, F7 the names, the wheel zooms, Escape
// quits.

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/game.h"
#include "game/systems/systems.h"
#include "gfx/font.h"
#include "gfx/gfx.h"
#include "input/input.h"
#include "render/interface.h"
#include "render/render.h"
#include "render/scale_data.h"

#define ME 0
#define MAX_FRAME 0.25 // a stall never turns into a burst of ticks
#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 960

// The original's frame pacing, its defaults: vsync off (r_swapeffect 0), frames no closer
// than 1/500 s (r_fpslimit, r_maxfps), and a millisecond's sleep after each so the loop
// never spins flat out (r_sleeptime).
#define MIN_FRAME_SECONDS (1.0 / 500.0)
#define SLEEP_AFTER_FRAME_MS 1

typedef struct Settings {
    const char *base; // the opensoldat base assets: maps/, anims/, objects/, textures/...
    const char *map;
    int width, height; // the window
    bool hud_demo;     // the HUD full of sample data, to see every part of it
    const char *screenshot; // a PNG of the 60th frame, then quit
} Settings;

typedef struct App {
    Settings settings;
    Game *game; // large; on the heap
    SDL_Window *window;
    Input input;
    uint32_t seq; // my commands, numbered
    double accumulator;
    bool quit;
    bool vsync;

    // The two ticks each frame is drawn between, and the frame built from them.
    TickSnapshot previous, latest;
    RenderState frame;

    GameCamera camera;
    Render render;
    RenderOptions render_options;
    ScaleData scales; // mod.ini: how big each image is
    Interface hud;
    HudData hud_data; // what the HUD shows beyond the frame: filled here from what there is
    double time;      // seconds since the start

    // the frame rate, counted over each second for the title
    int frames;
    double frame_timer;
    int fps;
} App;

static Settings settings_parse(int argc, char *argv[])
{
    Settings s = {.base = "assets", .map = "Arena", .width = WINDOW_WIDTH, .height = WINDOW_HEIGHT};
    for (int i = 1; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "-assets") == 0) s.base = argv[i + 1];
        else if (strcmp(argv[i], "-map") == 0) s.map = argv[i + 1];
        else if (strcmp(argv[i], "-hud-demo") == 0) s.hud_demo = true, i--;
        else if (strcmp(argv[i], "-screenshot") == 0) s.screenshot = argv[i + 1];
        else if (strcmp(argv[i], "-size") == 0 && sscanf(argv[i + 1], "%dx%d", &s.width, &s.height) != 2) {
            s.width = WINDOW_WIDTH;
            s.height = WINDOW_HEIGHT;
        }
    }
    return s;
}

// The world, with me in it.
static bool game_open(App *app)
{
    app->game = calloc(1, sizeof(Game));
    if (!app->game || !context_load(&app->game->ctx, app->settings.base, app->settings.map)) return false;

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
    app->window = SDL_CreateWindow("csoldat", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, app->settings.width,
                                   app->settings.height, SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!app->window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    if (!gfx_init(app->window)) return false;
    gfx_vsync(app->vsync);
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
static void hud_data_demo(HudData *d);

// This frame's events: the window's, the view's own keys, the mouse.
static void poll_events(App *app)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: app->quit = true; break;
        case SDL_KEYDOWN:
            if (e.key.repeat) break;
            switch (e.key.keysym.scancode) {
            case SDL_SCANCODE_ESCAPE: app->quit = true; break;
            case SDL_SCANCODE_TAB: app->render_options.wireframe = !app->render_options.wireframe; break;
            case SDL_SCANCODE_F3: app->render_options.debug = !app->render_options.debug; break;
            case SDL_SCANCODE_F1: app->hud_data.frags_menu = true; break;
            case SDL_SCANCODE_F5: app->hud_data.show_info = !app->hud_data.show_info; break;
            case SDL_SCANCODE_F7: app->hud_data.player_names = !app->hud_data.player_names; break;
            case SDL_SCANCODE_F4:
                app->vsync = !app->vsync;
                gfx_vsync(app->vsync);
                break;
            default: break;
            }
            break;
        case SDL_KEYUP:
            if (e.key.keysym.scancode == SDL_SCANCODE_F1) app->hud_data.frags_menu = false;
            break;
        case SDL_MOUSEMOTION:
            if (SDL_GetWindowFlags(app->window) & SDL_WINDOW_INPUT_FOCUS) input_mouse_motion(&app->input, &e.motion);
            break;
        case SDL_MOUSEWHEEL:
            if (e.wheel.y != 0) camera_zoom_at(&app->camera, e.wheel.y > 0 ? 1.15f : 1.0f / 1.15f, cursor(app));
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                input_resize(&app->input, view_size(app));
                interface_open(app);
            }
            break;
        default: break;
        }
    }
}

// What the HUD shows that the frame does not carry. Today a local match of one: the
// map's mode by its name, the match's limits and scores, me on the roster. The consoles,
// the chat, the big messages and the pings stay empty until what feeds them is ported.
static void hud_data_build(App *app)
{
    HudData *d = &app->hud_data;
    const Game *g = app->game;
    const Soldier *me = &g->world.soldiers[ME];

    d->mode = strncmp(app->settings.map, "ctf_", 4) == 0 ? HUD_MODE_CTF : HUD_MODE_DEATHMATCH;
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
    d->respawn_counter = me->respawn_counter;
    d->cease_fire_counter = me->cease_fire_counter;
    d->fps = app->fps;
    d->time = app->time;
    d->tick = (int)g->world.tick;
    if (app->settings.hud_demo) hud_data_demo(d);
}

// Sample data in every part of the HUD, for looking at it before the game fills it.
static void hud_data_demo(HudData *d)
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

// The fonts and the HUD, sized to the window; again whenever its height changes.
static void interface_open(App *app)
{
    Rect r = window_rect(app);
    if (!fonts_load(app->settings.base, r.height)) fprintf(stderr, "no fonts: the HUD draws without text\n");
}

int main(int argc, char *argv[])
{
    App app = {.settings = settings_parse(argc, argv), .hud_data = {.player_names = true}};

    if (!game_open(&app)) {
        fprintf(stderr, "could not load map '%s' from '%s'\nusage: client -assets <dir> -map <name>\n",
                app.settings.map, app.settings.base);
        game_close(&app);
        return 1;
    }
    if (!window_open(&app)) {
        window_close(&app);
        game_close(&app);
        return 1;
    }

    render_init(&app.render, app.settings.base, &app.game->ctx);
    scale_data_load(&app.scales, app.settings.base);
    interface_load(&app.hud, app.settings.base, &app.scales);
    interface_open(&app);

    snapshot_tick(&app);
    snapshot_tick(&app); // both snapshots start as the world before the first tick
    app.camera = (GameCamera){.pos = app.game->world.soldiers[ME].pos, .zoom = 1.0f, .viewport = window_rect(&app)};
    input_init(&app.input, view_size(&app));

    Uint64 last = SDL_GetPerformanceCounter();
    double since_frame = 0; // the time the frame being drawn covers
    int frames_drawn = 0;
    while (!app.quit) {
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / (double)SDL_GetPerformanceFrequency();
        last = now;
        app.time += dt;

        poll_events(&app);
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
            interface_draw(&app.hud, &app.hud_data, &app.frame, &app.game->ctx, &app.camera, app.input.cursor,
                           app.camera.viewport);
            if (app.settings.screenshot && ++frames_drawn == 60) {
                Rect r = app.camera.viewport;
                if (!gfx_save_screen(app.settings.screenshot, (int)r.width, (int)r.height)) {
                    fprintf(stderr, "could not write %s\n", app.settings.screenshot);
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
    return 0;
}
