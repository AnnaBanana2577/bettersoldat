#pragma once

// A weapons mod as Soldat's weapons.ini has it, so a mod made for Soldat or OpenSoldat is
// taken up as it is: an [Info] section (Name, Version), then a section for each weapon by
// Soldat's name for it ([Desert Eagles], [Barret M82A1], [Punch], [Grenade]...), each key
// one of its numbers in Soldat's units:
//
//   Damage FireInterval Ammo ReloadTime Speed StartUpTime Bink MovementAcc BulletSpread
//   Push InheritedVelocity ModifierHead ModifierChest ModifierLegs
//
// Soldat's BulletStyle, Recoil and NoCollision aren't this game's to change and are passed
// over. A weapon or a key the file doesn't name keeps the game's own number. `;` and `//`
// begin a comment.

#include <stdbool.h>
#include <stddef.h>

#include "game/game.h"

// Whether a mod may change a weapon: not those that follow another (the cluster grenade
// and its bomblets the frag grenade, the thrown knife the knife).
bool weapons_moddable(WeaponId id);

// A weapon's section in weapons.ini: Soldat's name for it; NULL for one no mod changes.
const char *weapons_ini_section(WeaponId id);

// `path` read over `stats`: each number it names. False if it can't be read. Its [Info]
// Name into `name`, if not NULL. `pass` (if not NULL) is told of each section or key it
// doesn't know.
bool weapons_ini_read(const char *path, WeaponStats stats[WEAPON_COUNT], char *name, size_t name_size,
                      void (*pass)(void *user, const char *what), void *user);

// A weapons.ini to start a mod from, written to `path`: every weapon's numbers as the game
// has them, commented out. False if it can't be written.
bool weapons_ini_template(const char *path);
