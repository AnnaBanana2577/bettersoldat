#pragma once

// What the game's events say to the player: the kill console, the big messages and the
// console's lines about flags and the match. Fed from the tick's events, so it is the
// same alone and online, where the server's events come down the wire. Ported from the
// original's KillConsole and BigText calls in Client.pas and Game.pas.

#include "console/console.h"
#include "game/game.h"
#include "ui/hud_data.h"

// The kill console scrolls as the original's (Console.pas): a line's arrival holds it
// for a moment, then the oldest line goes once (two, when the newest is a victim's),
// and the rest stay until the next kill; a full console drops its oldest as one comes.
#define FEED_SCROLL_TICKS 240
#define FEED_NEW_MESSAGE_WAIT 70
#define FEED_KILL_MESSAGE_TICKS (4 * 60)    // KILLMESSAGEWAIT
#define FEED_CAPTURE_MESSAGE_TICKS (6 * 60) // CAPTUREMESSAGEWAIT

typedef struct Feed {
    HudKillLine kills[HUD_KILL_LINES]; // newest last
    int kill_count;
    int scroll_tick;
    HudBigMessage big[HUD_BIG_MESSAGES]; // by layer: 0 the flags', 1 the match's
} Feed;

// After a tick: the kill console scrolls when it is time, and the tick's events add
// theirs. `names` are the players' for the lines; `me` gets the big words about myself.
void feed_tick(Feed *f, Console *con, const Game *g, const char names[MAX_PLAYERS][HUD_NAME], bool team_game, int me);

// Into the HUD, each frame.
void feed_fill(const Feed *f, HudData *d);

// The team's name and colour as the texts show them.
const char *team_name(Team team);
Rgba team_color(Team team);
