// The client. Reads as what it is: each subsystem opened, the loop, each closed.
//
//   game    the world (shared/game), for now ticked here with authority: a local
//           sandbox with one soldier, until the connection to a server is ported
//   input   the keys and mouse (input/)
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

#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/game.h"
#include "game/systems/systems.h"
#include "input/input.h"
#include "render/render.h"

#define ME 0
#define MAX_FRAME 0.25 // a stall never turns into a burst of ticks

typedef struct Settings {
    const char *base; // the opensoldat base assets: maps/, anims/, objects/, textures/...
    const char *map;
} Settings;

typedef struct App {
    Settings settings;
    Game *game; // large; on the heap
    Input input;
    uint32_t seq; // my commands, numbered
    double accumulator;

    // The two ticks each frame is drawn between, and the frame built from them.
    TickSnapshot previous, latest;
    RenderState frame;

    GameCamera camera;
    Render render;
    RenderOptions render_options;
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

static Vec2 cursor(void)
{
    Vector2 m = GetMousePosition();
    return (Vec2){m.x, m.y};
}

// This frame's keys: the view's own, then the soldier's.
static void sample_input(App *app)
{
    if (IsKeyPressed(KEY_TAB)) app->render_options.wireframe = !app->render_options.wireframe;
    if (IsKeyPressed(KEY_F3)) app->render_options.debug = !app->render_options.debug;
    if (IsKeyPressed(KEY_F4)) {
        if (IsWindowState(FLAG_VSYNC_HINT)) ClearWindowState(FLAG_VSYNC_HINT);
        else SetWindowState(FLAG_VSYNC_HINT);
    }
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) camera_zoom_at(&app->camera, wheel > 0 ? 1.15f : 1.0f / 1.15f, cursor());

    input_sample(&app->input, screen_to_world(&app->camera, cursor()));
}

// What the HUD will show, for now a line of text.
static void draw_status(const App *app)
{
    const Soldier *me = &app->game->world.soldiers[ME];
    DrawText(TextFormat("%s   health %.0f   jets %d/%d   %d fps%s", app->settings.map, me->health, me->jets,
                        app->game->ctx.map->start_jet, GetFPS(), IsWindowState(FLAG_VSYNC_HINT) ? " (vsync)" : ""),
             10, 10, 20, RAYWHITE);
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

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE); // vsync off for now: F4 turns it on
    InitWindow(1280, 960, "csoldat");

    render_init(&app.render, app.settings.base, &app.game->ctx);
    snapshot_tick(&app);
    snapshot_tick(&app); // both snapshots start as the world before the first tick
    app.camera = (GameCamera){.pos = app.game->world.soldiers[ME].pos, .zoom = 1.0f};

    while (!WindowShouldClose()) {
        double dt = GetFrameTime();
        sample_input(&app);

        int ticks = ticks_owed(&app, dt);
        for (int i = 0; i < ticks; i++) tick(&app);

        float alpha = (float)(app.accumulator / TICK_SECONDS); // how far into the next tick this frame is
        build_render_state(&app.frame, &app.game->ctx, &app.previous, &app.latest, alpha, ME);
        camera_follow(&app.camera, app.frame.focus, cursor(), dt);

        BeginDrawing();
        render_draw(&app.render, &app.frame, &app.camera, app.render_options);
        draw_status(&app);
        EndDrawing();
    }

    render_destroy(&app.render);
    CloseWindow();
    game_close(&app);
    return 0;
}
