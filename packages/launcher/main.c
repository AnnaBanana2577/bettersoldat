// The launcher, what a player starts: Soldat Reloaded.exe on Windows,
// soldatreloaded-launcher on Linux. It brings the install up to the latest release
// (update.h), says how that is going in a small window, and starts the game.
//
//   launcher [--no-update] [--update-only] [--verify] [--releases <url>] [anything for the game...]
//
//   --no-update       start the game as it is
//   --update-only     bring the install up to date and stop there, without the game; the
//                     exit status says whether it worked
//   --verify          hash every file, not only those manifest.txt doesn't vouch for
//   --releases <url>  where the releases are, rather than this repository's on GitHub
//                     (a local copy of the same layout, to try an update on)
//
// Everything else is passed to the game: launcher +map ctf_Ash plays as client +map
// ctf_Ash does. When the releases can't be reached the game starts as it is; when an
// update fails the window says why and waits, to play the version installed or quit.
//
// The work runs on a thread of its own and the window draws what it last said. The
// window opens only if the work takes longer than a glance, so a start with nothing to
// do goes straight to the game. The window is drawn in the main menu's look, with the
// game's own fonts from mods/default/; stb_easy_font stands in when they can't be read.

#include <SDL.h>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function" // what of stb_easy_font goes unused
#pragma GCC diagnostic ignored "-Wmissing-braces"  // and how it initializes its colour
#endif
#include <stb_easy_font.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "http.h"
#include "update.h"

#ifdef _WIN32
#define NOGDI
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define CLIENT_FILE "bin\\client.exe" // xmake.lua's client target, in bin/ beside the launcher
#else
#include <unistd.h>
#define CLIENT_FILE "bin/client"
// the window's icon, which on Windows is the executable's own (data/icon.ico)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#endif

#ifndef SOLDATRELOADED_VERSION
#define SOLDATRELOADED_VERSION "dev"
#endif
#ifndef SOLDATRELOADED_PLATFORM
#define SOLDATRELOADED_PLATFORM "unknown"
#endif
#ifndef SOLDATRELOADED_RELEASES
#define SOLDATRELOADED_RELEASES ""
#endif

#define WINDOW_WIDTH 540
#define WINDOW_HEIGHT 196
#define WINDOW_DELAY_MS 300 // the window opens if the work is still going after this

// What the worker last said, for the window to draw.
typedef struct Shared {
    SDL_mutex *lock;
    UpdateOptions options;
    char phase[256];
    uint64_t done, total;
    bool finished;
    UpdateOutcome outcome;
    char version[MANIFEST_VERSION_SIZE];
    char error[512];
} Shared;

static void on_phase(void *user, const char *text)
{
    Shared *s = user;
    SDL_LockMutex(s->lock);
    snprintf(s->phase, sizeof s->phase, "%s", text);
    s->done = s->total = 0;
    SDL_UnlockMutex(s->lock);
}

static void on_progress(void *user, uint64_t done, uint64_t total)
{
    Shared *s = user;
    SDL_LockMutex(s->lock);
    s->done = done;
    s->total = total;
    SDL_UnlockMutex(s->lock);
}

static int work(void *user)
{
    Shared *s = user;
    UpdateReport report = {.user = s, .phase = on_phase, .progress = on_progress};
    char version[MANIFEST_VERSION_SIZE], error[512];
    UpdateOutcome outcome = update_run(&s->options, &report, version, sizeof version, error, sizeof error);
    SDL_LockMutex(s->lock);
    s->outcome = outcome;
    snprintf(s->version, sizeof s->version, "%s", version);
    snprintf(s->error, sizeof s->error, "%s", error);
    s->finished = true;
    SDL_UnlockMutex(s->lock);
    return 0;
}

// --- text --------------------------------------------------------------------------------

// The game's faces, from mods/default/ beside the launcher (never a player's mod, which
// may be broken), as the main menu sets them: Play
// for what is read, its Bold for emphasis, Black Ops One for the name. Each style is one
// face at one pixel size, its ASCII glyphs baked into a texture the first time it is
// used. A face that can't be read (an install the launcher is about to repair) leaves
// its styles to stb_easy_font, so the window still says what is going on.
typedef enum FaceId { FACE_PLAY, FACE_PLAY_BOLD, FACE_LOGO, FACE_COUNT } FaceId;
static const char *const FACE_FILES[FACE_COUNT] = {"mods/default/play-regular.ttf", "mods/default/play-bold.ttf",
                                                    "mods/default/black-ops-one.ttf"};

typedef struct Face {
    unsigned char *data;
    stbtt_fontinfo info;
} Face;

#define ATLAS 512
#define FIRST_CHAR 32
#define CHAR_COUNT 96

typedef struct Style {
    FaceId face;
    float pixels;       // the size
    float tracking;     // between glyphs, in pixels
    SDL_Texture *atlas; // baked on first use
    stbtt_bakedchar glyphs[CHAR_COUNT];
    float ascent;
} Style;

static Face faces[FACE_COUNT];

static Style S_LOGO = {FACE_LOGO, 34, 1};
static Style S_LOGO_SUB = {FACE_PLAY_BOLD, 11, 0}; // its tracking is worked out to fit under the name
static Style S_BODY = {FACE_PLAY, 15, 0};
static Style S_SMALL = {FACE_PLAY, 13, 0};
static Style S_SMALL_BOLD = {FACE_PLAY_BOLD, 13, 0};

static void fonts_load(void)
{
    for (int i = 0; i < FACE_COUNT; i++) {
        Face *f = &faces[i];
        f->data = (unsigned char *)files_read(FACE_FILES[i], NULL);
        if (f->data && !stbtt_InitFont(&f->info, f->data, stbtt_GetFontOffsetForIndex(f->data, 0))) {
            free(f->data);
            f->data = NULL;
        }
    }
}

static void fonts_unload(void)
{
    Style *styles[] = {&S_LOGO, &S_LOGO_SUB, &S_BODY, &S_SMALL, &S_SMALL_BOLD};
    for (size_t i = 0; i < sizeof styles / sizeof styles[0]; i++) {
        if (styles[i]->atlas) SDL_DestroyTexture(styles[i]->atlas);
        styles[i]->atlas = NULL;
    }
    for (int i = 0; i < FACE_COUNT; i++) {
        free(faces[i].data);
        faces[i].data = NULL;
    }
}

// The style's glyphs baked, if its face loaded: false leaves it to stb_easy_font.
static bool style_ready(SDL_Renderer *r, Style *s)
{
    if (s->atlas) return true;
    const Face *f = &faces[s->face];
    if (!f->data || !r) return false;
    unsigned char *alpha = malloc(ATLAS * ATLAS);
    unsigned char *rgba = malloc((size_t)ATLAS * ATLAS * 4);
    if (!alpha || !rgba || stbtt_BakeFontBitmap(f->data, 0, s->pixels, alpha, ATLAS, ATLAS, FIRST_CHAR, CHAR_COUNT, s->glyphs) <= 0) {
        free(alpha);
        free(rgba);
        return false;
    }
    for (int i = 0; i < ATLAS * ATLAS; i++) {
        rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = alpha[i];
    }
    s->atlas = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, ATLAS, ATLAS);
    if (s->atlas) {
        SDL_UpdateTexture(s->atlas, NULL, rgba, ATLAS * 4);
        SDL_SetTextureBlendMode(s->atlas, SDL_BLENDMODE_BLEND);
        int ascent, descent, gap;
        stbtt_GetFontVMetrics(&f->info, &ascent, &descent, &gap);
        s->ascent = (float)ascent * stbtt_ScaleForPixelHeight(&f->info, s->pixels);
    }
    free(alpha);
    free(rgba);
    return s->atlas != NULL;
}

// stb_easy_font, for a style whose face is missing: its letters scaled to the style's size.
static float easy_scale(const Style *s) { return s->pixels / 13.0f; }

static void easy_text(SDL_Renderer *r, float x, float y, float scale, SDL_Color color, const char *s)
{
    static char quads[64 * 1024];
    static SDL_Vertex vertices[(sizeof quads / 64) * 6];
    unsigned char rgba[4] = {color.r, color.g, color.b, color.a};
    int count = stb_easy_font_print(0, 0, (char *)s, rgba, quads, sizeof quads);
    int n = 0;
    for (int q = 0; q < count; q++) {
        const float *v = (const float *)(quads + q * 64); // 4 vertices of x, y, z, colour
        static const int corners[6] = {0, 1, 2, 0, 2, 3};
        for (int i = 0; i < 6; i++) {
            const float *c = v + corners[i] * 4;
            vertices[n++] = (SDL_Vertex){{x + c[0] * scale, y + c[1] * scale}, color, {0, 0}};
        }
    }
    SDL_RenderGeometry(r, NULL, vertices, n, NULL, 0);
}

// `s` in `style` with its top-left at `x, y`; with no renderer, only measured. The width.
static float text_run(SDL_Renderer *r, Style *style, float x, float y, SDL_Color color, const char *s)
{
    if (!style_ready(r, style)) {
        float scale = easy_scale(style);
        if (r) easy_text(r, x, y + 1, scale, color, s);
        return (float)stb_easy_font_width((char *)s) * scale;
    }
    float pen_x = 0, pen_y = y + style->ascent;
    if (r) {
        SDL_SetTextureColorMod(style->atlas, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(style->atlas, color.a);
    }
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        int c = *p < FIRST_CHAR || *p >= FIRST_CHAR + CHAR_COUNT ? '?' : *p;
        stbtt_aligned_quad q;
        float qx = x + pen_x;
        stbtt_GetBakedQuad(style->glyphs, ATLAS, ATLAS, c - FIRST_CHAR, &qx, &pen_y, &q, 1);
        if (r) {
            SDL_Rect src = {(int)(q.s0 * ATLAS), (int)(q.t0 * ATLAS), (int)((q.s1 - q.s0) * ATLAS + 0.5f),
                            (int)((q.t1 - q.t0) * ATLAS + 0.5f)};
            SDL_FRect dst = {q.x0, q.y0, q.x1 - q.x0, q.y1 - q.y0};
            SDL_RenderCopyF(r, style->atlas, &src, &dst);
        }
        pen_x = qx - x + (p[1] ? style->tracking : 0);
    }
    return pen_x;
}

static void text(SDL_Renderer *r, Style *style, float x, float y, SDL_Color color, const char *s) { text_run(r, style, x, y, color, s); }
static float text_width(Style *style, const char *s) { return text_run(NULL, style, 0, 0, (SDL_Color){0, 0, 0, 0}, s); }

static float line_height(const Style *s) { return s->pixels * 1.3f; }

// `s` in lines no wider than `width`, broken at spaces; the y below the last. With no
// renderer it only measures.
static float wrapped(SDL_Renderer *r, Style *style, float x, float y, float width, SDL_Color color, const char *s)
{
    char line[512];
    while (*s) {
        size_t fit = 0, n = 0;
        while (s[n] && n < sizeof line - 1) {
            memcpy(line, s, n + 1);
            line[n + 1] = '\0';
            if (text_width(style, line) > width) break;
            n++;
            if (s[n] == ' ' || !s[n]) fit = n;
        }
        if (fit == 0) fit = n ? n : 1;
        memcpy(line, s, fit);
        line[fit] = '\0';
        if (r) text(r, style, x, y, color, line);
        y += line_height(style);
        s += fit;
        while (*s == ' ') s++;
    }
    return y;
}

// --- drawing ---------------------------------------------------------------------------

// The main menu's look (client/ui/mainmenu.c): night for the ground, white for what is
// read, and the one accent, ember, for what moves.
static const SDL_Color SURFACE = {14, 17, 25, 255}, TEXT = {236, 239, 244, 255}, MUTED = {146, 155, 172, 255},
                       FAINT = {94, 102, 120, 255}, ACCENT = {232, 80, 30, 255}, WARN = {236, 192, 84, 255},
                       TRACK = {48, 56, 74, 255}, LINE = {255, 255, 255, 14};

#define MARGIN 28
#define HEADER_H 84 // the name and the version, and the rule under them

static void fill(SDL_Renderer *r, float x, float y, float w, float h, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_FRect box = {x, y, w, h};
    SDL_RenderFillRectF(r, &box);
}

// The name, written: SOLDAT in the stencil face, and RELOADED under it in the accent,
// its letters spaced out to the same width.
static void wordmark(SDL_Renderer *r, float x, float y)
{
    float w = text_width(&S_LOGO, "SOLDAT");
    text(r, &S_LOGO, x, y, TEXT, "SOLDAT");
    S_LOGO_SUB.tracking = 0;
    float natural = text_width(&S_LOGO_SUB, "RELOADED");
    S_LOGO_SUB.tracking = natural < w ? (w - natural) / 7.0f : 0; // seven gaps
    text(r, &S_LOGO_SUB, x, y + S_LOGO.pixels + 2, ACCENT, "RELOADED");
}

static void draw(SDL_Renderer *r, const Shared *s, bool waiting)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, SURFACE.r, SURFACE.g, SURFACE.b, 255);
    SDL_RenderClear(r);
    wordmark(r, MARGIN, 22);
    if (s->version[0]) {
        char version[48];
        snprintf(version, sizeof version, "v%s", s->version);
        text(r, &S_SMALL, WINDOW_WIDTH - MARGIN - text_width(&S_SMALL, version), 30, FAINT, version);
    }
    fill(r, MARGIN, HEADER_H, WINDOW_WIDTH - 2 * MARGIN, 1, LINE);

    float y = HEADER_H + 22, w = WINDOW_WIDTH - 2 * MARGIN;
    if (waiting) {
        // what went wrong, or that the launcher is new and the game wants starting again, as
        // a sentence; the window grows to hold all of it
        bool restart = s->outcome == UPDATE_RESTART;
        char error[sizeof s->error];
        snprintf(error, sizeof error, "%s", s->error);
        if (error[0] >= 'a' && error[0] <= 'z') error[0] = (char)(error[0] - 'a' + 'A');
        float bottom = wrapped(NULL, &S_BODY, MARGIN, y, w, restart ? TEXT : WARN, error);
        int height = (int)bottom + 52;
        if (height < WINDOW_HEIGHT) height = WINDOW_HEIGHT;
        int ww, wh;
        SDL_Window *window = SDL_RenderGetWindow(r);
        SDL_GetWindowSize(window, &ww, &wh);
        if (wh != height) SDL_SetWindowSize(window, WINDOW_WIDTH, height);
        wrapped(r, &S_BODY, MARGIN, y, w, restart ? TEXT : WARN, error);
        // the keys, along the bottom: each named in bold, what it does beside it
        float hy = (float)height - MARGIN - S_SMALL.pixels, hx = MARGIN;
        if (!restart && files_exists(CLIENT_FILE)) {
            text(r, &S_SMALL_BOLD, hx, hy, TEXT, "Enter");
            hx += text_width(&S_SMALL_BOLD, "Enter") + 6;
            text(r, &S_SMALL, hx, hy, MUTED, "Play the version installed");
            hx += text_width(&S_SMALL, "Play the version installed") + 28;
        }
        text(r, &S_SMALL_BOLD, hx, hy, TEXT, "Esc");
        hx += text_width(&S_SMALL_BOLD, "Esc") + 6;
        text(r, &S_SMALL, hx, hy, MUTED, restart ? "Close" : "Quit");
    } else {
        text(r, &S_BODY, MARGIN, y, TEXT, s->phase);
        float ty = y + S_BODY.pixels + 18;
        fill(r, MARGIN, ty, w, 4, TRACK);
        if (s->total > 0) {
            double part = (double)s->done / (double)s->total;
            if (part > 1) part = 1;
            fill(r, MARGIN, ty, (float)(w * part), 4, ACCENT);
            char percent[16];
            snprintf(percent, sizeof percent, "%d%%", (int)(part * 100 + 0.5));
            text(r, &S_SMALL, WINDOW_WIDTH - MARGIN - text_width(&S_SMALL, percent), y + 1, MUTED, percent);
        }
    }
    SDL_RenderPresent(r);
}

// --- the game --------------------------------------------------------------------------

// The game, with the arguments the launcher didn't take; it takes over this process
// (Linux) or is left running as this one ends (Windows). False if it couldn't start.
static bool start_game(int argc, char **argv)
{
#ifdef _WIN32
    // One command line, each argument quoted as CommandLineToArgvW reads it back.
    static wchar_t line[32768];
    size_t at = 0;
    const char *first = "\"" CLIENT_FILE "\"";
    for (const char *c = first; *c; c++) line[at++] = (wchar_t)*c;
    for (int i = 0; i < argc; i++) {
        wchar_t arg[4096];
        int n = MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, arg, (int)(sizeof arg / sizeof arg[0]));
        if (n <= 0 || at + (size_t)n * 2 + 4 >= sizeof line / sizeof line[0]) return false;
        line[at++] = L' ';
        line[at++] = L'"';
        int slashes = 0;
        for (const wchar_t *c = arg; *c; c++) {
            if (*c == L'\\') {
                slashes++;
            } else {
                // backslashes before a quote are doubled, and the quote escaped
                for (; *c == L'"' && slashes > 0; slashes--) line[at++] = L'\\';
                if (*c == L'"') line[at++] = L'\\';
                slashes = 0;
            }
            line[at++] = *c;
        }
        for (; slashes > 0; slashes--) line[at++] = L'\\';
        line[at++] = L'"';
    }
    line[at] = L'\0';
    STARTUPINFOW startup = {.cb = sizeof startup};
    PROCESS_INFORMATION process;
    if (!CreateProcessW(NULL, line, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    char **args = calloc((size_t)argc + 2, sizeof *args);
    if (!args) return false;
    args[0] = "./" CLIENT_FILE;
    for (int i = 0; i < argc; i++) args[i + 1] = argv[i];
    execv(args[0], args);
    free(args);
    return false;
#endif
}

#ifndef _WIN32
// data/icon.png, the badge, as the window's icon.
static void set_icon(SDL_Window *window)
{
    int width, height, channels;
    unsigned char *rgba = stbi_load("data/icon.png", &width, &height, &channels, 4);
    if (!rgba) return;
    SDL_Surface *icon = SDL_CreateRGBSurfaceWithFormatFrom(rgba, width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA32);
    if (icon) {
        SDL_SetWindowIcon(window, icon);
        SDL_FreeSurface(icon);
    }
    stbi_image_free(rgba);
}
#endif

int main(int argc, char **argv)
{
    Shared s = {.options = {.releases = SOLDATRELOADED_RELEASES, .platform = SOLDATRELOADED_PLATFORM}};
    bool check = true, update_only = false;
    // the arguments not the launcher's, for the game
    char **rest = calloc((size_t)argc + 1, sizeof *rest);
    int rest_count = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--no-update")) check = false;
        else if (!strcmp(argv[i], "--update-only")) update_only = true;
        else if (!strcmp(argv[i], "--verify")) s.options.thorough = true;
        else if (!strcmp(argv[i], "--releases") && i + 1 < argc) s.options.releases = argv[++i];
        else if (rest) rest[rest_count++] = argv[i];
    }
    // the game's files are beside the launcher, wherever it was started from
    if (!files_enter_own_directory()) fprintf(stderr, "launcher: its own directory can't be found\n");
    char self[MANIFEST_PATH_SIZE]; // what it is called there, so it is brought first (update.h)
    if (files_own_name(self, sizeof self)) s.options.self = self;

    if (!check || !s.options.releases[0]) {
        if (start_game(rest_count, rest)) return 0;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Soldat Reloaded", "The game (" CLIENT_FILE ") can't be started.", NULL);
        return 1;
    }

#ifndef _WIN32
    // the window's class, the game's too, so a taskbar or dock groups them, unless the player
    // has given one
    setenv("SDL_VIDEO_X11_WMCLASS", "soldatreloaded", 0);
    setenv("SDL_VIDEO_WAYLAND_WMCLASS", "soldatreloaded", 0);
#endif
    if (SDL_Init(SDL_INIT_VIDEO) != 0) fprintf(stderr, "launcher: no window: %s\n", SDL_GetError());
    if (!http_init()) fprintf(stderr, "launcher: curl couldn't start\n");
    s.lock = SDL_CreateMutex();
    snprintf(s.phase, sizeof s.phase, "Starting");
    char *installed = files_read(UPDATE_VERSION, NULL); // the version until the work says otherwise
    if (installed) {
        installed[strcspn(installed, "\r\n")] = '\0';
        snprintf(s.version, sizeof s.version, "%s", installed);
        free(installed);
    }
    SDL_Thread *worker = s.lock ? SDL_CreateThread(work, "update", &s) : NULL;
    if (!worker) work(&s); // no thread: the work, then the game, without a window

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    Uint32 started = SDL_GetTicks();
    bool quit = false, play = false, waiting = false;
    int status = 0;
    while (!quit && !play) {
        SDL_LockMutex(s.lock);
        Shared now = s;
        SDL_UnlockMutex(s.lock);
        bool restart = now.outcome == UPDATE_RESTART;
        if (now.finished && update_only) {
            if (now.error[0]) fprintf(stderr, "launcher: %s\n", now.error);
            status = now.outcome == UPDATE_FAILED;
            break;
        }
        if (now.finished && !waiting) {
            if (now.outcome != UPDATE_FAILED && !restart) {
                if (now.error[0]) fprintf(stderr, "launcher: %s\n", now.error); // couldn't check: play on
                play = true;
                break;
            }
            waiting = true; // the window says what went wrong, or to start again, and waits for an answer
        }
        if (!window && (waiting || SDL_GetTicks() - started > WINDOW_DELAY_MS)) {
            window = SDL_CreateWindow("Soldat Reloaded", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH,
                                      WINDOW_HEIGHT, 0);
            if (window) {
#ifndef _WIN32
                set_icon(window); // on Windows SDL gives it the executable's own
#endif
                renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
                if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
                fonts_load();
            } else if (waiting) {
                SDL_ShowSimpleMessageBox(restart ? SDL_MESSAGEBOX_INFORMATION : SDL_MESSAGEBOX_ERROR, "Soldat Reloaded",
                                         now.error, NULL);
                play = !restart && files_exists(CLIENT_FILE);
                break;
            }
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            if (waiting && e.type == SDL_KEYDOWN) {
                SDL_Keycode key = e.key.keysym.sym;
                if (key == SDLK_ESCAPE) quit = true;
                if ((key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) && !restart && files_exists(CLIENT_FILE))
                    play = true;
            }
        }
        if (renderer) draw(renderer, &now, waiting);
        else SDL_Delay(16);
    }

    // Closed part way through an update: the thread is cut off with the process, and
    // the next start finishes what it began (update.h).
    fonts_unload();
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    if (play) {
        if (worker) SDL_WaitThread(worker, NULL);
        http_cleanup();
        SDL_Quit();
        if (start_game(rest_count, rest)) return 0;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Soldat Reloaded", "The game (" CLIENT_FILE ") can't be started.", NULL);
        return 1;
    }
    SDL_Quit();
    return status;
}
