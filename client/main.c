// The client. Reads as what it is: each subsystem opened, the loop, each closed.
//
//   game    the world (shared/game), for now ticked here with authority: a local
//           sandbox with one soldier, until the connection to a server is ported
//   input   the keys and mouse (input/)
//   gfx     the window's GL context and everything drawn into it (gfx/)
//   render  the world's picture: camera, map, soldiers (render/)
//
// Each tick: the game's tick on this frame's input, and a snapshot of it. Each frame: a
// RenderState built between the last two snapshots (render/render_state.h), the camera
// following me in it, and the world drawn from it. Everything the client is lives in
// App; nothing else is global.
//
//   client [-assets <opensoldat base dir>] [-map <name>]
//
// Tab toggles the wireframe, F3 the debug overlay, F4 vsync (off for now, to measure
// the frame rate), the wheel zooms, Escape quits.

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/game.h"
#include "game/systems/systems.h"
#include "gfx/gfx.h"
#include "input/input.h"
#include "render/render.h"

#define ME 0
#define MAX_FRAME 0.25 // a stall never turns into a burst of ticks
#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 960
#define CURSOR_IMAGE_SCALE 10.0f

// The original's frame pacing, its defaults: vsync off (r_swapeffect 0), frames no closer
// than 1/500 s (r_fpslimit, r_maxfps), and a millisecond's sleep after each so the loop
// never spins flat out (r_sleeptime).
#define MIN_FRAME_SECONDS (1.0 / 500.0)
#define SLEEP_AFTER_FRAME_MS 1

typedef struct Settings {
    const char *base; // the opensoldat base assets: maps/, anims/, objects/, textures/...
    const char *map;
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
    GfxTexture cursor; // the original's interface-gfx/cursor.png, drawn where the game's cursor is

    // the frame rate, counted over each second for the title
    int frames;
    double frame_timer;
} App;

static Settings settings_parse(int argc, char *argv[])
{
    Settings s = {.base = "assets", .map = "Arena"};
    for (int i = 1; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "-assets") == 0) s.base = argv[i + 1];
        else if (strcmp(argv[i], "-map") == 0) s.map = argv[i + 1];
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
    app->window = SDL_CreateWindow("csoldat", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WINDOW_WIDTH,
                                   WINDOW_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) input_resize(&app->input, view_size(app));
            break;
        default: break;
        }
    }
}

// The cursor over everything: the original's crosshair, less the bink and the sniper
// line, which come with the HUD. The interface is drawn for a 480-tall view and scaled
// to the window (r_scaleinterface), and the cursor's image is 10 times too big for it
// (mod.ini's interface-gfx/cursor.png=10; read from there once the HUD is ported).
static void draw_cursor(const App *app)
{
    Rect vp = app->camera.viewport;
    gfx_transform(mat3_ortho(0, vp.width, 0, vp.height));
    if (app->cursor.handle == 0) return;

    Vec2 c = cursor(app);
    float scale = vp.height / GAME_HEIGHT / CURSOR_IMAGE_SCALE;
    float w = (float)app->cursor.width * scale, h = (float)app->cursor.height * scale;
    float x = floorf(c.x - w / 2), y = floorf(c.y - h / 2); // the original's PixelAlign
    GfxVertex v[4] = {
        gfx_vertex(x, y, 0, 0, RGBA_WHITE),
        gfx_vertex(x + w, y, 1, 0, RGBA_WHITE),
        gfx_vertex(x + w, y + h, 1, 1, RGBA_WHITE),
        gfx_vertex(x, y + h, 0, 1, RGBA_WHITE),
    };
    gfx_draw_quad(app->cursor, v);
}

// What the HUD will show, for now in the title once a second: text comes with the fonts.
static void update_title(App *app, double dt)
{
    app->frames++;
    app->frame_timer += dt;
    if (app->frame_timer < 1.0) return;

    const Soldier *me = &app->game->world.soldiers[ME];
    char title[256];
    snprintf(title, sizeof(title), "csoldat   %s   health %.0f   jets %d/%d   %d fps%s", app->settings.map, me->health,
             me->jets, app->game->ctx.map->start_jet, app->frames, app->vsync ? " (vsync)" : "");
    SDL_SetWindowTitle(app->window, title);
    app->frames = 0;
    app->frame_timer = 0;
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
    char path[512];
    snprintf(path, sizeof(path), "%s/interface-gfx/cursor.png", app.settings.base);
    if (!gfx_texture_load(&app.cursor, path, NULL)) fprintf(stderr, "no cursor image at %s\n", path);

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
            draw_cursor(&app);
            gfx_present(app.window);
            update_title(&app, since_frame);
            since_frame = 0;
        }
        SDL_Delay(SLEEP_AFTER_FRAME_MS);
    }

    gfx_texture_delete(&app.cursor);
    render_destroy(&app.render);
    window_close(&app);
    game_close(&app);
    return 0;
}
