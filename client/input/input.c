#include "input/input.h"

#include <raylib.h>

typedef struct Bind {
    KeyboardKey key;
    Button button;
} Bind;

// Soldat's defaults; binds from the config come later.
static const Bind BINDS[] = {
    {KEY_A, BUTTON_LEFT},  {KEY_D, BUTTON_RIGHT},  {KEY_W, BUTTON_JUMP},   {KEY_S, BUTTON_CROUCH},
    {KEY_X, BUTTON_PRONE}, {KEY_SPACE, BUTTON_JET}, {KEY_E, BUTTON_THROW},  {KEY_R, BUTTON_RELOAD},
    {KEY_Q, BUTTON_CHANGE}, {KEY_F, BUTTON_DROP},  {KEY_K, BUTTON_SUICIDE},
};

void input_sample(Input *in, Vec2 aim)
{
    Buttons held = 0;
    for (size_t i = 0; i < sizeof(BINDS) / sizeof(BINDS[0]); i++) {
        if (IsKeyDown(BINDS[i].key)) held |= (Buttons)BINDS[i].button;
    }
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) held |= BUTTON_FIRE;
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) held |= BUTTON_THROW;

    // a one-shot button counts from the frame it goes down until a tick consumes it
    in->pressed |= (Buttons)(held & ~in->held & BUTTONS_ONE_SHOT);
    in->held = held;
    in->aim = aim;
}

Command input_command(const Input *in, uint32_t seq)
{
    return (Command){.seq = seq, .buttons = (Buttons)(in->held | in->pressed), .aim = in->aim};
}

void input_clear(Input *in)
{
    in->pressed = 0;
}
