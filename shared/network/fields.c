// The tables: the soldier's halves as the wire lays them out. What each half holds is
// soldier_copy_owned's and soldier_copy_served's to say (soldier.c); the tables follow
// them, and network_tests holds them to it. Widths are generous: a delta sends only
// what changed, and a value that does not fit goes bad in a test rather than on the
// wire.

#include <stddef.h>

#include "game/game.h"
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
    NETFIELD(Soldier, jets, NET_I, 32), // a map's fuel can be anything
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
    NETFIELD(Soldier, idle.time, NET_I, COUNTER),
    NETFIELD(Soldier, idle.random, NET_I, 8),
    NETFIELD(Soldier, idle.seen, NET_U, 8),
    NETFIELD(Soldier, has_cigar, NET_U, 4),
    NETFIELD(Soldier, wear_helmet, NET_U, 2),
    NETFIELD(Soldier, can_mercy, NET_BOOL, 0),
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
    NETFIELD(Soldier, death_fire, NET_U, 8),
    NETFIELD(Soldier, antic, NET_I, 8),
    NETFIELD(Soldier, antic_seq, NET_U, 8),
    NETFIELD(Soldier, rng, NET_U, 64),
    NETFIELD(Soldier, cmd_seq, NET_U, 32),
    NETFIELD(Soldier, shot_count, NET_U, 32),
    NETFIELD_ENUM(Soldier, primary_choice, WEAPON_COUNT - 1),
    NETFIELD_ENUM(Soldier, secondary_choice, WEAPON_COUNT - 1),
    NETFIELD(Soldier, look.shirt, NET_RGBA, 0),
    NETFIELD(Soldier, look.pants, NET_RGBA, 0),
    NETFIELD(Soldier, look.skin, NET_RGBA, 0),
    NETFIELD(Soldier, look.hair, NET_RGBA, 0),
    NETFIELD(Soldier, look.jet, NET_RGBA, 0),
    NETFIELD(Soldier, typing, NET_BOOL, 0),
    NETFIELD(Soldier, ping, NET_U, 16),
    NETFIELD(Soldier, bot, NET_BOOL, 0),
    NETFIELD_ENUM(Soldier, look.hair_style, 4),
    NETFIELD_ENUM(Soldier, look.head_style, 2),
    NETFIELD_ENUM(Soldier, look.chain_style, 2),
};
const int SOLDIER_SERVED_COUNT = sizeof SOLDIER_SERVED_FIELDS / sizeof SOLDIER_SERVED_FIELDS[0];

const NetField PLAYER_LOOK_FIELDS[] = {
    NETFIELD(PlayerLook, shirt, NET_RGBA, 0),
    NETFIELD(PlayerLook, pants, NET_RGBA, 0),
    NETFIELD(PlayerLook, skin, NET_RGBA, 0),
    NETFIELD(PlayerLook, hair, NET_RGBA, 0),
    NETFIELD(PlayerLook, jet, NET_RGBA, 0),
    NETFIELD_ENUM(PlayerLook, hair_style, 4),
    NETFIELD_ENUM(PlayerLook, head_style, 2),
    NETFIELD_ENUM(PlayerLook, chain_style, 2),
};
const int PLAYER_LOOK_COUNT = sizeof PLAYER_LOOK_FIELDS / sizeof PLAYER_LOOK_FIELDS[0];

const NetField SOLDIER_LOADOUT_FIELDS[] = {
    NETFIELD_ENUM(Soldier, primary_choice, WEAPON_COUNT - 1),
    NETFIELD_ENUM(Soldier, secondary_choice, WEAPON_COUNT - 1),
};
const int SOLDIER_LOADOUT_COUNT = sizeof SOLDIER_LOADOUT_FIELDS / sizeof SOLDIER_LOADOUT_FIELDS[0];

const NetField THING_FIELDS[] = {
    NETFIELD_ENUM(Thing, style, THING_STYLE_COUNT - 1),
    NETFIELD_ENUM(Thing, weapon, WEAPON_COUNT - 1),
    NETFIELD(Thing, ammo, NET_I, COUNTER),
    NETFIELD(Thing, flip, NET_BOOL, 0),
    NETFIELD(Thing, in_base, NET_BOOL, 0), // a flag at home: the team box shows the ones away
    NETFIELD_ENUM(Thing, holder, MAX_PLAYERS),
    NETFIELD_ENUM(Thing, owner, MAX_PLAYERS),
    NETFIELD(Thing, timeout, NET_I, 32), // a map's kit lies for hours
    NETFIELD(Thing, is_static, NET_BOOL, 0),
    NETFIELD_ENUM(Thing, points, 4),
    NETFIELD(Thing, pos[0], NET_VEC2, 0),
    NETFIELD(Thing, pos[1], NET_VEC2, 0),
    NETFIELD(Thing, pos[2], NET_VEC2, 0),
    NETFIELD(Thing, pos[3], NET_VEC2, 0),
    NETFIELD(Thing, old_pos[0], NET_VEC2, 0),
    NETFIELD(Thing, old_pos[1], NET_VEC2, 0),
    NETFIELD(Thing, old_pos[2], NET_VEC2, 0),
    NETFIELD(Thing, old_pos[3], NET_VEC2, 0),
    NETFIELD(Thing, cut, NET_U, 8),
    NETFIELD(Thing, in_base, NET_BOOL, 0),
    NETFIELD(Thing, interest, NET_I, COUNTER),
    NETFIELD(Thing, last_spawn, NET_U, 8),
};
const int THING_COUNT = sizeof THING_FIELDS / sizeof THING_FIELDS[0];

const NetField MATCH_FIELDS[] = {
    NETFIELD_ENUM(Match, settings.mode, MATCH_MODE_COUNT - 1),
    NETFIELD_ENUM(Match, state, MATCH_PAUSED),
    NETFIELD(Match, scores[0], NET_I, COUNTER),
    NETFIELD(Match, scores[1], NET_I, COUNTER),
    NETFIELD(Match, scores[2], NET_I, COUNTER),
    NETFIELD(Match, scores[3], NET_I, COUNTER),
    NETFIELD(Match, scores[4], NET_I, COUNTER),
    NETFIELD(Match, scores[5], NET_I, COUNTER),
    NETFIELD(Match, time_left, NET_I, 32),
    NETFIELD(Match, counter, NET_I, COUNTER),
};
const int MATCH_COUNT = sizeof MATCH_FIELDS / sizeof MATCH_FIELDS[0];
