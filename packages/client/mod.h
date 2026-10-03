#pragma once

// What the game looks and sounds like: mods/default/, the game's own, which every update
// replaces, and over it the one mod a player picks (cl_mod), mods/<name>/, which no update
// touches. A mod holds only what it changes: each file is looked for in the mod first,
// then in the default, so a mod may be a single sound. What the game plays by (the maps,
// animations, skeletons and bots) is not a mod's but data/'s, the same for everyone in a
// game.

#include <stdbool.h>
#include <stddef.h>

#define MOD_ROOT "mods"
#define MOD_DEFAULT "default"

typedef struct Mod {
    char dir[256];      // mods/<name>; empty for the default alone
    char fallback[256]; // mods/default
} Mod;

// The mod `name` over the default, both under `root`; an empty name, or "default", for the
// default alone.
void mod_init(Mod *m, const char *root, const char *name);

// The path of a file, named relative to a mod printf-style ("sfx/%s"): the mod's if it has it,
// else the default's, given whether there or not, so that what can't be found is named
// where it belongs.
#if defined(__GNUC__)
__attribute__((format(printf, 4, 5)))
#endif
void mod_file(const Mod *m, char *path, size_t size, const char *fmt, ...);

// An image in the directory `dir` (interface-gfx/guns) as find_image finds one, the .png
// first and in any case: the mod's if it has it, else the default's. False if neither.
bool mod_image(const Mod *m, const char *dir, const char *name, char *path, int path_size);
