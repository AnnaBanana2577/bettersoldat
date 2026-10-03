#pragma once

// What a frame draws, built from two ticks. The renderer reads a RenderState and nothing
// else of the game, so every choice about where things appear between ticks is made
// here, in build_render_state:
//
//   the game ticks  ->  TickSnapshot (after every tick)
//   each frame      ->  build_render_state(previous, latest, alpha)  ->  RenderState  ->  render_draw
//
// The two snapshots are the world's last two ticks, alone or online alike. The
// bullets, the things and the corpses ride the snapshot too, and are drawn between
// their own last two positions, which the simulation keeps.

#include "game/game.h"

// The part of one tick the picture is made from.
typedef struct TickSnapshot {
    uint32_t tick;
    Soldier soldiers[MAX_PLAYERS];
    Ragdoll ragdolls[MAX_PLAYERS]; // the corpses
    Bullet bullets[MAX_BULLETS];
    Thing things[MAX_THINGS];
} TickSnapshot;

// One soldier as the gostek draws it.
typedef struct RenderSoldier {
    bool active;
    bool dead;
    bool corpse; // dead with its body started: the pose is the ragdoll's
    Team team;
    Pose pose; // the skeleton, already placed in the world
    Vec2 pos;  // the body, where the frame shows it
    bool facing_left;
    WeaponId weapon;    // in the hands
    Weapon gun;         // its ammo, and the ticks of firing and reloading left: the HUD's bars
    int jets;           // fuel left
    int hit_spray;      // the bink from being hit, and the aim's wobble from moving: the crosshair grows
    float move_acc;
    float aim_dist;     // the sniper view: the camera's lead toward the aim (camera.h)
    WeaponId secondary; // slung across the back
    AnimId body_anim;
    int grenades;
    float health;
    float vest;
    bool jetting;
    bool fired;           // a shot went off on the latest tick: the muzzle flash
    bool spawn_protected; // drawn faded
    PlayerLook look;      // its colours, hair, headgear and chain
    Vec2 swing[4];        // the chain's and the hair's points 21 to 24, where the frame shows them
    int body_frame;       // the body animation's frame: the headgear is in the hand past a wipe's fourth
    uint8_t has_cigar;    // the cigar in the mouth: 5 unlit, 10 lit
    uint8_t wear_helmet;  // 1 on the head; anything else bares the hair
    RopePhase rope;       // anything but none draws the line below
    Vec2 rope_tip;        // the rope's far end: growing, or anchored
    uint8_t rope_wraps_count;      // the corners the rope is caught around
    Vec2 rope_wraps[ROPE_WRAPS];   // each, from the anchor out to its owner
    Gear gear;            // jets, or a rope: the flame only burns the first
} RenderSoldier;

typedef struct RenderState {
    float alpha; // how far between the two ticks this frame is, 0..1
    RenderSoldier soldiers[MAX_PLAYERS];
    Vec2 focus; // what the camera follows: the local player's body
    const Bullet *bullets; // the latest tick's, drawn alpha of the way from their last positions
    const Thing *things;
} RenderState;

// The world's soldiers as they stand after a tick.
void tick_snapshot_capture(TickSnapshot *snap, const World *w);

// The team's shirt, worn over the player's own in a team game, and the roster's colour.
Rgba team_shirt(Team team);

// The frame `alpha` of the way from `from` to `to`. Positions and poses blend between the
// two, the pose across a change of animation too, as the original's skeleton does;
// everything discrete (animation frames, weapons, health) is the latest tick's. A
// soldier placed anew between the two (spawned, respawned, corrected) is drawn where
// it now is rather than slid there. `me` is the soldier the camera follows. In a
// `team_game` the shirt drawn is the team's. `offsets`, if given, moves each soldier's
// picture from where the simulation has it: what a correction from the wire moved it
// by, still being smoothed out (stream.h).
void build_render_state(RenderState *out, const Context *ctx, const TickSnapshot *from, const TickSnapshot *to,
                        float alpha, int me, bool team_game, const Vec2 *offsets);
