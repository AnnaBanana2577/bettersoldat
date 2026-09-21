#pragma once

// What a frame draws, built from two ticks. The renderer reads a RenderState and nothing
// else of the game, so every choice about where things appear between ticks is made
// here, in build_render_state:
//
//   the game ticks  ->  TickSnapshot (after every tick)
//   each frame      ->  build_render_state(previous, latest, alpha)  ->  RenderState  ->  render_draw
//
// Today the two snapshots are the local world's last two ticks. With a server they
// will be the two server snapshots the client is drawing between; nothing past this
// file needs to change for that.

#include "game/game.h"

// The part of one tick the picture is made from.
typedef struct TickSnapshot {
    uint32_t tick;
    Soldier soldiers[MAX_PLAYERS];
} TickSnapshot;

// One soldier as the gostek draws it.
typedef struct RenderSoldier {
    bool active;
    bool dead;
    Team team;
    Pose pose; // the skeleton, already placed in the world
    Vec2 pos;  // the body, where the frame shows it
    bool facing_left;
    WeaponId weapon;    // in the hands
    Weapon gun;         // its ammo, and the ticks of firing and reloading left: the HUD's bars
    int jets;           // fuel left
    WeaponId secondary; // slung across the back
    AnimId body_anim;
    int grenades;
    float health;
    float vest;
    bool jetting;
    bool fired;           // a shot went off on the latest tick: the muzzle flash
    bool spawn_protected; // drawn faded
} RenderSoldier;

typedef struct RenderState {
    float alpha; // how far between the two ticks this frame is, 0..1
    RenderSoldier soldiers[MAX_PLAYERS];
    Vec2 focus; // what the camera follows: the local player's body
} RenderState;

// The world's soldiers as they stand after a tick.
void tick_snapshot_capture(TickSnapshot *snap, const World *w);

// The frame `alpha` of the way from `from` to `to`. Positions blend between the two;
// everything discrete (animation frames, weapons, health) is the latest tick's. A
// soldier placed anew between the two (spawned, respawned, corrected) is drawn where
// it now is rather than slid there. `me` is the soldier the camera follows.
void build_render_state(RenderState *out, const Context *ctx, const TickSnapshot *from, const TickSnapshot *to,
                        float alpha, int me);
