#pragma once

// Input is the keys and mouse, run through the console's binds. Every key has a name
// ("a", "space", "f1", "mouse1", "mwheelup"...), and a key going down or up runs its
// bind (console_key). The soldier's buttons are commands for the binds to run:
//
//   +left +right +jump +crouch +prone +jet +fire +throw +reload +change +drop +suicide
//   +flagthrow
//
// A button is held while any key bound to it is down. One-shot presses (throw, change,
// prone, drop) are latched the moment they go down and cleared by input_clear after the
// tick that used them, so a press between two ticks is never lost and never counted
// twice. Keys are named by their place on the keyboard (SDL's scancodes), as the
// original binds them, so "w" is the key above "s" on any layout.
//
// A key pressed with a modifier held is looked up as "alt+w", "ctrl+w" or "shift+w"
// first, and as "w" when that has no bind, so Alt+W can say something while W alone
// still jumps. A key goes up under the name it went down with, so what a +cmd began, the
// release always ends. The modifier keys are keys too: "alt" itself can hold the radio
// menu open.
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

#include "console/console.h"
#include "game/entities.h"

typedef struct Input {
    Buttons held;
    Buttons pressed;
    uint8_t down[16];  // for each bit of Buttons, how many of the keys bound to it are down
    Vec2 aim;          // the cursor in world space
    Vec2 cursor;       // the game's cursor, in view units from the view's top-left
    Vec2 view;         // the view's size in those units: GAME_HEIGHT tall, the width follows the window
    float sensitivity; // the original's cl_sensitivity

    // For each key that is down, the modifier it went down with (0 none, else an index
    // into the modifiers' names), so it goes up under the same name.
    uint8_t down_with[SDL_NUM_SCANCODES];
} Input;

// Registers the buttons' commands, which press and release `in`.
void input_init(Input *in, Console *con);

// Binds the keys to Soldat's defaults. A saved config, exec'd after, replaces them.
void input_default_binds(Console *con);

// Once the window is up: locks the mouse to it and puts the cursor in the middle of a
// view this size (the original's StartInput).
void input_start(Input *in, Vec2 view);

// The view's size changed with the window: the cursor keeps its place in it.
void input_resize(Input *in, Vec2 view);

// A mouse motion event: the deltas move the cursor.
void input_mouse_motion(Input *in, const SDL_MouseMotionEvent *motion);

// A key, mouse button or wheel event: names the key and runs its bind. Key repeats do
// nothing. False for any other event.
bool input_event(Input *in, Console *con, const SDL_Event *e);

// Takes `aim`, the cursor in world space, for this frame's commands.
void input_sample(Input *in, Vec2 aim);

// The command for this tick, numbered: the server runs them in order and says which it
// has run, and the client replays the rest. Held buttons and this tick's presses.
Command input_command(const Input *in, uint32_t seq);

void input_clear(Input *in);
