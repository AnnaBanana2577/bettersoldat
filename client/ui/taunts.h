#pragma once

// The taunts, as binds on the console: a modifier and a key, the message said when
// they are pressed. `bind alt+q "say_team Cover me!"` is a taunt, and so is one
// whose text runs as a radio call: `bind alt+1 "radio 1 2 Base!"` says "Base!" to
// the team as the "Enemy flagger, middle!" call, with its sound — one message, the
// call's own words replaced by the taunt's. The taunt editor (the main menu's
// Taunts page) reads and writes them here, so this is pure logic over the console,
// with no SDL, for the tests (tests/taunts_test.c).
//
// The keys are the editor's keyboard: the number row, then the qwerty rows, 36 of
// them. A slot holds one taunt, which taunt_set keeps to, unbinding the slot's
// other modifier combos. The message loses what a console line can't carry: `"`
// ends a quoted word, `;` ends a command, and `//` comments the rest of the line
// away (it becomes a space).

#include "console/console.h"

#define TAUNT_SLOTS 36
#define TAUNT_MODS 3

extern const char *const TAUNT_SLOT_KEYS[TAUNT_SLOTS]; // "1".."0", "q".."p", "a".."l", "z".."m"
extern const char *const TAUNT_MOD_KEYS[TAUNT_MODS];   // "alt", "ctrl", "shift"

typedef enum TauntMode { TAUNT_CHAT, TAUNT_TEAM } TauntMode;

// One taunt, as the editor shows it.
typedef struct Taunt {
    int slot;                     // its key's place in TAUNT_SLOT_KEYS
    int mod;                      // its modifier's place in TAUNT_MOD_KEYS
    char combo[CONSOLE_NAME_SIZE]; // the bind's key, "alt+q"
    TauntMode mode;               // who hears the message: everyone or the team; a radio call's goes to the team
    char text[CONSOLE_VALUE_SIZE]; // the message, a radio call's own words
    int radio;                    // 0 none, else the call: (call - 1) * 3 + place, 1..9
} Taunt;

// The bind's key name for a slot and a modifier: "alt+q".
void taunt_combo(char *out, size_t size, int slot, int mod);

// The taunt bound to `slot`, its modifiers tried in order, or false. A slot holds
// one taunt (taunt_set keeps to that); if a config binds two, the first shows. A
// bind that is no taunt (a control on the combo, say) is skipped: the keys page
// owns those.
bool taunt_at(const Console *con, int slot, Taunt *out);

// The bind's text for a taunt: `say` or `say_team` and the message, or
// `radio <call> <place>` and the message, said as that call to the team — what
// console_key runs, and what console_save writes back, round trip.
void taunt_compose(char *out, size_t size, TauntMode mode, const char *text, int radio);

// Binds the slot's `mod` combo to `text` (empty text unbinds it), unbinding the
// slot's other modifier combos first, so the slot holds this one taunt.
void taunt_set(Console *con, int slot, int mod, const char *text);
