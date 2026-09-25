#pragma once

// The HUD: what the original's InterfaceGraphics.pas draws over the world while
// playing, with its default interface (LoadDefaultInterfaceData). The interface is laid
// out for a 640x480 screen and drawn in a view 480 units tall whose width follows the
// window (r_scaleinterface): x anchors stretch by GameWidth/640, offsets from them
// don't, and every image is its pixel size over its mod.ini scale.
//
// Everything the HUD shows that isn't in the frame's render state comes through a
// HudData: the match and its players, the consoles, the chat, the big messages. The
// app fills one each frame from whatever it has; the drawing never reads the game.
// Until the server, the scoring and the messages are ported, most of it is empty and
// the parts it feeds stay hidden, as they are in the original with nothing to show.
//
// Not here: the menus (GameMenus.pas), the minimap (a render target), the weapon
// stats (F2), the vote and radio menus.

#include "game/game.h"
#include "gfx/gfx.h"
#include "render/camera.h"
#include "render/render_state.h"
#include "render/scale_data.h"

#define HUD_TEXT 160
#define HUD_NAME 32
#define HUD_CONSOLE_LINES 20   // the original's console length cvars top out here
#define HUD_KILL_LINES 15      // ui_killconsole_length
#define HUD_BIG_MESSAGES 4
#define HUD_TEAMS 5            // none, alpha, bravo, charlie, delta

typedef enum HudGameMode {
    HUD_MODE_DEATHMATCH,
    HUD_MODE_POINTMATCH,
    HUD_MODE_TEAMMATCH,
    HUD_MODE_CTF,
    HUD_MODE_RAMBO,
    HUD_MODE_INF,
    HUD_MODE_HTF,
} HudGameMode;

typedef enum HudChatType { HUD_CHAT_NONE, HUD_CHAT_PUBLIC, HUD_CHAT_TEAM, HUD_CHAT_COMMAND } HudChatType;

typedef struct HudLine {
    char text[HUD_TEXT];
    Rgba color;
} HudLine;

// One kill console entry: the text and the weapon's icon beside it (WEAPON_NONE for a
// line without one, which also skips the gap the icon leaves).
typedef struct HudKillLine {
    char text[HUD_TEXT];
    Rgba color;
    WeaponId weapon;
    bool has_icon;
} HudKillLine;

// A big message: shown while `delay` ticks remain, fading in its last seconds.
typedef struct HudBigMessage {
    char text[HUD_TEXT];
    Rgba color;
    float scale;
    int delay;
    float x, y; // in the interface's units
} HudBigMessage;

typedef struct HudPlayer {
    bool active;
    char name[HUD_NAME];
    Team team;
    bool spectator;
    bool dead;
    bool bot;
    bool holding_flag;
    int kills, deaths, flags;
    int ping;
    Rgba shirt;
    // said above the head
    bool typing;
    char chat[HUD_TEXT];
    int chat_delay; // ticks left
    bool chat_team;
} HudPlayer;

typedef struct HudData {
    // the match
    HudGameMode mode;
    bool team_game;
    char hostname[HUD_TEXT];
    char info[HUD_TEXT];
    int time_left_min, time_left_sec;
    int kill_limit;
    int team_kills[HUD_TEAMS]; // by team
    bool flags_known;          // a CTF match with both flags placed
    bool flag_in_base[HUD_TEAMS];
    bool paused;
    bool survival;
    bool survival_round_over;
    int alive, team_alive[HUD_TEAMS];

    // the players, me among them
    HudPlayer players[MAX_PLAYERS];
    int me;
    int ping;
    int respawn_counter; // ticks until I respawn
    int cease_fire_counter;

    // the text
    HudLine console[HUD_CONSOLE_LINES];
    int console_count;
    HudKillLine kills[HUD_KILL_LINES];
    int kill_count;
    HudBigMessage big[HUD_BIG_MESSAGES];
    int big_count;
    char cursor_text[HUD_NAME]; // the player under the cursor
    bool cursor_friendly;

    // what I am typing
    HudChatType chat_type;
    char chat_text[HUD_TEXT];
    int chat_cursor;
    double chat_changed_at; // seconds, for the caret's blink

    // toggles and clocks
    bool frags_menu;   // F1
    bool show_info;    // F5: the FPS and ping line
    bool player_names; // the original's PlayerNamesShow
    int fps;
    double time; // seconds since the start, for what blinks and bobs
    int tick;    // the main tick counter, for what steps
} HudData;

// An interface image with its size in game units.
typedef struct HudSprite {
    GfxTexture tex;
    float width, height;
} HudSprite;

typedef struct Interface {
    HudSprite health, ammo, jet; // the icons
    HudSprite health_bar, jet_bar, reload_bar, vest_bar;
    HudSprite fire_bar, fire_bar_r; // the fire bar and its frame
    HudSprite nade, cluster_nade;
    HudSprite dot;      // the ping dot
    HudSprite cursor;   // the crosshair
    HudSprite back;     // the translucent box behind the team box and the frags menu
    HudSprite noflag;   // a team's flag away from its base
    HudSprite arrow;    // the indicator above my head
    HudSprite scroll;   // the frags menu's scroll hint
    HudSprite guns[WEAPON_COUNT]; // the kill console's icons, by weapon
} Interface;

// The images from <base>/interface-gfx, keyed on pure green as the original's are.
// The context must be up.
void interface_load(Interface *hud, const char *base, const ScaleData *scales);
void interface_unload(Interface *hud);

// The HUD over the world: sets its own transform. `cursor` is the game's cursor in the
// 480-tall view's units, `viewport` the window's pixels.
void interface_draw(const Interface *hud, const HudData *data, const RenderState *state, const Context *ctx,
                    const GameCamera *camera, Vec2 cursor, Rect viewport);
