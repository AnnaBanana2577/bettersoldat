#pragma once

// Everything the HUD shows that isn't in the frame's render state: the match and its
// players, the consoles, the chat, the big messages, the votes and the stats. The app
// fills one each frame from whatever it has; the drawing never reads the game. Until
// the server, the scoring and the messages are ported, most of it is empty and the
// parts it feeds stay hidden, as they are in the original with nothing to show.

#include "game/game.h"
#include "utils/utils.h"

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

typedef enum HudBonus { HUD_BONUS_NONE, HUD_BONUS_PREDATOR, HUD_BONUS_BERSERKER, HUD_BONUS_FLAMEGOD } HudBonus;

typedef enum HudVoteType { HUD_VOTE_NONE, HUD_VOTE_KICK, HUD_VOTE_MAP } HudVoteType;

// One weapon's line of the weapon stats (F2).
typedef struct HudWeaponStat {
    WeaponId weapon;
    char name[HUD_NAME];
    int shots, hits, kills, headshots;
} HudWeaponStat;

#define HUD_WEAPON_STATS 21
#define HUD_RADIO_LINES 3

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

    // the vote
    HudVoteType vote;
    char vote_target[HUD_NAME]; // the player or the map
    char vote_starter[HUD_NAME];
    char vote_reason[HUD_TEXT];
    bool vote_reason_typing; // "Type reason for vote:"

    // the radio menu, from txt/radiomenu-default.ini: the first choice, then the second
    bool radio_menu;
    char radio_first[HUD_RADIO_LINES][HUD_NAME];
    char radio_second[HUD_RADIO_LINES][HUD_NAME];
    int radio_state; // 0 none chosen, else the first choice, 1 to 3

    // my bonus, my weapon stats
    HudBonus bonus;
    int bonus_time; // ticks
    HudWeaponStat weapon_stats[HUD_WEAPON_STATS];
    int weapon_stat_count;

    // the camera, watching
    bool free_camera;
    int camera_follow; // the player the camera follows, -1 for me

    // the last shot's distance, while shown (the original's ShotDistanceShow)
    bool shot_distance_shown;
    float shot_distance, shot_airtime;
    int shot_ricochets;

    // the weapons menu's picks: mine, and the secondary chosen (cl_player_secwep)
    WeaponId selected_weapon;
    WeaponId selected_secondary;

    // toggles and clocks
    bool frags_menu;   // F1
    bool stats_menu;   // F2
    bool minimap;      // F3
    bool sniper_line;  // ui_sniperline, when the server allows
    bool recording;    // a demo
    bool bullet_time;  // sv_bullettime slowing the game: the widescreen cut
    bool show_info;    // F5: the FPS and ping line
    bool player_names; // the original's PlayerNamesShow
    int fps;
    double time; // seconds since the start, for what blinks and bobs
    int tick;    // the main tick counter, for what steps
} HudData;
