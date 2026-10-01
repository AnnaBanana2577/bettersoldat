#pragma once

// What happens to the entities: the systems, one file each, run as the passes of a
// tick in the order world_step gives them (game.h). Each takes the static Context and
// the World it acts on; nothing here keeps state of its own.
//
// The systems talk only through events. A pass writes its own entities and reads any;
// what it wants of another's it emits, and the owner's pass does it when it runs: a
// shot (EVENT_SHOT) becomes a bullet in the bullet pass, a gun let go of
// (EVENT_WEAPON_DROP) is laid down by the things pass, a kit's gift (EVENT_KIT_PICKUP)
// is taken by the soldier in the receipts pass. A pass consumes every event since it
// last ran (events_pending): this tick's before it began, and the last tick's after it
// ran, so what a later pass asks of an earlier one happens next tick, as in the
// original's frame. The same events are what the client shows and what the wire
// carries; a shot heard from another machine is a shot like any other.
//
// Some of a soldier's fields are another system's on it, written by that system's pass
// alone: what it holds and mans and the heat it has run up (held, stat, use_time) and
// the things' counters on it (flag_grab_cooldown, medikit_cooldown) are the things';
// its health, its death and its tally are the wounds'.
//
// the soldiers' pass, one soldier at a time in the original's order, and the receipts
//   soldier.c            the soldier's tick in order; spawning; the server's half; the receipts
//   movement.c           the control state machines: input -> animation -> forces
//   soldier_collision.c  the soldier against the map; what special polys do
//   pose.c               the skeleton pose from the animations and the aim
//   antics.c             the idle antics
//   combat.c             the weapon in hand: firing, reloads, changing, throwing
//   weapons.c            the weapons table and its defaults
// the corpses' pass
//   ragdoll.c            the corpses
// the bullets' pass
//   bullet.c             the bullet pool: the shots made, the tick, the flight
//   bullet_collision.c   what a bullet meets: the map, colliders, soldiers, things
//   explosion.c          grenades, rockets and clusters going off
// the wounds' pass
//   damage.c             the one place health changes
// the things' pass
//   thing.c              the thing pool: creating, the physics, respawning, the pickups
//   flag.c               the flags: carried, thrown, returned, captured
//   kit.c                the medical, grenade and bonus kits
//   dropped_gun.c        guns thrown or let go of, and the knives that land
//   parachute.c          the parachute of a high spawn
//   stat_gun.c           the stationary gun
// and beside the passes
//   spawn.c              where a team is placed
//   event.c              the tick's events, and the passes' mail
//   history.c            where everyone was over the last second
//   rand.c               the game's own randomness

#include "game/game.h"

// --- soldier.c ---------------------------------------------------------------------

#define DEFAULT_HEALTH 150.0f
#define DEFAULT_AIM_DIST 7.0f // the camera's lead toward the aim (DEFAULTAIMDIST)
#define SNIPER_AIM_DIST 3.5f  // scoped prone (SNIPERAIMDIST)
#define CROUCH_AIM_DIST 4.5f  // scoped crouching (CROUCHAIMDIST)
#define AIM_DIST_STEP 0.05f   // per tick, toward either (AIMDISTINCR)
#define DEFAULT_CEASE_FIRE 90
#define MAX_VELOCITY 11.0f // the safety clamp on a soldier's speed, each way

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

// The receipts pass: the soldiers take what the things gave them since it last ran
// (a kit's gift, a gun into the hands), told or heard of.
void soldiers_receive(const Context *ctx, World *w, const Events *last, Events *events);

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

// Around a round of collision checks: no background poly met yet, and if none was met,
// out of any.
void bg_test_prepare(BackgroundState *bg);
void bg_test_reset(BackgroundState *bg);

// --- pose.c ------------------------------------------------------------------------

// The pose of a living soldier drawn at pos (usually its own or an interpolated one).
Pose soldier_pose(const Anims *anims, const Soldier *s, Vec2 pos);

// --- antics.c ----------------------------------------------------------------------

#define DEFAULT_IDLE_TIME (60 * 8)  // standing still this long brings an antic (DEFAULT_IDLETIME)
#define LONGER_IDLE_TIME (60 * 30)  // a lit cigar is smoked this long (LONGER_IDLETIME)

// The idle antics and the taunts, on the soldier's animations; `armed` as soldier_step's,
// for the mercy's shot.
void antics_apply(const Context *ctx, World *w, uint8_t index, Events *events, bool armed);

// --- pose.c, the swing --------------------------------------------------------------

// The chain's and the hair's points (Soldier.swing) after this tick's pose.
void soldier_swing(const Context *ctx, const World *w, Soldier *s);

// --- combat.c ----------------------------------------------------------------------

void combat_fire(const Context *ctx, World *w, uint8_t index, Events *events);

// A fresh weapon of this kind, as it is picked up or spawned with.
Weapon weapon_state(const Context *ctx, WeaponId id);

// This tick's buttons on the weapon, in the control order.
void combat_control(const Context *ctx, World *w, uint8_t index, Events *events);

// A soldier asks for a bullet (EVENT_SHOT), numbered as its next, for the bullet pass
// to make. The index of the event, or -1 if the tick's buffer was full.
int soldier_shoot(World *w, uint8_t index, WeaponId weapon, Vec2 pos, Vec2 vel, float damage, Events *events);

// The two parts of the weapon's control that come later in the original's order: the
// Barrett's bolt and the stationary gun's after going prone, the reload animations after
// the locomotion.
void combat_after_prone(const Context *ctx, World *w, Soldier *s);
void combat_reload_animation(const Context *ctx, Soldier *s);

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

// How long a bullet of each kind lives, in ticks.
#define BULLET_TIMEOUT (60 * 7)
#define GRENADE_TIMEOUT (60 * 3)
#define M2BULLET_TIMEOUT 60
#define FLAMER_TIMEOUT 32
#define MELEE_TIMEOUT 1

// The stationary gun (stat_gun.c): shots on end before it starts to wander, and before
// it overheats and stops (M2GUN_OVERAIM, M2GUN_OVERHEAT). The audio reads the second.
#define M2_OVERAIM 4
#define M2_OVERHEAT 18

bool weapon_is_primary(WeaponId id);   // Eagle through Minigun
bool weapon_is_secondary(WeaponId id); // Colt, Knife, Chainsaw, LAW
// The guns that lie on the ground once let go of: not the hands, the flamer, the M2,
// the grenades, nor the bows outside Rambo.
bool weapon_droppable(WeaponId id);

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

// The wounds pass: every Hit pending lands. Where the world has authority it lands
// whole; elsewhere only its shove and its bink, which the original gives wherever the
// bullet is flown (see hit_shove).
void wounds_apply(const Context *ctx, World *w, const Events *last, Events *events);

// A Hit lands: the knockback, the wound (the vest and berserker rules, then death), the
// disturbed aim.
void damage_apply(const Context *ctx, World *w, Hit hit, Events *events);
// A Hit's knockback and disturbed aim alone, on the living.
void hit_shove(const Context *ctx, World *w, Hit hit);

// What a Hit would take off the target's health; 0 where it does not wound (a teammate
// without friendly fire, the Flame God).
float hit_damage(const World *w, Hit hit);
void die(const Context *ctx, World *w, Hit hit, Events *events);

// --- bullet.c ----------------------------------------------------------------------

#define BULLET_GRAVITY 2.25f // a bullet falls this many times the world's gravity
#define ARROW_RESIST 280     // an arrow stuck in a wall stays this long

// The bullet system's own spawn, for the bullets a bullet makes (a cluster's children):
// the next of its owner's numbers, into the first free slot. Its index, or -1 if none
// was made. Everyone else asks with an EVENT_SHOT (soldier_shoot).
int bullet_spawn(const Context *ctx, World *w, Vec2 pos, Vec2 vel, WeaponId weapon, uint8_t owner, float damage, Events *events);

// The bullet pass: the shots asked for since it last ran become bullets, then every
// bullet's tick, then every bullet's flight.
void bullets_update(const Context *ctx, World *w, const Events *last, Events *events);

// Every deactivation goes through here, so the end is heard where it happens. `impact`
// is where it stopped against something, if it did (may be NULL).
void bullet_end(Bullet *b, uint16_t index, Events *events, const Vec2 *impact);

// --- bullet_collision.c ------------------------------------------------------------

// One tick of a bullet against the map, the colliders, the soldiers and the things.
void bullet_collide(const Context *ctx, World *w, Bullet *b, uint16_t index, Events *events);

// The pose points a bullet can hit, in priority order: the head first.
#define HIT_PART_COUNT 7
extern const int HIT_PARTS[HIT_PART_COUNT];

// A weapon's damage modifier for a pose point: legs, chest or head.
float hitbox_modifier(const WeaponStats *stats, int part);

// --- explosion.c -------------------------------------------------------------------

typedef enum ExplosionKind {
    EXPLOSION_FRAG,    // a frag grenade
    EXPLOSION_M79,     // an M79 grenade, a LAW rocket, a flame arrow
    EXPLOSION_CLUSTER, // a cluster, and an M2 bullet's flak
} ExplosionKind;

// A bullet going off: every soldier in the radius hit, the corpses and things shoved,
// nearby grenades and rockets set off, the bullet ended. `hit_soldier` and `hit_part`
// name a soldier it struck directly, or are -1.
void explode(const Context *ctx, World *w, Bullet *b, uint16_t index, ExplosionKind kind, int hit_soldier, int hit_part, Events *events);

// --- thing.c -----------------------------------------------------------------------

#define GUN_RESIST_TIME (60 * 20)          // a dropped gun lies this long, and resists pickup at first
#define FLAG_TIMEOUT (60 * 25)             // a loose flag, a bonus kit: this long before it goes
#define FLAG_INTEREST_TIME (60 * 25)       // how long the bots go for a thing
#define DEFAULT_INTEREST_TIME (60 * 5 + 50)
#define BOW_INTEREST_TIME (60 * 41 + 40)

// The spawn point kinds the map author places for the things.
#define SPAWN_ALPHA_FLAG 5
#define SPAWN_BRAVO_FLAG 6
#define SPAWN_GRENADE_KIT 7
#define SPAWN_MEDICAL_KIT 8
#define SPAWN_CLUSTER_KIT 9
#define SPAWN_VEST_KIT 10
#define SPAWN_FLAMER_KIT 11
#define SPAWN_BERSERK_KIT 12
#define SPAWN_PREDATOR_KIT 13
#define SPAWN_BOW 15
#define SPAWN_STAT_GUN 16

bool thing_is_flag(ThingStyle style);
bool thing_is_kit(ThingStyle style);

// A thing of a style at a spot (CreateThing), into `slot` or the first free one, owned
// by soldier `owner` (index + 1, or 0). A gun of a living or dead owner flies from its
// hand. Its index, or -1 if there was no room.
int thing_create(const Context *ctx, World *w, ThingStyle style, Vec2 pos, WeaponId weapon, uint8_t owner, int slot);

// The thing is gone (TThing.Kill). Its slot remembers where a kit last came up.
void thing_kill(Thing *t);

// A flag or kit back to one of its spawn points (TThing.Respawn), in the same slot.
void thing_respawn(const Context *ctx, World *w, int index);

// A random active spawn point of this kind, a few pixels off it (RandomizeStart); any
// spawn point, and false, if the map has none of the kind.
bool thing_spawn_point(const Map *m, int32_t kind, uint64_t *rng, Vec2 *pos);

// SpawnBoxes: the same, but not the spawn point `t`'s slot last came up at, unless it
// is the only one of the kind.
bool thing_spawn_boxes(const Map *m, int32_t kind, Thing *t, uint64_t *rng, Vec2 *pos);

// The round's things: the flags, the kits, the stationary guns.
void things_spawn(const Context *ctx, World *w);

// The things pass: what was asked of the things since it last ran (a gun let go of, a
// knife landed, a kill that lets a flag fall), then every thing's tick: its physics,
// the flags' bases and captures, the pickups, the timeouts.
void things_update(const Context *ctx, World *w, const Events *last, Events *events);

// Whether bullets and blasts knock this thing about: flags always, the bow always,
// dropped guns and kits as the match says, the rest never.
bool thing_collides_with_bullets(const World *w, const Thing *t);

// A soldier died: the flag it carried falls, and what it threw is nobody's.
void things_let_go(World *w, uint8_t index, Events *events); // a flag let go of is an event, if `events`

// --- flag.c ------------------------------------------------------------------------

// The flag spawn in-base is measured from: the map's first of the kind.
Vec2 flag_base(const Map *m, ThingStyle style);

// The flag's own tick after its physics: in base, and home again or captured.
void flag_update(const Context *ctx, World *w, int index, Events *events);

// A soldier near the flag takes it, or returns its own.
void flag_touch(const Context *ctx, World *w, int index, uint8_t soldier, Events *events);

// The carrier lobs the flag toward the cursor (TSprite.ThrowFlag), as it asked
// (EVENT_FLAG_THROW). Only with authority.
void flag_throw(const Context *ctx, World *w, uint8_t soldier);

// --- kit.c -------------------------------------------------------------------------

// The medical or grenade kits of a map, or a bonus kit, at their spawn points
// (SpawnThings).
void kits_spawn(const Context *ctx, World *w, ThingStyle style, int amount);

// Whether the kit is worth taking to this soldier, and what it gives.
bool kit_wanted(const World *w, ThingStyle style, const Soldier *s);
// The taking, in the things pass: the pickup told (EVENT_KIT_PICKUP), the kit gone or
// up elsewhere, the soldier's medikit cooldown started.
void kit_take(const Context *ctx, World *w, int index, uint8_t soldier, Events *events);
// What the kit gives, in the soldiers' receipts pass, from the pickup told.
void kit_give(const Context *ctx, const World *w, Soldier *s, ThingStyle style);

// The bonus kits that turn up now and then, on the server's schedule.
void bonuses_spawn(const Context *ctx, World *w, const MatchSettings *settings, uint32_t tick);

// --- dropped_gun.c -----------------------------------------------------------------

// A gun that left a soldier's hands (EVENT_WEAPON_DROP) laid down where the hand was,
// flying as the death or the throw sends it. Only with authority.
void dropped_gun_drop(const Context *ctx, World *w, const EventWeaponDrop *e);

// A thrown knife that stopped (EVENT_KNIFE_LAND) lies there as a knife to pick up.
void thrown_knife_land(const Context *ctx, World *w, const EventKnifeLand *e);

// Whether a soldier may take the gun, and the taking.
bool dropped_gun_wanted(const Thing *t, const Soldier *s);
// The taking, in the things pass: the pickup told (EVENT_WEAPON_PICKUP), the gun gone.
void dropped_gun_take(const Context *ctx, World *w, int index, uint8_t soldier, Events *events);
// The gun into the hands, in the soldiers' receipts pass, from the pickup told.
void dropped_gun_give(const Context *ctx, Soldier *s, WeaponId weapon, int32_t ammo);

// --- parachute.c -------------------------------------------------------------------

// A soldier placed high over the map floats down under one (TSprite.Parachute).
void parachute_deploy(const Context *ctx, World *w, uint8_t soldier);

// The parachute's tick: let go of by a holder on the ground or jetting, or by a landed
// corpse; else hung from its holder's head.
void parachute_update(const Context *ctx, World *w, int index);

// The holder's side, in its own step, reading the parachute back: before integrating,
// a canopy that turned over catches the fall for a tick; after, the lift for the next.
void parachute_catch(World *w, Soldier *s);
void parachute_carry(World *w, Soldier *s);

// --- stat_gun.c --------------------------------------------------------------------

// The stationary gun's tick: taken, aimed and fired by whoever mans it, left with a
// jump or the jets.
void stat_gun_update(const Context *ctx, World *w, int index, Events *events);

// Every gunner's heat bleeds off while the trigger is up; before the guns' ticks.
void stat_guns_cool(World *w);

// --- ragdoll.c ---------------------------------------------------------------------

// One tick of every corpse.
void ragdolls_update(const Context *ctx, World *w, Events *events);

// A corpse's points as a pose, for the bullets to meet it where it lies.
Pose ragdoll_pose(const Ragdoll *r);

// Any of the body's points off the map (CheckSkeletonOutOfBounds): the soldier is placed
// again.
bool ragdoll_out_of_bounds(const Context *ctx, const Ragdoll *r);

// --- spawn.c -----------------------------------------------------------------------

// A random active spawn point of the team's, or of the general ones, or the origin.
Vec2 spawn_point(const Map *m, Team team, uint64_t *rng);

// --- event.c -----------------------------------------------------------------------

// Dropped silently once the tick's buffer is full.
void event_emit(Events *events, Event e);
void events_clear(Events *events);

// The events a pass has not seen, in order: the last tick's from where the pass stood
// when it ran, then this tick's up to where it begins now. Taking the cursor marks the
// pass as begun; what the pass emits itself is for the next time.
typedef struct EventCursor {
    const Events *last, *now;
    int i, end;
    bool in_now;
} EventCursor;

EventCursor events_pending(const Events *last, Events *now, Pass pass);
const Event *events_next(EventCursor *c); // NULL once they are all seen

// --- history.c ---------------------------------------------------------------------

// The soldiers as they stand, filed under the world's tick.
void history_record(History *h, const World *w);

// The soldiers, and the things, as they stood at `tick`, or NULL if the ring no longer
// (or never) has it.
const Soldier *history_at(const World *w, uint32_t tick);
const Thing *history_things_at(const World *w, uint32_t tick);

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
