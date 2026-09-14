#include "input/input.h"

typedef struct Bind {
    SDL_Scancode key;
    Button button;
} Bind;

// Soldat's defaults, by physical key as the original binds; binds from the config come
// later.
static const Bind BINDS[] = {
    {SDL_SCANCODE_A, BUTTON_LEFT},   {SDL_SCANCODE_D, BUTTON_RIGHT},    {SDL_SCANCODE_W, BUTTON_JUMP},
    {SDL_SCANCODE_S, BUTTON_CROUCH}, {SDL_SCANCODE_X, BUTTON_PRONE},    {SDL_SCANCODE_SPACE, BUTTON_JET},
    {SDL_SCANCODE_E, BUTTON_THROW},  {SDL_SCANCODE_R, BUTTON_RELOAD},   {SDL_SCANCODE_Q, BUTTON_CHANGE},
    {SDL_SCANCODE_F, BUTTON_DROP},   {SDL_SCANCODE_K, BUTTON_SUICIDE},
};

void input_init(Input *in, Vec2 view)
{
    *in = (Input){.view = view, .cursor = vec2_scale(view, 0.5f), .sensitivity = 1.0f};
    SDL_SetRelativeMouseMode(SDL_TRUE); // the original's StartInput
}

void input_resize(Input *in, Vec2 view)
{
    Vec2 share = {in->cursor.x / in->view.x, in->cursor.y / in->view.y};
    in->view = view;
    in->cursor = vec2_mul(share, view);
}

void input_mouse_motion(Input *in, const SDL_MouseMotionEvent *motion)
{
    in->cursor.x = clampf(in->cursor.x + (float)motion->xrel * in->sensitivity, 0.0f, in->view.x);
    in->cursor.y = clampf(in->cursor.y + (float)motion->yrel * in->sensitivity, 0.0f, in->view.y);
}

void input_sample(Input *in, Vec2 aim)
{
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    Buttons held = 0;
    for (size_t i = 0; i < sizeof(BINDS) / sizeof(BINDS[0]); i++) {
        if (keys[BINDS[i].key]) held |= (Buttons)BINDS[i].button;
    }
    Uint32 mouse = SDL_GetMouseState(NULL, NULL);
    if (mouse & SDL_BUTTON(SDL_BUTTON_LEFT)) held |= BUTTON_FIRE;
    if (mouse & SDL_BUTTON(SDL_BUTTON_RIGHT)) held |= BUTTON_THROW;

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
