#pragma once

// Rounds: the match ends at its score or time limit, the scores stand a while, and the
// next round begins on the next map of the rotation, or the same one again. The world
// is loaded and made anew, everyone on the line is placed in it, and everyone is told
// (MsgMap); a client hears of the first round on joining the same way.

#include "connections.h"
#include "game/game.h"

// The map after `current` in `list`, a list of names separated by spaces or commas:
// the one following it, the first if it is last or not in the list, `current` itself
// if the list is empty. Into `out`.
const char *rounds_next_map(const char *list, const char *current, char *out, size_t size);

// A round on `map` from `assets`: the context reloaded, the world and match made anew
// with the history ring kept and cleared, everyone joined placed on their team, and
// the Map told. The limits stay; the mode is `wanted` as the map allows it
// (match_mode_choose; MATCH_MODE_COUNT for the map's own). False, with the game
// destroyed, if the map can't be loaded.
bool round_start(Game *g, Connections *c, const char *assets, const char *map, MatchMode wanted);
