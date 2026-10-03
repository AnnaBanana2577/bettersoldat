#include "input/input.h"

#include <stdio.h>
#include <string.h>

static const struct {
    const char *name;
    Button button;
} BUTTONS[] = {
    {"left", BUTTON_LEFT},       {"right", BUTTON_RIGHT},   {"jump", BUTTON_JUMP},   {"crouch", BUTTON_CROUCH},
    {"prone", BUTTON_PRONE},     {"jet", BUTTON_JET},       {"fire", BUTTON_FIRE},   {"throw", BUTTON_THROW},
    {"reload", BUTTON_RELOAD},   {"change", BUTTON_CHANGE}, {"drop", BUTTON_DROP},
    {"flagthrow", BUTTON_FLAG_THROW},
};

#define BUTTON_COUNT (int)(sizeof BUTTONS / sizeof BUTTONS[0])

// The keys with names that aren't their letter, digit or F-number.
static const struct {
    SDL_Scancode key;
    const char *name;
} KEYS[] = {
    {SDL_SCANCODE_SPACE, "space"},         {SDL_SCANCODE_ESCAPE, "escape"},       {SDL_SCANCODE_RETURN, "enter"},
    {SDL_SCANCODE_KP_ENTER, "kp_enter"},   {SDL_SCANCODE_TAB, "tab"},             {SDL_SCANCODE_BACKSPACE, "backspace"},
    {SDL_SCANCODE_GRAVE, "grave"},         {SDL_SCANCODE_UP, "uparrow"},          {SDL_SCANCODE_DOWN, "downarrow"},
    {SDL_SCANCODE_LEFT, "leftarrow"},      {SDL_SCANCODE_RIGHT, "rightarrow"},    {SDL_SCANCODE_LSHIFT, "shift"},
    {SDL_SCANCODE_RSHIFT, "rshift"},       {SDL_SCANCODE_LCTRL, "ctrl"},          {SDL_SCANCODE_RCTRL, "rctrl"},
    {SDL_SCANCODE_LALT, "alt"},            {SDL_SCANCODE_RALT, "ralt"},           {SDL_SCANCODE_INSERT, "ins"},
    {SDL_SCANCODE_DELETE, "del"},          {SDL_SCANCODE_HOME, "home"},           {SDL_SCANCODE_END, "end"},
    {SDL_SCANCODE_PAGEUP, "pgup"},         {SDL_SCANCODE_PAGEDOWN, "pgdn"},       {SDL_SCANCODE_CAPSLOCK, "capslock"},
    {SDL_SCANCODE_MINUS, "minus"},         {SDL_SCANCODE_EQUALS, "equals"},       {SDL_SCANCODE_LEFTBRACKET, "leftbracket"},
    {SDL_SCANCODE_RIGHTBRACKET, "rightbracket"}, {SDL_SCANCODE_BACKSLASH, "backslash"}, {SDL_SCANCODE_SEMICOLON, "semicolon"},
    {SDL_SCANCODE_APOSTROPHE, "apostrophe"}, {SDL_SCANCODE_COMMA, "comma"},       {SDL_SCANCODE_PERIOD, "period"},
    {SDL_SCANCODE_SLASH, "slash"},
};

static const char *DEFAULT_BINDS = "bind a +left; bind d +right; bind w +jump; bind s +crouch; bind x +prone;"
                                   "bind space +jet; bind mouse1 +fire; bind mouse2 +throw; bind e +throw;"
                                   "bind r +reload; bind q +change; bind f +drop; bind k \"say /kill\"";

// "+name" presses a button, "-name" releases it.
static void button_command(Console *con, int argc, char **argv, void *user)
{
    (void)con, (void)argc;
    Input *in = user;
    int i = 0;
    while (i < BUTTON_COUNT && strcmp(BUTTONS[i].name, argv[0] + 1) != 0) i++;
    if (i == BUTTON_COUNT) return;

    Button button = BUTTONS[i].button;
    int bit = 0;
    while (!(button >> bit & 1)) bit++;

    if (argv[0][0] == '+') {
        if (in->down[bit]++ == 0) {
            in->held |= (Buttons)button;
            in->pressed |= (Buttons)(button & BUTTONS_ONE_SHOT);
        }
    } else if (in->down[bit] > 0 && --in->down[bit] == 0) {
        in->held &= (Buttons)~button;
    }
}

void input_init(Input *in, Console *con)
{
    *in = (Input){.sensitivity = 1.0f};
    for (int i = 0; i < BUTTON_COUNT; i++) {
        char name[CONSOLE_NAME_SIZE];
        snprintf(name, sizeof name, "+%s", BUTTONS[i].name);
        console_add_command(con, name, button_command, in, NULL);
        name[0] = '-';
        console_add_command(con, name, button_command, in, NULL);
    }
}

void input_default_binds(Console *con) { console_execute(con, DEFAULT_BINDS); }

void input_start(Input *in, Vec2 view)
{
    in->view = view;
    in->cursor = vec2_scale(view, 0.5f);
    SDL_SetRelativeMouseMode(SDL_TRUE);
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

// The name of a key, by its place on the keyboard; NULL for one with no name.
static const char *key_name(SDL_Scancode key, char *buf, size_t size)
{
    if (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_Z) {
        snprintf(buf, size, "%c", 'a' + (key - SDL_SCANCODE_A));
        return buf;
    }
    if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_0) {
        snprintf(buf, size, "%c", key == SDL_SCANCODE_0 ? '0' : '1' + (key - SDL_SCANCODE_1));
        return buf;
    }
    if (key >= SDL_SCANCODE_F1 && key <= SDL_SCANCODE_F12) {
        snprintf(buf, size, "f%d", 1 + (key - SDL_SCANCODE_F1));
        return buf;
    }
    for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
        if (KEYS[i].key == key) return KEYS[i].name;
    return NULL;
}

// mouse1 left, mouse2 right, mouse3 middle, then the side buttons: Quake's order, not
// SDL's.
static int mouse_number(Uint8 button)
{
    switch (button) {
    case SDL_BUTTON_LEFT: return 1;
    case SDL_BUTTON_RIGHT: return 2;
    case SDL_BUTTON_MIDDLE: return 3;
    default: return button;
    }
}

// The modifiers a key can be bound with, by the index kept in Input.down_with.
static const struct {
    Uint16 mod;
    const char *name;
} MODIFIERS[] = {{0, NULL}, {KMOD_ALT, "alt"}, {KMOD_CTRL, "ctrl"}, {KMOD_SHIFT, "shift"}};

// A key event's name: with a modifier held, "alt+w" if that is bound, "w" otherwise;
// on the way up, whichever it went down with.
static const char *key_event_name(Input *in, const Console *con, const SDL_KeyboardEvent *k, char *buf, size_t size)
{
    char plain[16];
    const char *name = key_name(k->keysym.scancode, plain, sizeof plain);
    if (!name) return NULL;

    uint8_t with = 0;
    if (k->type == SDL_KEYDOWN) {
        for (uint8_t i = 1; i < sizeof MODIFIERS / sizeof MODIFIERS[0] && !with; i++) {
            if (!(k->keysym.mod & MODIFIERS[i].mod)) continue;
            snprintf(buf, size, "%s+%s", MODIFIERS[i].name, name);
            if (console_bind_get(con, buf)) with = i;
        }
        in->down_with[k->keysym.scancode] = with;
    } else {
        with = in->down_with[k->keysym.scancode];
        in->down_with[k->keysym.scancode] = 0;
    }
    if (with) snprintf(buf, size, "%s+%s", MODIFIERS[with].name, name);
    else snprintf(buf, size, "%s", name);
    return buf;
}

bool input_event(Input *in, Console *con, const SDL_Event *e)
{
    char buf[32];
    switch (e->type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        if (e->key.repeat) return true;
        const char *name = key_event_name(in, con, &e->key, buf, sizeof buf);
        if (name) console_key(con, name, e->type == SDL_KEYDOWN);
        return true;
    }
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        snprintf(buf, sizeof buf, "mouse%d", mouse_number(e->button.button));
        console_key(con, buf, e->type == SDL_MOUSEBUTTONDOWN);
        return true;
    case SDL_MOUSEWHEEL: {
        // The wheel has no keys to hold: each notch is a press and a release.
        const char *name = e->wheel.y > 0 ? "mwheelup" : e->wheel.y < 0 ? "mwheeldown" : NULL;
        if (name) {
            console_key(con, name, true);
            console_key(con, name, false);
        }
        return true;
    }
    default: return false;
    }
}

void input_sample(Input *in, Vec2 aim) { in->aim = aim; }

// Jump and crouch held together throw the flag too, as the original's LocalInput has it.
Command input_command(const Input *in, uint32_t seq)
{
    Buttons buttons = (Buttons)(in->held | in->pressed);
    if ((buttons & (BUTTON_JUMP | BUTTON_CROUCH)) == (BUTTON_JUMP | BUTTON_CROUCH)) buttons |= BUTTON_FLAG_THROW;
    return (Command){.seq = seq, .buttons = buttons, .aim = in->aim};
}

void input_clear(Input *in)
{
    in->pressed = 0;
}

void input_release_all(Input *in)
{
    in->held = 0;
    in->pressed = 0;
    memset(in->down, 0, sizeof in->down);
    memset(in->down_with, 0, sizeof in->down_with);
}

bool input_event_key_name(const SDL_Event *e, char *buf, size_t size)
{
    switch (e->type) {
    case SDL_KEYDOWN: { // into `buf` whichever way it is named: a table's name comes back as itself, unwritten
        const char *name = key_name(e->key.keysym.scancode, buf, size);
        if (!name) return false;
        if (name != buf) snprintf(buf, size, "%s", name);
        return true;
    }
    case SDL_MOUSEBUTTONDOWN: snprintf(buf, size, "mouse%d", mouse_number(e->button.button)); return true;
    case SDL_MOUSEWHEEL:
        if (e->wheel.y == 0) return false;
        snprintf(buf, size, "%s", e->wheel.y > 0 ? "mwheelup" : "mwheeldown");
        return true;
    default: return false;
    }
}
