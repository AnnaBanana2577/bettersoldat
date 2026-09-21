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
//   client [-assets <opensoldat base dir>] [-map <name>] [-size <width>x<height>]
//
// Tab toggles the wireframe, F3 the debug overlay, F4 vsync (off, as the original's
// default), F5 the FPS line, the wheel zooms, Escape quits.

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
            case SDL_SCANCODE_F5: app->hud.show_info = !app->hud.show_info; break;
            case SDL_SCANCODE_F4:
                app->vsync = !app->vsync;
                gfx_vsync(app->vsync);
                break;
            default: break;
            }
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
    App app = {.settings = settings_parse(argc, argv)};

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
    while (!app.quit) {
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / (double)SDL_GetPerformanceFrequency();
        last = now;

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
            interface_draw(&app.hud, &app.frame.soldiers[ME], &app.game->ctx, app.input.cursor, app.fps,
                           app.camera.viewport);
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
