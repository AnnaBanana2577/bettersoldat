// The tables: the soldier's halves as the wire lays them out. What each half holds is
// soldier_copy_owned's and soldier_copy_served's to say (soldier.c); the tables follow
// them, and network_tests holds them to it. Widths are generous: a delta sends only
// what changed, and a value that does not fit goes bad in a test rather than on the
// wire.

#include <stddef.h>

#include "network/network.h"

// A count that never nears its width: 16 bits signed for the tick counters.
#define COUNTER 16

const NetField SOLDIER_OWNED_FIELDS[] = {
    NETFIELD(Soldier, pos, NET_VEC2, 0),
    NETFIELD(Soldier, vel, NET_VEC2, 0),
    NETFIELD(Soldier, next_push, NET_VEC2, 0),
    NETFIELD(Soldier, controls, NET_U, 16),
    NETFIELD(Soldier, aim, NET_VEC2, 0),
    NETFIELD(Soldier, direction, NET_I, 2),
    NETFIELD_ENUM(Soldier, stance, STANCE_PRONE),
    NETFIELD(Soldier, on_ground, NET_BOOL, 0),
    NETFIELD(Soldier, jets, NET_I, COUNTER),
    NETFIELD_ENUM(Soldier, legs.id, ANIM_COUNT - 1),
    NETFIELD(Soldier, legs.frame, NET_I, 8),
    NETFIELD_ENUM(Soldier, body.id, ANIM_COUNT - 1),
    NETFIELD(Soldier, body.frame, NET_I, 8),
    NETFIELD_ENUM(Soldier, weapon.id, WEAPON_COUNT - 1),
    NETFIELD(Soldier, weapon.ammo, NET_I, 10),
    NETFIELD(Soldier, weapon.fire_count, NET_I, COUNTER),
    NETFIELD(Soldier, weapon.reload_count, NET_I, COUNTER),
    NETFIELD(Soldier, weapon.startup_count, NET_I, COUNTER),
    NETFIELD_ENUM(Soldier, secondary.id, WEAPON_COUNT - 1),
    NETFIELD(Soldier, secondary.ammo, NET_I, 10),
    NETFIELD(Soldier, secondary.fire_count, NET_I, COUNTER),
    NETFIELD(Soldier, secondary.reload_count, NET_I, COUNTER),
    NETFIELD(Soldier, secondary.startup_count, NET_I, COUNTER),
    NETFIELD(Soldier, grenades, NET_I, 5),
    NETFIELD(Soldier, spawn_still, NET_BOOL, 0),
    NETFIELD_ENUM(Soldier, grenade_type, WEAPON_COUNT - 1),
    NETFIELD(Soldier, use_time, NET_I, 8),
    NETFIELD_ENUM(Soldier, stat, MAX_THINGS),
};
const int SOLDIER_OWNED_COUNT = sizeof SOLDIER_OWNED_FIELDS / sizeof SOLDIER_OWNED_FIELDS[0];

const NetField SOLDIER_SERVED_FIELDS[] = {
    NETFIELD(Soldier, active, NET_BOOL, 0),
    NETFIELD(Soldier, dead, NET_BOOL, 0),
    NETFIELD_ENUM(Soldier, team, TEAM_COUNT - 1),
    NETFIELD(Soldier, life, NET_U, 8),
    NETFIELD(Soldier, health, NET_F32, 0),
    NETFIELD(Soldier, vest, NET_F32, 0),
    NETFIELD(Soldier, respawn_counter, NET_I, COUNTER),
    NETFIELD(Soldier, cease_fire_counter, NET_I, COUNTER),
    NETFIELD_ENUM(Soldier, bonus, BONUS_BERSERKER),
    NETFIELD(Soldier, bonus_time, NET_I, COUNTER),
    NETFIELD_ENUM(Soldier, held, MAX_THINGS),
    NETFIELD(Soldier, flag_grab_cooldown, NET_I, COUNTER),
    NETFIELD(Soldier, medikit_cooldown, NET_I, COUNTER),
    NETFIELD(Soldier, kills, NET_I, COUNTER),
    NETFIELD(Soldier, deaths, NET_I, COUNTER),
    NETFIELD(Soldier, flags, NET_I, COUNTER),
    NETFIELD(Soldier, death_pos, NET_VEC2, 0),
    NETFIELD(Soldier, death_vel, NET_VEC2, 0),
    NETFIELD(Soldier, death_part, NET_U, 8),
    NETFIELD(Soldier, torn_apart, NET_BOOL, 0),
    NETFIELD(Soldier, rng, NET_U, 64),
    NETFIELD(Soldier, cmd_seq, NET_U, 32),
    NETFIELD(Soldier, view_lag, NET_U, 8),
    NETFIELD(Soldier, shot_count, NET_U, 32),
    NETFIELD_ENUM(Soldier, primary_choice, WEAPON_COUNT - 1),
    NETFIELD_ENUM(Soldier, secondary_choice, WEAPON_COUNT - 1),
    NETFIELD(Soldier, look.shirt, NET_RGBA, 0),
    NETFIELD(Soldier, look.pants, NET_RGBA, 0),
    NETFIELD(Soldier, look.skin, NET_RGBA, 0),
    NETFIELD(Soldier, look.hair, NET_RGBA, 0),
    NETFIELD(Soldier, look.jet, NET_RGBA, 0),
    NETFIELD_ENUM(Soldier, look.hair_style, 4),
    NETFIELD_ENUM(Soldier, look.head_style, 2),
    NETFIELD_ENUM(Soldier, look.chain_style, 2),
};
const int SOLDIER_SERVED_COUNT = sizeof SOLDIER_SERVED_FIELDS / sizeof SOLDIER_SERVED_FIELDS[0];
