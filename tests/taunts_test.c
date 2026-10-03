// The taunts: the binds as the taunt editor reads and writes them (client/ui/taunts.c).
// What taunt_compose writes, taunt_at reads back, said or as a radio call; what a
// message loses to be a console line; and taunt_set's one taunt per slot.

#include <stdio.h>
#include <string.h>

#include "console/console.h"
#include "ui/taunts.h"
#include "test.h"

static void compose_and_read(void)
{
    Console *con = console_create(NULL, NULL);
    char text[CONSOLE_VALUE_SIZE];

    taunt_compose(text, sizeof text, TAUNT_CHAT, "Hello world", 0);
    CHECK(strcmp(text, "say Hello world") == 0, "a chat taunt is say and the message (%s)", text);
    taunt_set(con, 10, 0, text); // the q slot, on alt
    Taunt t;
    CHECK(taunt_at(con, 10, &t), "the bound taunt is read back");
    CHECK(t.mode == TAUNT_CHAT && t.radio == 0 && strcmp(t.text, "Hello world") == 0 && strcmp(t.combo, "alt+q") == 0,
          "as a chat taunt on its key (%s: %s, radio %d)", t.combo, t.text, t.radio);

    taunt_compose(text, sizeof text, TAUNT_TEAM, "Cover me!", 0);
    CHECK(strcmp(text, "say_team Cover me!") == 0, "a team taunt is say_team (%s)", text);
    taunt_set(con, 11, 1, text); // w, on ctrl
    CHECK(taunt_at(con, 11, &t) && t.mode == TAUNT_TEAM && strcmp(t.text, "Cover me!") == 0 && strcmp(t.combo, "ctrl+w") == 0,
          "and reads back with its modifier (%s)", t.combo);

    taunt_compose(text, sizeof text, TAUNT_TEAM, "Enemy!", 4);
    CHECK(strcmp(text, "radio 2 1 Enemy!") == 0, "a radio taunt is the call, the place and the message (%s)", text);
    taunt_set(con, 0, 0, text); // the 1 slot, on alt
    CHECK(taunt_at(con, 0, &t) && t.mode == TAUNT_TEAM && t.radio == 4 && strcmp(t.text, "Enemy!") == 0,
          "and reads back as the message with the radio call (radio %d, %s)", t.radio, t.text);

    taunt_compose(text, sizeof text, TAUNT_CHAT, "Base!", 9);
    CHECK(strcmp(text, "radio 3 3 Base!") == 0, "a radio call says it to the team whatever the mode (%s)", text);

    taunt_set(con, 31, 2, "radio 3 3 Base!"); // c, on shift, bound by hand
    CHECK(taunt_at(con, 31, &t) && t.radio == 9 && t.mode == TAUNT_TEAM && strcmp(t.text, "Base!") == 0,
          "a config's radio bind reads back too (%s, radio %d)", t.text, t.radio);

    console_destroy(con);
}

static void one_taunt_per_slot(void)
{
    Console *con = console_create(NULL, NULL);

    taunt_set(con, 10, 0, "say Hi");       // alt+q
    console_bind(con, "ctrl+q", "say Hi"); // a second, bound by hand
    Taunt t;
    CHECK(taunt_at(con, 10, &t) && t.mod == 0, "with two combos bound on a slot, the first modifier shows (%s)", t.combo);

    taunt_set(con, 10, 2, "say Bye"); // shift+q
    CHECK(strcmp(console_bind_get(con, "shift+q"), "say Bye") == 0 && !console_bind_get(con, "alt+q") &&
              !console_bind_get(con, "ctrl+q"),
          "setting a taunt unbinds the slot's other modifier combos");

    CHECK(!taunt_at(con, 12, &t), "an empty slot has no taunt");
    console_bind(con, "e", "jump");
    CHECK(!taunt_at(con, 12, &t), "and a plain bind, no modifier, is none either");
    console_bind(con, "ctrl+e", "jump");
    CHECK(!taunt_at(con, 12, &t), "nor is a control on the combo: the keys page owns it");

    taunt_set(con, 10, 2, "");
    CHECK(!console_bind_get(con, "shift+q"), "empty text unbinds the slot");

    taunt_set(con, 10, 0, "say radio");
    CHECK(taunt_at(con, 10, &t) && t.mode == TAUNT_CHAT && strcmp(t.text, "radio") == 0,
          "a message that begins with the word radio is still said, not a call");

    console_destroy(con);
}

static void message_sanitized(void)
{
    Console *con = console_create(NULL, NULL);
    char text[CONSOLE_VALUE_SIZE];

    taunt_compose(text, sizeof text, TAUNT_CHAT, "He said \"hi\"; ok // bye", 0);
    CHECK(strcmp(text, "say He said hi ok   bye") == 0,
          "a message loses its quotes and semicolons, and // becomes a space (%s)", text);
    taunt_set(con, 10, 0, text);
    Taunt t;
    CHECK(taunt_at(con, 10, &t) && strcmp(t.text, "He said hi ok   bye") == 0,
          "and the cleaned message reads back (%s)", t.text);

    console_destroy(con);
}

void taunt_tests(void)
{
    compose_and_read();
    one_taunt_per_slot();
    message_sanitized();
}
