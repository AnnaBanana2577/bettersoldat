#pragma once

// The bots: soldiers the server plays itself. The original's AI (opensoldat's AI.pas,
// ControlBot and SimpleDecision), ported as it stands: each tick a bot looks for the
// nearest enemy it can see from its head, and fights it by how far away it is along
// each axis (SimpleDecision: closer than DIST_CLOSE it crouches and fires, at
// DIST_VERY_FAR it jumps and fires now and then, a camper lies down...), aiming ahead
// of the target with its accuracy as a spread; with nobody in sight it walks the map's
// waypoints, taking from the one it is heading for the keys the mapper laid on it,
// picking the next of its connections at random, on its team's path in CTF and the
// other team's once it carries the flag; a flag, a kit it needs or a knife it sees
// draws it off the path (GoToThing); a grenade near it is run from; stuck, it jumps,
// and after too long on one waypoint it forgets the path and finds another. It is
// pissed off at who last hit it and looks for them first. A bot is told by a .bot file
// (data/bots, the original's format): its name, look, favourite weapon, accuracy,
// how often it throws grenades, whether it camps, and what it says.
//
// The bots are a source of commands, as the players' clients are: before the tick
// bots_commands fills a bot's slot in the commands, and after it bots_hear reads the
// tick's events for what was done to it. Nothing here is in the simulation; the
// bots read the world as a client would and the server does with their commands what
// it does with a player's. What a bot says goes to a callback (the server relays it as
// chat).

#include "game/game.h"
#include "game/systems/systems.h"

#define BOT_NAME_SIZE 24  // a name, as NET_NAME_SIZE
#define BOT_TEXT_SIZE 128 // a line of chat, as NET_TEXT_SIZE
#define BOT_PROFILES 64   // as many .bot files as are read

// What a .bot file says.
typedef struct BotProfile {
    char name[BOT_NAME_SIZE];
    PlayerLook look;
    WeaponId favourite; // the primary it spawns with (Favourite_Weapon, by the weapon's name)
    WeaponId secondary; // Secondary_Weapon: 0 USSOCOM, 1 knife, 2 chainsaw, 3 LAW
    char friend[BOT_NAME_SIZE]; // a player it never fires at
    int accuracy;        // the spread of its aim, in units; more is worse
    bool shoot_dead;     // Shoot_Dead: fires on a corpse a while
    int grenade_frequency; // one in this many ticks, with a target near
    int camping;         // Camping: crouches and lies in wait
    int chat_frequency;  // Chat_Frequency: how rarely it talks (more is rarer)
    char chat_kill[BOT_TEXT_SIZE], chat_dead[BOT_TEXT_SIZE], chat_low_health[BOT_TEXT_SIZE];
    char chat_see_enemy[BOT_TEXT_SIZE], chat_winning[BOT_TEXT_SIZE];
} BotProfile;

// A .bot file; false (and says so on stderr) if it can't be read or names no weapon.
bool bot_profile_load(const char *path, const Weapons *weapons, BotProfile *out);
// Every .bot under <data>/bots, sorted by file name: how many were read.
int bot_profiles_load(const char *data, const Weapons *weapons, BotProfile *out, int max);
// One of them at random, for a bot nobody named: the original's RandomBot, which never
// picks "Boogie Man" (it takes "Sniper" instead). NULL with none.
const BotProfile *bot_profile_random(const BotProfile *profiles, int count, uint64_t *rng);

// The original's bots_difficulty: scales the accuracy's spread (100 as the file says,
// 50 half, 300 three times) and, below some marks, what the bot bothers with.
#define BOT_DIFFICULTY_NORMAL 100

typedef struct BotSettings {
    int difficulty;
    bool chat; // bots_chat: whether they talk
} BotSettings;

// A bot's keys, as the original's TControl, kept between ticks: FreeControls clears
// them each tick but the aim, and a grenade being wound up.
typedef struct BotControls {
    bool left, right, up, down, fire, jet, throw_nade, change, throw_weapon, reload, prone;
} BotControls;

// The original's TBotData, per soldier.
typedef struct Brain {
    bool active;
    BotProfile profile;
    int accuracy;  // the profile's, scaled by the difficulty
    int chat_freq; // the profile's, as the original stretches it
    uint64_t rng;
    BotControls controls;
    Vec2 aim;
    int pissed_off; // the slot that last hit it + 1; 0 none
    int path_num;
    int target;     // the slot it fights; the original's TargetNum less one
    bool go_thing;  // walking to a thing rather than the waypoints
    int current_waypoint, next_waypoint, old_waypoint, last_waypoint; // 1-based, 0 none
    int waypoint_time, waypoint_timeout_counter, one_place_count;
    int fall_save;
    int life_seen; // the soldier's life last tick: a new one is a respawn
} Brain;

// Something a bot says: `slot` is its soldier's.
typedef void (*BotSay)(void *user, int slot, const char *text);

typedef struct Bots {
    Brain brains[MAX_PLAYERS];
    BotSettings settings;
    BotSay say;
    void *say_user;
} Bots;

void bots_init(Bots *b, BotSettings settings, BotSay say, void *say_user);
// The soldier in `slot` is a bot with this profile from now; `seed` its own randomness.
void bots_attach(Bots *b, int slot, const BotProfile *profile, uint64_t seed);
void bots_detach(Bots *b, int slot);
bool bots_has(const Bots *b, int slot);
int bots_count(const Bots *b);
// A new round, a new map: the paths are forgotten.
void bots_new_round(Bots *b);

// Before the tick: every bot's command for it, into its slot of `cmds`. `names` are
// the players' by slot (a friend is known by name), NULL or "" for none. The bots
// read the world and write only a thing's interest, as the original's do.
void bots_commands(Bots *b, Game *g, const char *const names[MAX_PLAYERS], Command cmds[MAX_PLAYERS]);
// After the tick: what the tick did to the bots. A hit makes a bot pissed off at the
// shooter; a kill has the killer and the killed say their lines, when bots talk.
void bots_hear(Bots *b, const Game *g);
