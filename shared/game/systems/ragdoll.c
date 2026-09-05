// Corpses: a dead soldier's gostek skeleton run as a Verlet particle system, from the
// dead-soldier branches of Sprites.pas and Parts.pas by way of soldat-odin.
//
// Not ported yet (ragdoll.odin). Until then a corpse does not move: the dead soldier
// stays where it fell until it respawns.

#include "game/systems/systems.h"

void ragdolls_update(const Context *ctx, World *w, Events *events)
{
    (void)ctx;
    (void)w;
    (void)events;
}
