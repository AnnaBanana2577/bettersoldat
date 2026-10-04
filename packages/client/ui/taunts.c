#include "taunts.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// The taunt keys, as the editor's keyboard shows them: the number row, then the
// qwerty rows, the original's Alt+1..0 and Alt+Q..P.
const char *const TAUNT_SLOT_KEYS[TAUNT_SLOTS] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p",
    "a", "s", "d", "f", "g", "h", "j", "k", "l",
    "z", "x", "c", "v", "b", "n", "m",
};

// The modifiers a taunt can be bound with, as the input names them
// (client/input/input.c's MODIFIERS).
const char *const TAUNT_MOD_KEYS[TAUNT_MODS] = {"alt", "ctrl", "shift"};

void taunt_combo(char *out, size_t size, int slot, int mod)
{
    snprintf(out, size, "%s+%s", TAUNT_MOD_KEYS[mod], TAUNT_SLOT_KEYS[slot]);
}

// A word of `text` beginning at `at`, matched against `word` without regard to
// case; the text after it, or NULL. Console commands are matched so.
static const char *word_eq(const char *at, const char *word)
{
    for (; *word; at++, word++)
        if (tolower((unsigned char)*at) != tolower((unsigned char)*word)) return NULL;
    return at;
}

// A radio call's or place's digit, 1..3 (the radio has three of each), at `at`
// after its spaces; the text after the digit, or NULL.
static const char *radio_digit(const char *at, int *digit)
{
    while (*at == ' ') at++;
    if (*at < '1' || *at > '3') return NULL;
    *digit = *at - '0';
    return at + 1;
}

bool taunt_at(const Console *con, int slot, Taunt *out)
{
    for (int mod = 0; mod < TAUNT_MODS; mod++) {
        char combo[CONSOLE_NAME_SIZE];
        taunt_combo(combo, sizeof combo, slot, mod);
        const char *text = console_bind_get(con, combo);
        if (!text) continue;

        Taunt t = {.slot = slot, .mod = mod, .mode = TAUNT_CHAT, .radio = 0};
        snprintf(t.combo, sizeof t.combo, "%s", combo);

        // A radio taunt: radio <call> <place> <words>, the words said as that call.
        const char *p = word_eq(text, "radio");
        if (p) {
            int call = 0, place = 0;
            if ((p = radio_digit(p, &call)) && (p = radio_digit(p, &place)) && (*p == ' ' || *p == '\0')) {
                while (*p == ' ') p++;
                t.mode = TAUNT_TEAM; // the call goes to the team
                t.radio = (call - 1) * 3 + place;
                snprintf(t.text, sizeof t.text, "%s", p);
                *out = t;
                return true;
            }
        }

        // A said one: `say` to everyone, `say_team` to the team, then the message.
        const char *message = NULL;
        p = word_eq(text, "say_team");
        if (p && (*p == ' ' || *p == '\0')) {
            while (*p == ' ') p++;
            message = p;
            t.mode = TAUNT_TEAM;
        } else {
            p = word_eq(text, "say");
            if (p && (*p == ' ' || *p == '\0')) {
                while (*p == ' ') p++;
                message = p;
                t.mode = TAUNT_CHAT;
            }
        }
        if (!message) continue; // bound, but to no taunt: the keys page owns it
        snprintf(t.text, sizeof t.text, "%s", message);
        *out = t;
        return true;
    }
    return false;
}

void taunt_compose(char *out, size_t size, TauntMode mode, const char *text, int radio)
{
    // The message as a console line can carry it: no " or ; (a quoted word and a
    // command end there), and // would comment the rest of the line away, so it
    // becomes a space. Words stay readable for it.
    char clean[CONSOLE_VALUE_SIZE];
    size_t n = 0;
    for (const char *p = text; *p && n + 1 < sizeof clean; p++) {
        if (*p == '"' || *p == ';') continue;
        if (p[0] == '/' && p[1] == '/') {
            clean[n++] = ' ';
            p++;
            continue;
        }
        clean[n++] = *p;
    }
    clean[n] = '\0';

    if (radio >= 1 && radio <= 9) {
        // The message as the call: radio <call> <place> <words>, said to the team
        // with the call's sound — one message, not the two the old suffix sent.
        int call = (radio - 1) / 3 + 1, place = (radio - 1) % 3 + 1;
        snprintf(out, size, "radio %d %d %s", call, place, clean);
    } else if (mode == TAUNT_TEAM) {
        snprintf(out, size, "say_team %s", clean);
    } else {
        snprintf(out, size, "say %s", clean);
    }
}

void taunt_set(Console *con, int slot, int mod, const char *text)
{
    for (int m = 0; m < TAUNT_MODS; m++) {
        char combo[CONSOLE_NAME_SIZE];
        taunt_combo(combo, sizeof combo, slot, m);
        console_bind(con, combo, m == mod ? text : ""); // empty text unbinds
    }
}
