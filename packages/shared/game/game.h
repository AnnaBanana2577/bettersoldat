#pragma once

// The game: the simulation shared by the client and the server.
//
//   Game {
//     Context  the static data a tick reads and never writes: map, anims, weapons, skeletons
//     World    the state: the soldiers, bullets, things and corpses, stepped by world_step
//     Match    the round around it: the clock, the scores, the settings, run by match_run
//   }
//
// Rules:
//   - No I/O, no rendering, no audio, no globals in a tick. Loading the Context is the
//     only place files are read.
//   - No allocation per tick: fixed-capacity arrays only.
//   - Randomness comes from World.rng, from a soldier's own rng for what its player
//     rolls, and from a bullet's own numbers for what happens to it in flight, so a
//     bullet flies the same on every machine.
//   - Nothing wounds a soldier on its own. Bullets and blasts emit a Hit; the server
//     applies it through damage_apply. Health changes in one place.
//   - Everything else that happened is emitted as an Event too.
//   - The world never hears of the match. What it does that the match must know (a
//     kill, a capture) it emits as an event; what the match decides (the round is over,
//     friendly fire) reaches the world as values in World.rules.
//
// Who runs what. Every machine runs this same simulation on its own world, and one flag
// on the world, authority, marks the server's: the one that decides. Only with
// authority do hits become wounds, are things made, taken, returned and scored, and do
// the dead respawn. Tools and tests run game_tick on a whole world with authority.

#include "game/entities.h"
#include "resources/skeleton.h"

#define TICK_SECONDS (1.0 / TICK_RATE)
#define DEFAULT_GRAVITY 0.06f

// Static data a tick reads and never writes.
typedef struct Context {
    Map *map;
    Anims *anims;
    Skeletons *skeletons; // the things' and the corpses' particle objects
    Weapons weapons;
} Context;

// What the match decides that the world reads.
typedef struct WorldRules {
    bool frozen; // between rounds nobody moves
    bool friendly_fire;
    bool kits_collide; // bullets and blasts knock kits (sv_kits_collide; flags always)
    bool guns_collide; // and dropped guns (sv_guns_collide; the bow always)
    int32_t respawn_time;
    int32_t max_grenades;
    int32_t medikit_cooldown; // ticks before a soldier may take a second medikit
    bool stationary_guns;     // the map's stationary guns are placed
    bool flags;               // the flags are placed: a game of capture the flag
    bool rope;                // the rope is allowed (sv_rope); off, the boots are jets
} WorldRules;

typedef struct World {
    uint32_t tick;
    float gravity;
    uint64_t rng;
    Soldier soldiers[MAX_PLAYERS];
    Bullet bullets[MAX_BULLETS];
    Thing things[MAX_THINGS];
    Ragdoll ragdolls[MAX_PLAYERS]; // the corpses, one per dead soldier
    History *history;              // the server's rewind for judging shots; NULL elsewhere
    WorldRules rules;

    // This world decides: things are made, taken, returned and scored here, and the
    // dead respawn. Elsewhere (a client's world) things only move, and what was decided
    // arrives as facts.
    bool authority;
} World;

typedef enum MatchState { MATCH_PLAYING, MATCH_ENDED, MATCH_PAUSED } MatchState;

#define DEFAULT_RESPAWN_TIME 180
#define DEFAULT_MAX_GRENADES 2
#define DEFAULT_MEDIKIT_COOLDOWN 2 // seconds
#define DEFAULT_TIME_LIMIT (15 * 60 * TICK_RATE)
#define DEFAULT_SCORE_LIMIT 10
#define ROUND_END_TICKS (5 * TICK_RATE + 20) // the scores stand this long before the next round

// What is played: a deathmatch, everyone against everyone with no team, won by the
// first to the score limit in kills; or capture the flag, alpha against bravo, won by
// the team to the score limit in captures. A map with a flag's spawn point plays CTF
// unless a deathmatch is asked for (sv_gamemode); a map without one plays a deathmatch
// whatever is asked. The other modes of the original are still to come.
typedef enum MatchMode { MATCH_DEATHMATCH, MATCH_CTF, MATCH_MODE_COUNT } MatchMode;

typedef struct MatchSettings {
    MatchMode mode;
    int32_t time_limit; // ticks
    int32_t score_limit;
    int32_t respawn_time;
    int32_t max_grenades;
    bool friendly_fire;
    bool kits_collide;
    bool guns_collide;
    bool stationary_guns;         // sv_stationaryguns
    int32_t medikit_cooldown;     // seconds (sv_healthcooldown)
    int32_t bonus_frequency;      // 0 none, 1 rare .. 5 often (sv_bonus_frequency)
    bool bonus_flamer, bonus_predator, bonus_berserker, bonus_vest, bonus_cluster;
    bool rope; // sv_rope: the rope is allowed; off, a rope soldier's boots are jets
} MatchSettings;

typedef struct Match {
    MatchSettings settings;
    MatchState state;
    int32_t scores[TEAM_COUNT];
    int32_t time_left;
    int32_t counter; // after it ends: ticks until the next round
} Match;

typedef struct Game {
    Context ctx;
    World world;
    Match match;
    Events events;   // what the last tick left behind
    Events last;     // and the tick before it, for the passes that had run before it was emitted
    Events incoming; // what was heard from elsewhere, for the next tick's passes (game_hear)
} Game;

// --- Context -----------------------------------------------------------------------

// The map, animations, skeletons and default weapons from a data folder
// (the layout of opensoldat's base: maps/, anims/, objects/). Reports failures on stderr.
bool context_load(Context *ctx, const char *base_dir, const char *map_name);
void context_destroy(Context *ctx);

// --- World -------------------------------------------------------------------------

void world_init(World *w, uint64_t seed);

// One tick of everything in the world, as passes in the order the server and the
// clients run them (the original's UpdateFrame): every active soldier on its command,
// then the corpses, the bullets, the wounds where the world has authority, the things,
// and the soldiers taking what the things gave; then the tick advances. The passes
// talk only through events (see Pass): `last` is the tick before's, for what was asked
// of a pass after it had run.
void world_step(const Context *ctx, World *w, const Command cmds[MAX_PLAYERS], const Events *last, Events *events);

// --- Match -------------------------------------------------------------------------

MatchSettings match_default_settings(void);
// The defaults, in the mode the map plays.
MatchSettings match_settings_for_map(const Map *map);
// The mode `map` plays when `wanted` is asked for: the map's own for MATCH_MODE_COUNT
// (the server's sv_gamemode 0), else `wanted`, unless it is CTF on a map with no flags.
MatchMode match_mode_choose(const Map *map, MatchMode wanted);
// Whether the mode has teams.
bool match_has_teams(const Match *m);
void match_init(Match *m, MatchSettings settings);

// The match's tick, after the world's: what the server keeps of every soldier (the
// respawns, the spawn protection, the bonuses), the captures scored, the bonus kits
// that turn up, the clock, the end of the round. Only where the world has authority;
// a client's match comes down the wire.
void match_run(const Context *ctx, World *w, Match *m, Events *events);

// The round has ended and its scores have stood long enough: time for the next.
bool match_over(const Match *m);

// The round ends now, as at a limit: the scores stand for ROUND_END_TICKS with the
// world frozen (the original's MapChangeCounter), and EVENT_MATCH_END says who won.
// The server's, for `nextmap` and a map vote passed; emitted into `events`, which is
// the mailbox (game_hear's) when called between ticks.
void match_stop(Match *m, Events *events);
// Paused, nobody moves and the clock stands, until resumed; a round that has ended
// is left alone. True if the state changed.
bool match_pause(Match *m, bool paused);

// What the match decides that the world reads.
WorldRules match_rules(const Match *m);

// --- Game --------------------------------------------------------------------------

// A fresh world and match over an already loaded context.
void game_init(Game *g, uint64_t seed, MatchSettings settings);

// The whole-world tick: what was heard becomes the tick's first events, then the
// match's rules into the world, world_step, match_run.
void game_tick(Game *g, const Command cmds[MAX_PLAYERS]);

// A decision made elsewhere (a shot from another machine, a kill the server ruled): it
// goes among the next tick's events, before the passes, and the owner's pass does it as
// it would its own. Dropped silently once the mailbox is full.
void game_hear(Game *g, Event e);
