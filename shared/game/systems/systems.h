#pragma once

// What happens to the entities, one file per system. Each takes the static Context and
// the World it acts on; nothing here keeps state of its own.
//
//   soldier.c            the soldier's tick in order; spawning; the server's half
//   movement.c           the control state machines: input -> animation -> forces
//   soldier_collision.c  the soldier against the map; what special polys do
//   pose.c               the skeleton pose from the animations and the aim
//   antics.c             the idle antics
//   combat.c             the weapon in hand: firing, reloads, changing, throwing
//   weapons.c            the weapons table and its defaults
//   damage.c             the one place health changes
//   bullet.c             the bullet pool
//   thing.c              the thing pool: flags, kits, dropped guns, parachutes, stat guns
//   ragdoll.c            the corpses
//   spawn.c              where a team is placed
//   event.c              the tick's events
//   history.c            the server's rewind of the soldiers
//   rand.c               the game's own randomness
//
// Not yet ported from soldat-odin (their entry points exist and do nothing):
// combat_control's firing and throwing, the bullet flight and collisions, the things,
// the corpses.

#include "game/game.h"

// --- soldier.c ---------------------------------------------------------------------

#define DEFAULT_HEALTH 150.0f
#define DEFAULT_CEASE_FIRE 90

// A fresh soldier at a spot; the tally and the count of its lives survive a respawn.
void soldier_spawn(const Context *ctx, Soldier *s, Vec2 pos, Team team, WeaponId primary, WeaponId secondary);

// The server places a soldier on one of its team's spawn points: a new life.
void soldier_respawn(const Context *ctx, World *w, uint8_t index, Events *events);

// The weapons a soldier chose, put in its hands.
void soldier_arm(const Context *ctx, Soldier *s, WeaponId primary, WeaponId secondary);

// Euler integration of the body particle, before the control step.
void soldier_integrate(Soldier *s, float gravity);

// One tick of one soldier: integrate, take the knockback, the controls through the
// state machines, animate, collide with the map, the weapon timers, the jet fuel.
// `armed` is false where a soldier is moved without its player behind it, which leaves
// its weapon alone.
void soldier_step(const Context *ctx, World *w, uint8_t index, Command cmd, Events *events, bool armed);

bool soldier_out_of_bounds(const Context *ctx, Vec2 pos);

// Suicide is a hit on oneself, applied like any other, and a brutal one.
Hit suicide_hit(const World *w, uint8_t index);

// The server's half of a soldier's tick, whoever moves it: the way back from death and
// from off the map, the spawn protection, the bonus.
void soldier_served_tick(const Context *ctx, World *w, uint8_t index, Events *events);

// The three halves of a soldier as the wire carries them: what its own client decides,
// what the server decides, and the rest.
void soldier_copy_owned(const Anims *anims, Soldier *dst, const Soldier *src);
void soldier_copy_served(Soldier *dst, const Soldier *src);
void soldier_copy_rest(Soldier *dst, const Soldier *src);

// --- movement.c --------------------------------------------------------------------

// The control step: resolve left+right, jets, the weapon, prone, cover, locomotion,
// rolls and the body pose, in the original's ControlSoldier order.
void soldier_control(const Context *ctx, World *w, uint8_t index, Events *events, bool armed);

// Leg transitions are blocked while lying down; Get_Up is the only way out of Prone.
void legs_apply(const Anims *anims, Soldier *s, AnimId id, int32_t frame);

// --- soldier_collision.c -----------------------------------------------------------

// The whole check against the map, at the end of the soldier's step once it has moved.
void soldier_collide(const Context *ctx, World *w, uint8_t index, Events *events);

// Point collision against the map polys; area 1 is the head, area 0 the feet.
bool check_map_collision(const Context *ctx, World *w, uint8_t index, Vec2 at, int area, Events *events);

bool soldier_collides_with(const Soldier *s, PolyType t);

// True if the poly is to be ignored as a background poly.
bool bg_test(const Map *m, BackgroundState *bg, uint16_t poly);
void bg_test_big_poly_center(const Map *m, BackgroundState *bg, Vec2 pos);

// --- pose.c ------------------------------------------------------------------------

// The pose of a living soldier drawn at pos (usually its own or an interpolated one).
Pose soldier_pose(const Anims *anims, const Soldier *s, Vec2 pos);

// --- antics.c ----------------------------------------------------------------------

void antics_apply(const Context *ctx, World *w, Soldier *s);

// --- combat.c ----------------------------------------------------------------------

// A fresh weapon of this kind, as it is picked up or spawned with.
Weapon weapon_state(const Context *ctx, WeaponId id);

// This tick's buttons on the weapon, in the control order.
void combat_control(const Context *ctx, World *w, uint8_t index, Events *events);

// The fire and reload timers, after the step.
void weapon_timers(const Context *ctx, Soldier *s);

// Adds bink with diminishing returns as more accumulates (Weapons.pas CalculateBink).
uint16_t calculate_bink(uint16_t accumulated, int bink);

// A hit disturbs the victim's aim by the bink of the weapon they hold.
void hit_spray(const Context *ctx, World *w, uint8_t victim, uint8_t attacker);

float movement_inaccuracy(const Context *ctx, const Soldier *s);
Vec2 hands_aim_direction(const Pose *pose);

// Where the soldier is aiming from its position; the fallback is the way it faces.
Vec2 aim_direction(const Soldier *s);

// --- weapons.c ---------------------------------------------------------------------

bool weapon_is_primary(WeaponId id);   // Eagle through Minigun
bool weapon_is_secondary(WeaponId id); // Colt, Knife, Chainsaw, LAW

// Soldat 1.7.1's table, which is also OpenSoldat's built-in one.
void weapons_default(Weapons *w);
void weapon_set_stats(WeaponInfo *info, WeaponStats stats);

// The derived weapons and numbers; call after changing any stats.
void weapons_finalize(Weapons *w);

// The weapon with this display name (any case), bare hands when there is none.
WeaponId weapon_named(const char *name);

// --- damage.c ----------------------------------------------------------------------

#define BRUTAL_DEATH_HEALTH (-400.0f)
#define HEADCHOP_DEATH_HEALTH (-90.0f)

// A Hit becomes a wound: the vest and berserker rules, then death.
void damage_apply(const Context *ctx, World *w, Hit hit, Events *events);
void die(const Context *ctx, World *w, Hit hit, Events *events);

// --- bullet.c ----------------------------------------------------------------------

// A bullet into the first free slot of the pool; its index, or -1 if none was made.
int bullet_spawn(const Context *ctx, World *w, Vec2 pos, Vec2 vel, WeaponId weapon, uint8_t owner, float damage, Events *events);
void bullets_update(const Context *ctx, World *w, Events *events);

// --- thing.c -----------------------------------------------------------------------

// The flags and kits from the map's spawn points, at the start of a round.
void things_spawn(const Context *ctx, World *w);
void things_update(const Context *ctx, World *w, Events *events);

// A gun leaves a soldier's hands: thrown on purpose, or let go of by a death.
void dropped_gun_throw(const Context *ctx, World *w, uint8_t index, Soldier *s, WeaponId weapon, int32_t ammo, Events *events);
void dropped_gun_from_death(const Context *ctx, World *w, uint8_t index, Soldier *s, Vec2 impact, Events *events);

// --- ragdoll.c ---------------------------------------------------------------------

// One tick of every corpse.
void ragdolls_update(const Context *ctx, World *w, Events *events);

// --- spawn.c -----------------------------------------------------------------------

// A random active spawn point of the team's, or of the general ones, or the origin.
Vec2 spawn_point(const Map *m, Team team, uint64_t *rng);

// --- event.c -----------------------------------------------------------------------

// Dropped silently once the tick's buffer is full.
void event_emit(Events *events, Event e);
void events_clear(Events *events);

// --- history.c ---------------------------------------------------------------------

// The soldiers as they stand, filed under the world's tick.
void history_record(History *h, const World *w);

// The soldiers a bullet with this lag meets: the frame that many ticks before the
// present, or the present itself where there is no history to rewind.
Soldier *history_targets(World *w, uint8_t lag);

// One of the soldiers a bullet meets, out of the frame history_targets gave. Its own
// shooter is taken from the present.
Soldier *target_soldier(World *w, Soldier *frame, uint8_t owner, int i);

// --- rand.c ------------------------------------------------------------------------

// xorshift64*: the same numbers on every machine.
uint64_t rand_next(uint64_t *state);
float rand_f32(uint64_t *state); // uniform in [0, 1)
int rand_int(uint64_t *state, int n);
