#pragma once

// What lives in the world: the soldiers and what they carry, the bullets, the things
// (flags, kits, dropped guns, parachutes, stationary guns), the corpses, the events a
// tick leaves behind, and the server's rewind of the soldiers. Plain data only: what
// happens to it is in systems/. Ported from soldat-odin's shared/sim.

#include "resources/animation.h"
#include "resources/map.h"
#include "utils/utils.h"

#define TICK_RATE 60

#define MAX_PLAYERS 32
#define MAX_BULLETS 512
#define MAX_THINGS 64
#define MAX_EVENTS 256
#define HISTORY_TICKS 64

// ---------------------------------------------------------------------------------
// Input

typedef enum Button {
    BUTTON_LEFT = 1 << 0,
    BUTTON_RIGHT = 1 << 1,
    BUTTON_JUMP = 1 << 2,
    BUTTON_CROUCH = 1 << 3,
    BUTTON_PRONE = 1 << 4,
    BUTTON_JET = 1 << 5,
    BUTTON_FIRE = 1 << 6,
    BUTTON_THROW = 1 << 7,
    BUTTON_RELOAD = 1 << 8,
    BUTTON_CHANGE = 1 << 9,
    BUTTON_SUICIDE = 1 << 10,
    BUTTON_DROP = 1 << 11,
    BUTTON_FLAG_THROW = 1 << 12,
} Button;

typedef uint16_t Buttons;

// Buttons that count once when pressed, however long they are held.
#define BUTTONS_ONE_SHOT \
    (BUTTON_THROW | BUTTON_CHANGE | BUTTON_PRONE | BUTTON_DROP | BUTTON_SUICIDE | BUTTON_FLAG_THROW | BUTTON_RELOAD)

// One tick of input for one soldier, numbered by the client that made it: the server
// runs them in order and says which it has run, and the client replays the rest.
typedef struct Command {
    uint32_t seq;
    Buttons buttons;
    Vec2 aim; // world-space cursor
} Command;

// ---------------------------------------------------------------------------------
// Weapons: the ids and the table's shape. The table itself is systems/weapons.c.

typedef enum WeaponId {
    WEAPON_NONE,
    WEAPON_EAGLE,
    WEAPON_MP5,
    WEAPON_AK74,
    WEAPON_STEYR,
    WEAPON_SPAS,
    WEAPON_RUGER,
    WEAPON_M79,
    WEAPON_BARRETT,
    WEAPON_M249,
    WEAPON_MINIGUN,
    WEAPON_COLT,
    WEAPON_KNIFE,
    WEAPON_CHAINSAW,
    WEAPON_LAW,
    WEAPON_BOW2,
    WEAPON_BOW,
    WEAPON_FLAMER,
    WEAPON_M2,
    WEAPON_FRAG,
    WEAPON_CLUSTER_NADE,
    WEAPON_CLUSTER,
    WEAPON_THROWN_KNIFE,
    WEAPON_COUNT
} WeaponId;

typedef enum BulletStyle {
    BULLET_PLAIN,
    BULLET_FRAG_GRENADE,
    BULLET_SHOTGUN,
    BULLET_M79,
    BULLET_FLAME,
    BULLET_PUNCH,
    BULLET_ARROW,
    BULLET_FLAME_ARROW,
    BULLET_CLUSTER_NADE,
    BULLET_CLUSTER,
    BULLET_KNIFE,
    BULLET_LAW,
    BULLET_THROWN_KNIFE,
    BULLET_M2,
} BulletStyle;

// The tunable numbers of a weapon: what a weapons mod sets.
typedef struct WeaponStats {
    float damage; // "HitMultiply"
    int32_t fire_interval;
    int32_t ammo;
    int32_t reload_time;
    float speed;
    BulletStyle style;
    int32_t startup;    // the wind-up of the minigun and the LAW
    int32_t bink;       // negative: self-bink per shot; positive: given to who it hits
    float movement_acc;
    float spread;
    float push;
    float inherit;      // of the shooter's velocity
    float mod_head, mod_chest, mod_legs;
} WeaponStats;

typedef struct WeaponInfo {
    const char *name;
    bool clip_reload; // a clip out and in, or shell by shell
    bool semi_auto;   // the trigger must be released between shots
    WeaponStats stats;

    // derived by weapons_finalize
    int32_t clip_out_time;
    int32_t clip_in_time;
    int32_t timeout;
} WeaponInfo;

typedef struct Weapons {
    WeaponInfo info[WEAPON_COUNT];
} Weapons;

// The gun in hand and what it is doing.
typedef struct Weapon {
    WeaponId id;
    int32_t ammo;
    int32_t fire_count;    // ticks until it may fire again
    int32_t reload_count;  // ticks of reload left
    int32_t startup_count; // the minigun's and LAW's wind-up
} Weapon;

// ---------------------------------------------------------------------------------
// Soldiers

typedef enum Stance { STANCE_STAND, STANCE_CROUCH, STANCE_PRONE } Stance;

typedef enum Bonus { BONUS_NONE, BONUS_FLAME_GOD, BONUS_PREDATOR, BONUS_BERSERKER } Bonus;

#define BACKGROUND_NORMAL 0
#define BACKGROUND_TRANSITION 1
#define BACKGROUND_POLY_NONE (-1)
#define BACKGROUND_POLY_UNKNOWN (-2)

// Walking "into" background polys: they only block when entered from outside.
typedef struct BackgroundState {
    uint8_t status;
    int16_t poly;
    bool test_result;
} BackgroundState;

// The idle antics: how long a soldier has stood still, and which antic it rolled.
typedef struct Idle {
    int32_t time;
    int8_t random;
} Idle;

// How a player looks: the colours and the styles the gostek is drawn with. The player
// chooses them (the client's cl_player_* cvars) and they travel with the name, so every
// client draws everyone the same. Nothing in a tick reads them.
typedef struct PlayerLook {
    Rgba shirt, pants, skin, hair;
    Rgba jet;            // the jet's flame, once the sparks are drawn
    uint8_t hair_style;  // 0 army (none), 1 dreadlocks, 2 punk, 3 Mr. T, 4 normal
    uint8_t head_style;  // 0 none, 1 helmet, 2 hat
    uint8_t chain_style; // 0 none, 1 dog tags, 2 gold chain
} PlayerLook;

// A soldier is run in one place at a time: the server runs them all, on the commands
// their clients sent, and a client runs its own as well to predict it. What a soldier's
// player decides and what the server decides are kept apart, because the server sends
// the parts separately (see soldier_copy_owned / soldier_copy_served).
typedef struct Soldier {
    // the server's
    bool active;
    Team team;
    float health;
    bool dead;
    // Counts the times the server has placed this soldier: a spawn, a respawn, a
    // correction, so word from before a placing is never taken for word from after it.
    uint8_t life;
    uint8_t view_lag; // ticks behind the present its client shows the others; its shots inherit it
    // How it died, so any client can start the corpse from this state alone.
    Vec2 death_pos;
    Vec2 death_vel;
    uint8_t death_part;
    bool torn_apart; // killed or hit dead by a berserker: the body comes apart as a brutal death's does
    uint64_t rng;        // its own randomness (the spread of its shots), rolled where it is played
    uint32_t cmd_seq;    // the command it last ran: what its bullets are stamped with
    uint32_t shot_count; // bullets it has fired: each is stamped with its number

    // owned by the client that plays it
    Vec2 pos, old_pos;
    Vec2 vel, forces; // forces apply on the next integration step
    Vec2 next_push;   // knockback applied at the start of the next step
    Buttons controls; // this tick's resolved input
    Vec2 aim;
    int8_t direction; // 1 facing right, -1 left
    int8_t old_direction;
    // Left+Right held together keeps the previous direction: memory of the last tick.
    bool was_running_left;
    bool was_jumping;
    Stance stance;
    Anim legs, body;
    bool on_ground;
    bool on_ground_last, on_ground_permanent, on_ground_for_law;
    int32_t jets;
    BackgroundState bg;
    bool fired; // a shot went off this tick: the muzzle flash
    Weapon weapon;
    Weapon secondary;
    int32_t grenades;
    WeaponId grenade_type; // frag grenades, or the cluster grenades of a cluster kit
    int32_t burst_count;
    bool grenade_can_throw;
    bool can_auto_reload_spas;
    bool auto_reload_when_can_fire;
    // Distance from the muzzle to cover (a map collider, or a crouched teammate),
    // refreshed every 10 ticks; 255 = not near any. Crouching by cover raises the gun.
    uint8_t collider_distance;
    uint16_t hit_spray; // bink: aim disturbance from being hit, decaying one per tick
    bool spawn_still;   // not moved since spawning: the weapons menu still applies
    uint8_t stat;      // the stationary gun manned (thing index + 1)
    int16_t use_time;  // how hot the stationary guns it fires run: past the overheat they stop
    Idle idle;
    bool dont_drop; // a knife just thrown: the held drop key throws nothing more until released

    // the server's
    uint8_t held;                // the flag carried or the parachute hung from (thing index + 1)
    int32_t flag_grab_cooldown;  // a flag just thrown can't be grabbed back at once
    int32_t medikit_cooldown;    // a medikit just taken: no second one yet
    int32_t respawn_counter;
    int32_t cease_fire_counter; // spawn protection: no shooting, no wounds while >= 0
    float vest;
    Bonus bonus;
    int32_t bonus_time;
    WeaponId primary_choice; // the loadout for the next spawn
    WeaponId secondary_choice;
    int32_t kills, deaths, flags;
    PlayerLook look;
    // The player's, relayed by the server in the served half for the HUD alone; the
    // simulation reads neither: whether the player is typing, and its round trip in ms.
    bool typing;
    uint16_t ping;

    // This machine's alone, never on the wire: the soldier is heard of, not played
    // here, so its keys move it between words but fire nothing.
    bool remote;
} Soldier;

// ---------------------------------------------------------------------------------
// Bullets

// Things a bullet pushed recently: not again every tick.
typedef struct ThingCooldown {
    uint8_t thing; // index + 1, 0 for an empty slot
    uint32_t until;
} ThingCooldown;

typedef struct Bullet {
    bool active;
    BulletStyle style;
    WeaponId weapon;
    uint8_t owner;
    uint8_t lag;        // ticks behind the present it meets the soldiers, as its shooter saw them
    uint32_t spawn_cmd; // the command of its owner that fired it
    uint32_t shot_id;   // its owner's count of its bullets: the same number on every machine
    Vec2 pos, old_pos;
    Vec2 vel, forces;
    Vec2 initial;  // where it was fired: the damage falls off from here
    Vec2 hit_spot; // the last ricochet, so one surface deflects it once
    int32_t timeout;
    float hit_multiply;
    int8_t hit_body; // the soldier last hit, so a piercing bullet hits it once
    int32_t ricochet_count;
    int32_t degrade_count;
    ThingCooldown thing_cooldowns[4];
} Bullet;

// ---------------------------------------------------------------------------------
// Things: a small Verlet skeleton with a holder and a timeout. What each kind means
// lives in its own system.

typedef enum ThingStyle {
    THING_NONE,
    THING_ALPHA_FLAG,
    THING_BRAVO_FLAG,
    THING_MEDICAL_KIT,
    THING_GRENADE_KIT,
    THING_WEAPON,
    THING_FLAMER_KIT,
    THING_PREDATOR_KIT,
    THING_VEST_KIT,
    THING_BERSERK_KIT,
    THING_CLUSTER_KIT,
    THING_PARACHUTE,
    THING_STAT_GUN,
    THING_STYLE_COUNT
} ThingStyle;

typedef struct Thing {
    ThingStyle style; // THING_NONE: the slot is free
    WeaponId weapon;  // a dropped gun's
    int32_t ammo;
    bool flip;      // a dropped gun thrown facing left
    uint8_t holder; // soldier index + 1, 0 when loose
    uint8_t owner;  // who dropped or threw it, index + 1: it passes the polys its team does
    int32_t timeout;
    bool is_static; // at rest: no physics until something moves it
    int points;     // 2 or 4
    Vec2 pos[4];
    Vec2 old_pos[4];
    Vec2 forces[4];
    uint8_t collide_count[4]; // touches per point, for the landing sounds
    uint8_t cut;              // constraints let go of, from the last: a parachute's line once landed
    bool flipped;             // a parachute's canopy turned over this tick: its holder's fall catches
    bool in_base;             // a flag at home
    int32_t interest;         // how long the bots still go for it; a stationary gun's heat
    uint8_t last_spawn;       // the spawn point (1-based) a kit last came up at, not to be picked again
    BackgroundState bg;
} Thing;

// ---------------------------------------------------------------------------------
// Corpses: a dead soldier's gostek skeleton run as a Verlet particle system.

#define RAGDOLL_POINTS 24 // gostek.po

typedef struct Ragdoll {
    bool active;
    Vec2 pos[RAGDOLL_POINTS], old_pos[RAGDOLL_POINTS];
    Vec2 forces[RAGDOLL_POINTS];
    uint32_t torn;     // bit n: constraint n (0-based) no longer holds
    uint8_t hits;      // landings so far, which quiet the thud
    int32_t dead_time; // ticks since the body started, which dry the bleeding up
    bool on_ground;    // the last point checked touched the map: a parachute is let go of
} Ragdoll;

// ---------------------------------------------------------------------------------
// Events: what happened this tick, for whoever is listening: the client's sparks,
// sounds and messages, the server's wounds.

typedef struct EventFire { uint8_t player; WeaponId weapon; Vec2 pos, vel; } EventFire;
// A bullet asked for: by a soldier's weapon, a grenade, a punch, the map (lava, an
// exploding poly) or the stationary gun. The bullet pass makes it. `shot` is the
// owner's count of its bullets, taken when the shot is asked for, so the same numbers
// come out on every machine; `self` marks the mercy antic's, which leaves its shooter
// alone.
// `advance` is the wire's: ticks the bullet is run forward on being made, to sit where
// its shooter has it now; 0 for a shot asked for here.
typedef struct EventShot { uint8_t player; WeaponId weapon; Vec2 pos, vel; float damage; uint32_t shot; bool self; uint8_t advance; } EventShot;
typedef struct EventBulletSpawn { uint16_t id; uint8_t player; WeaponId weapon; Vec2 pos, vel; float damage; } EventBulletSpawn;
typedef struct EventBulletEnd { uint16_t id; uint8_t owner; uint32_t shot; WeaponId weapon; Vec2 pos; bool impact; } EventBulletEnd;
typedef struct EventWallHit { uint16_t id; uint8_t owner; WeaponId weapon; Vec2 pos, vel; } EventWallHit;
typedef struct EventRicochet { uint16_t id; uint8_t owner; Vec2 pos, vel; } EventRicochet;
typedef struct EventColliderHit { uint16_t id; uint8_t owner; Vec2 pos, vel; } EventColliderHit;
typedef struct EventGrenadeBounce { uint16_t id; uint8_t owner; Vec2 pos; } EventGrenadeBounce;
typedef struct EventClusterSplit { uint16_t id; uint8_t owner; Vec2 pos; } EventClusterSplit;
typedef struct EventBlood { uint8_t shooter, target; Vec2 pos, vel; } EventBlood;
typedef struct EventExplosion { uint16_t id; uint8_t player; WeaponId weapon; Vec2 pos; float radius; } EventExplosion;

// A bullet or blast of `shooter` wounded `target`: the damage computed, the skeleton
// part hit (0 for a blast), the point hit, and the knockback to give. Nothing has
// changed yet; the server applies it with damage_apply, a client only shows it. An
// amount of 0 is a shove alone (a blast over spawn protection); `spray` says the hit
// also disturbs the victim's aim, as bullets and blasts do. `impact` is the blow a death
// gives the gun it lets go of.
// `distance` (meters), `airtime` (ticks) and `ricochets` are the bullet's flight, for the
// killer's readout; 0 for a blast or a blade.
typedef struct Hit {
    uint8_t shooter, target; WeaponId weapon; float amount; uint8_t part; Vec2 pos, push, impact; bool spray;
    float distance; int32_t airtime; uint8_t ricochets;
} Hit;

typedef struct EventDamage { uint8_t attacker, target; WeaponId weapon; float amount; bool vest; } EventDamage;
// kills: the killer's tally now
typedef struct EventKill {
    uint8_t killer, target; WeaponId weapon; Vec2 pos; float health; uint8_t part; int32_t kills;
    float distance; int32_t airtime; uint8_t ricochets; // the shot's, as the Hit had them
} EventKill;
// The server placed a soldier: where, on which team, holding what, and the number of
// the life that begins. All its own client needs to begin it too.
typedef struct EventRespawn { uint8_t target; uint8_t life; Team team; WeaponId primary, secondary; Vec2 pos; } EventRespawn;
// The pickups carry the thing's index.
typedef struct EventFlagGrab { uint8_t player; uint8_t thing; ThingStyle flag; Vec2 pos; } EventFlagGrab;
typedef struct EventFlagReturn { uint8_t player; ThingStyle flag; Vec2 pos; } EventFlagReturn; // player 255: timed out
typedef struct EventFlagScore { uint8_t player; ThingStyle flag; Vec2 pos; } EventFlagScore;
typedef struct EventKitPickup { uint8_t player; uint8_t thing; ThingStyle kit; Vec2 pos; } EventKitPickup;
typedef struct EventWeaponPickup { uint8_t player; uint8_t thing; WeaponId weapon; int32_t ammo; Vec2 pos; } EventWeaponPickup;
// A gun left the hand: thrown on purpose, or let go of by a death, which gives it the
// blow that killed (`impact`). `pos` is the hand. The things pass lays it down.
typedef struct EventWeaponDrop { uint8_t player; WeaponId weapon; int32_t ammo; bool thrown; Vec2 pos, impact; } EventWeaponDrop;
// A thrown knife stopped in a wall, a collider or a body, and lies there to be taken.
typedef struct EventKnifeLand { uint8_t owner; Vec2 pos; } EventKnifeLand;
// The carrier asked to throw the flag (the key, while not rolling); the things pass
// throws it if it can.
typedef struct EventFlagThrow { uint8_t player; } EventFlagThrow;
typedef struct EventThingHit { ThingStyle thing; Vec2 pos, vel; uint8_t part; } EventThingHit;
// A bullet struck a thing: the things pass knocks point `part` of thing `thing` along
// the bullet's velocity, by the weapon's push.
typedef struct EventThingKnock { uint8_t thing, part; Vec2 vel; float push; } EventThingKnock;
// A hurting, lava, regenerating or exploding poly touched.
typedef struct EventPolyEffect { uint8_t target; PolyType type; Vec2 pos; bool spark; } EventPolyEffect;
// A corpse's point struck the map hard enough to be heard: how far it fell that tick,
// and how many times the body had already landed. Never leaves the machine that made it.
typedef struct EventCorpseHit { uint8_t target; Vec2 pos; float fall; uint8_t count; } EventCorpseHit;
typedef struct EventMatchEnd { Team winner; } EventMatchEnd;
typedef struct EventEchoTest { int n; } EventEchoTest; // the tests', to watch the passes' mail

typedef enum EventType {
    EVENT_FIRE,
    EVENT_SHOT,
    EVENT_BULLET_SPAWN,
    EVENT_BULLET_END,
    EVENT_WALL_HIT,
    EVENT_RICOCHET,
    EVENT_COLLIDER_HIT,
    EVENT_GRENADE_BOUNCE,
    EVENT_CLUSTER_SPLIT,
    EVENT_BLOOD,
    EVENT_EXPLOSION,
    EVENT_HIT,
    EVENT_DAMAGE,
    EVENT_KILL,
    EVENT_RESPAWN,
    EVENT_FLAG_GRAB,
    EVENT_FLAG_RETURN,
    EVENT_FLAG_SCORE,
    EVENT_KIT_PICKUP,
    EVENT_WEAPON_PICKUP,
    EVENT_WEAPON_DROP,
    EVENT_KNIFE_LAND,
    EVENT_FLAG_THROW,
    EVENT_THING_HIT,
    EVENT_THING_KNOCK,
    EVENT_POLY_EFFECT,
    EVENT_CORPSE_HIT,
    EVENT_MATCH_END,
    EVENT_ECHO_TEST,
} EventType;

typedef struct Event {
    EventType type;
    uint32_t tick; // when it happened; 0 for this tick. The wire sets it on what it heard.
    union {
        EventFire fire;
        EventShot shot;
        EventBulletSpawn bullet_spawn;
        EventBulletEnd bullet_end;
        EventWallHit wall_hit;
        EventRicochet ricochet;
        EventColliderHit collider_hit;
        EventGrenadeBounce grenade_bounce;
        EventClusterSplit cluster_split;
        EventBlood blood;
        EventExplosion explosion;
        Hit hit;
        EventDamage damage;
        EventKill kill;
        EventRespawn respawn;
        EventFlagGrab flag_grab;
        EventFlagReturn flag_return;
        EventFlagScore flag_score;
        EventKitPickup kit_pickup;
        EventWeaponPickup weapon_pickup;
        EventWeaponDrop weapon_drop;
        EventKnifeLand knife_land;
        EventFlagThrow flag_throw;
        EventThingHit thing_hit;
        EventThingKnock thing_knock;
        EventPolyEffect poly_effect;
        EventCorpseHit corpse_hit;
        EventMatchEnd match_end;
        EventEchoTest echo;
    };
} Event;

// The passes of a tick, in the order they run (world_step). Systems talk only through
// events: a pass consumes the events emitted since it last ran, which are the rest of
// the last tick's after it ran and this tick's before it began. So what a later pass
// asks of an earlier one happens on the next tick, as it does in the original, where
// a thing's bullet is made in the things' update and first flies the frame after.
typedef enum Pass {
    PASS_SOLDIERS,
    PASS_CORPSES,
    PASS_BULLETS,
    PASS_WOUNDS,
    PASS_THINGS,
    PASS_RECEIPTS, // the soldiers take what the things gave them
    PASS_COUNT,
} Pass;

typedef struct Events {
    Event items[MAX_EVENTS];
    int count;
    int passed[PASS_COUNT]; // how many events each pass had seen when it began
} Events;

// ---------------------------------------------------------------------------------
// History: where everyone was over the last second, kept on the server so a shot is
// judged against the soldiers as its shooter saw them.

typedef struct History {
    Soldier frames[HISTORY_TICKS][MAX_PLAYERS]; // by tick modulo the ring
    Thing things[HISTORY_TICKS][MAX_THINGS];    // and the things, for the snapshots' deltas
    uint32_t tick; // the newest frame's
    uint32_t count;
} History;
