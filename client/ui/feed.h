#pragma once

// What the game's events say to the player: the kill console, the big messages and the
// console's lines about flags and the match. Fed from the tick's events, so it is the
// same alone and online, where the server's events come down the wire. Ported from the
// original's KillConsole and BigText calls in Client.pas and Game.pas.

#include "console/console.h"
#include "game/game.h"
#include "ui/hud_data.h"

#define FEED_KILL_TICKS 300 // a kill console line's life
#define FEED_BIG_TICKS 270  // a big message's (the original's delay)

typedef struct Feed {
    HudKillLine kills[HUD_KILL_LINES]; // newest last
    int kill_ticks[HUD_KILL_LINES];    // left for each
    int kill_count;
    HudBigMessage big[HUD_BIG_MESSAGES]; // by layer: 0 the flags', 1 the match's
} Feed;

// After a tick: the lines age, and the tick's events add theirs. `names` are the
// players' for the lines; `team_game` colours the names by team.
void feed_tick(Feed *f, Console *con, const Game *g, const char names[MAX_PLAYERS][HUD_NAME], bool team_game);

// Into the HUD, each frame.
void feed_fill(const Feed *f, HudData *d);

// The team's name and colour as the texts show them.
const char *team_name(Team team);
Rgba team_color(Team team);
