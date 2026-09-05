#pragma once

// Keyframe animations (.poa) and their playback. The soldier's movement state machine
// is driven by the legs and body animation ids and frame numbers, so this is gameplay
// data, not just visuals. Frame numbers are 1-based to keep the original's tuning
// constants readable. Ported from Anims.pas by way of soldat-odin.

#include "utils/utils.h"

#define MAX_ANIM_FRAMES 40
#define MAX_ANIM_POINTS 20

// A skeleton pose: where each of the gostek's points is. 0-based; point n in the
// original data is index n-1.
#define POSE_POINTS MAX_ANIM_POINTS
typedef struct Pose {
    Vec2 p[POSE_POINTS];
} Pose;

typedef enum AnimId {
    ANIM_STAND,
    ANIM_RUN,
    ANIM_RUN_BACK,
    ANIM_JUMP,
    ANIM_JUMP_SIDE,
    ANIM_FALL,
    ANIM_CROUCH,
    ANIM_CROUCH_RUN,
    ANIM_RELOAD,
    ANIM_THROW,
    ANIM_RECOIL,
    ANIM_SMALL_RECOIL,
    ANIM_SHOTGUN,
    ANIM_CLIP_OUT,
    ANIM_CLIP_IN,
    ANIM_SLIDE_BACK,
    ANIM_CHANGE,
    ANIM_THROW_WEAPON,
    ANIM_WEAPON_NONE,
    ANIM_PUNCH,
    ANIM_RELOAD_BOW,
    ANIM_BARRET,
    ANIM_ROLL,
    ANIM_ROLL_BACK,
    ANIM_CROUCH_RUN_BACK,
    ANIM_CIGAR,
    ANIM_MATCH,
    ANIM_SMOKE,
    ANIM_WIPE,
    ANIM_GROIN,
    ANIM_PISS,
    ANIM_MERCY,
    ANIM_MERCY2,
    ANIM_TAKE_OFF,
    ANIM_PRONE,
    ANIM_VICTORY,
    ANIM_AIM,
    ANIM_HANDS_UP_AIM,
    ANIM_PRONE_MOVE,
    ANIM_GET_UP,
    ANIM_AIM_RECOIL,
    ANIM_HANDS_UP_RECOIL,
    ANIM_MELEE,
    ANIM_OWN,
    ANIM_COUNT
} AnimId;

// Where an animation's keyframes live and how it plays.
typedef struct AnimInfo {
    const char *file;
    int32_t speed;
    bool loop;
} AnimInfo;

extern const AnimInfo ANIM_INFO[ANIM_COUNT];

typedef struct AnimData {
    Vec2 frames[MAX_ANIM_FRAMES][MAX_ANIM_POINTS];
    int32_t num_frames;
    int32_t speed;
    bool loop;
} AnimData;

// Every animation, indexed by AnimId. Large: allocate it once and share it.
typedef struct Anims {
    AnimData data[ANIM_COUNT];
} Anims;

// Per-soldier playback state: simulated and networked.
typedef struct Anim {
    AnimId id;
    int32_t frame; // 1-based
    int32_t count;
    int32_t speed;
} Anim;

// Parses a .poa keyframe file (text is modified), keeping speed and loop from info.
void anim_parse(AnimData *anim, AnimInfo info, char *text);

// Every animation from <base_dir>/anims. NULL on failure (reported on stderr).
// Free with free().
Anims *anims_load(const char *base_dir);

// The keyframe points of an animation's current frame.
const Vec2 *anim_frame(const Anims *anims, Anim a);

void anim_advance(const Anims *anims, Anim *a);

// Switches animation unconditionally, at frame 1 unless given (pass 1 for the start).
void anim_set(const Anims *anims, Anim *a, AnimId id, int32_t frame);

// Switches animation only if it isn't already playing.
void anim_apply(const Anims *anims, Anim *a, AnimId id, int32_t frame);

static inline int32_t anim_frames(const Anims *anims, AnimId id) { return anims->data[id].num_frames; }
