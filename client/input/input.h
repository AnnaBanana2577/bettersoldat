#pragma once

// Input is sampled every frame and consumed every tick. Held buttons are whatever the
// keys say right now; one-shot presses (throw, change, prone, drop) are latched the
// frame they go down and cleared by input_clear after the tick that used them, so a
// press between two ticks is never lost and never counted twice.
//
// The mouse is the original's: SDL's relative mode locks the system cursor in the
// window and the game keeps its own, moved by the raw deltas times the sensitivity and
// clamped to the view, in the view's units (480 tall, however big the window), so it
// feels the same at any window size (opensoldat's ControlGame.pas, SDL_MOUSEMOTION).
//
// The default binds, Soldat's:
//   A / D  left / right    W  jump    S  crouch    X  prone    Space  jet
//   left mouse  fire    right mouse / E  throw    R  reload    Q  change    F  drop
//   K  suicide

#include <SDL.h>

#include "game/entities.h"

typedef struct Input {
    Buttons held;
    Buttons pressed;
    Vec2 aim;          // the cursor in world space
    Vec2 cursor;       // the game's cursor, in view units from the view's top-left
    Vec2 view;         // the view's size in those units: GAME_HEIGHT tall, the width follows the window
    float sensitivity; // the original's cl_sensitivity
} Input;

// Locks the mouse to the window and puts the cursor in the middle of a view this size.
void input_init(Input *in, Vec2 view);

// The view's size changed with the window: the cursor keeps its place in it.
void input_resize(Input *in, Vec2 view);

// A mouse motion event: the deltas move the cursor.
void input_mouse_motion(Input *in, const SDL_MouseMotionEvent *motion);

// The keys and mouse now, and `aim`, the cursor in world space.
void input_sample(Input *in, Vec2 aim);

// The command for this tick, numbered: the server runs them in order and says which it
// has run, and the client replays the rest. Held buttons and this tick's presses.
Command input_command(const Input *in, uint32_t seq);

void input_clear(Input *in);
