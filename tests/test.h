#pragma once

// The tests: headless checks of the simulation on real maps, and of the console. Each
// suite is a function of checks; a check that fails says where and why, and the run
// ends with how many failed. They read the maps and animations from assets/, so they
// run from the project directory:
//
//   xmake test
//
// The simulation's are scenes: a game on a map with two soldiers (alpha and bravo) a
// gap apart, settled, and driven tick by tick with the buttons a scenario presses.
// Soldier 0 aims at soldier 1's chest and soldier 1 at soldier 0's.

#include "game/game.h"
#include "game/systems/systems.h"

// --- checks ------------------------------------------------------------------------

void check_that(bool ok, const char *file, int line, const char *fmt, ...);

// A claim about the game; the message says what should be so.
#define CHECK(cond, ...) check_that((cond), __FILE__, __LINE__, __VA_ARGS__)

// --- scenes ------------------------------------------------------------------------

// A game on `map` from assets/, with alpha's soldier 0 on an alpha spawn point holding
// `a_weapon` and bravo's soldier 1 `gap` to its right holding `b_weapon`; the world
// has authority, as the server's does.
Game *scene(const char *map, float gap, WeaponId a_weapon, WeaponId b_weapon);
void scene_free(Game *g);

// Both soldiers stand a while, until they have landed and their spawn protection is
// gone.
void settle(Game *g);

// What a run of ticks left behind.
typedef struct Tally {
    int spawned[WEAPON_COUNT]; // bullets, by weapon
    int fired, hits, kills, explosions;
    float damage;
} Tally;

// Runs `ticks` ticks, soldier 0 pressing what `buttons` says on each (counted from 0),
// soldier 1 nothing.
typedef Buttons (*Presses)(int tick);
Tally run(Game *g, int ticks, Presses buttons);

Buttons press_nothing(int tick);
Buttons press_fire(int tick);

// A soldier put somewhere, at rest.
void place(Soldier *s, Vec2 at);

// The first thing of a style, or -1.
int find_thing(const Game *g, ThingStyle style);

// A spot `soldier` could fall from for a long way: open air at least 500 below.
Vec2 open_sky(const Game *g, int soldier);

// Whether two worlds are in the same state, as far as the simulation reads it.
bool same_world(const World *a, const World *b);

// --- the suites --------------------------------------------------------------------

void combat_tests(void);
void thing_tests(void);
void corpse_tests(void);
void events_tests(void);
void rope_tests(void);
void network_tests(void);
void join_tests(void);
void wire_tests(void);
void stream_tests(void);
void rewind_tests(void);
void console_tests(void);
void taunt_tests(void);
void color_tests(void);
void bot_tests(void);
void round_tests(void);
void script_tests(void);
void query_tests(void);
void lobby_tests(void);
void launcher_tests(void);
void demo_tests(void);
void shot_end_tests(void);
void bink_tests(void);
