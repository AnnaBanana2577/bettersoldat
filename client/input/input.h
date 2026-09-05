#pragma once

// Input is sampled every frame and consumed every tick. Held buttons are whatever the
// keys say right now; one-shot presses (throw, change, prone, drop) are latched the
// frame they go down and cleared by input_clear after the tick that used them, so a
// press between two ticks is never lost and never counted twice.
//
// The default binds, Soldat's:
//   A / D  left / right    W  jump    S  crouch    X  prone    Space  jet
//   left mouse  fire    right mouse / E  throw    R  reload    Q  change    F  drop
//   K  suicide

#include "game/entities.h"

typedef struct Input {
    Buttons held;
    Buttons pressed;
    Vec2 aim; // the cursor in world space
} Input;

// The keys and mouse now, and `aim`, the cursor in world space.
void input_sample(Input *in, Vec2 aim);

// The command for this tick, numbered: the server runs them in order and says which it
// has run, and the client replays the rest. Held buttons and this tick's presses.
Command input_command(const Input *in, uint32_t seq);

void input_clear(Input *in);
