// A soldier: spawning, and the tick in order. The parts live in their own systems:
// movement, soldier_collision, pose, combat, antics. Ported from OpenSoldat Sprites.pas
// by way of soldat-odin.

#include <string.h>

#include "game/systems/systems.h"

#define SOLDIER_DAMPING 0.99f

// A soldier's own randomness is seeded once, from where it first stood.
static uint64_t seed_from_position(Vec2 pos)
{
    uint32_t x, y;
    memcpy(&x, &pos.x, sizeof(x));
    memcpy(&y, &pos.y, sizeof(y));
    return ((uint64_t)x << 32 | (uint64_t)y) | 1;
}

void soldier_spawn(const Context *ctx, Soldier *s, Vec2 pos, Team team, Gear gear, WeaponId primary, WeaponId secondary)
{
    int32_t kills = s->kills, deaths = s->deaths, flags = s->flags;
    uint8_t life = s->life;
    PlayerLook look = s->look; // the player's, not the life's
    bool remote = s->remote;   // and whose keys move it is the machine's, not the life's
    bool bot = s->bot;
    uint64_t rng = s->rng != 0 ? s->rng : seed_from_position(pos);

    *s = (Soldier){
        .rng = rng,
        .life = life,
        .look = look,
        .remote = remote,
        .bot = bot,
        .kills = kills,
        .deaths = deaths,
        .flags = flags,
        .active = true,
        .team = team,
        .health = DEFAULT_HEALTH,
        .pos = pos,
        .old_pos = pos,
        .direction = 1,
        .old_direction = 1,
        .stance = STANCE_STAND,
        .jets = ctx->map->start_jet,
        .bg = {.status = BACKGROUND_TRANSITION, .poly = BACKGROUND_POLY_UNKNOWN},
        .cease_fire_counter = DEFAULT_CEASE_FIRE,
        .grenades = 1,
        .grenade_type = WEAPON_FRAG,
        .gear = gear,
        .primary_choice = primary,
        .secondary_choice = secondary,
        .weapon = weapon_state(ctx, primary),
        .secondary = weapon_state(ctx, secondary),
        .grenade_can_throw = true,
        .collider_distance = 255,
        .spawn_still = true,
        .bonus_time = -1,
        .aim_dist = DEFAULT_AIM_DIST,
        .idle = {.time = DEFAULT_IDLE_TIME, .random = -1},
        .wear_helmet = 1,
    };
    anim_set(ctx->anims, &s->legs, ANIM_STAND, 1);
    anim_set(ctx->anims, &s->body, ANIM_STAND, 1);
}

void soldiers_receive(const Context *ctx, World *w, const Events *last, Events *events)
{
    EventCursor pending = events_pending(last, events, PASS_RECEIPTS);
    for (const Event *e = events_next(&pending); e; e = events_next(&pending)) {
        switch (e->type) {
        case EVENT_KIT_PICKUP: kit_give(ctx, w, &w->soldiers[e->kit_pickup.player], e->kit_pickup.kit); break;
        case EVENT_WEAPON_PICKUP:
            dropped_gun_give(ctx, &w->soldiers[e->weapon_pickup.player], e->weapon_pickup.weapon, e->weapon_pickup.ammo);
            break;
        default: break;
        }
    }
}

void soldier_respawn(const Context *ctx, World *w, uint8_t index, Events *events)
{
    Soldier *s = &w->soldiers[index];
    // What it held goes back (a flag to its base, a parachute away) and a high spawn
    // gets a parachute: the things pass does both on hearing of the respawn.
    Vec2 pos = spawn_point(ctx->map, s->team, &w->rng);
    soldier_spawn(ctx, s, pos, s->team, s->gear, s->primary_choice, s->secondary_choice);
    s->life++;
    event_emit(events, (Event){
        .type = EVENT_RESPAWN,
        .respawn = {
            .target = index,
            .life = s->life,
            .team = s->team,
            .gear = s->gear,
            .primary = s->primary_choice,
            .secondary = s->secondary_choice,
            .pos = pos,
        },
    });
}

void soldier_arm(const Context *ctx, Soldier *s, WeaponId primary, WeaponId secondary)
{
    s->weapon = weapon_state(ctx, primary);
    s->secondary = weapon_state(ctx, secondary);
}

void soldier_integrate(Soldier *s, float gravity)
{
    s->forces.y += gravity;
    Vec2 prev = s->pos;
    s->vel = vec2_add(s->vel, s->forces);
    s->pos = vec2_add(s->pos, s->vel);
    s->vel = vec2_scale(s->vel, SOLDIER_DAMPING);
    s->old_pos = prev;
    s->forces = (Vec2){0};
}

void soldier_step(const Context *ctx, World *w, uint8_t index, Command cmd, Events *events, bool armed)
{
    Soldier *s = &w->soldiers[index];
    if (!s->active || s->team == TEAM_SPECTATOR) return;
    if (s->dead) { // the bink goes with the life, so none is carried into the next (a client's placing doesn't wipe it)
        s->hit_spray = 0;
        memset(s->bink_owed, 0, sizeof s->bink_owed);
        return;
    }

    parachute_catch(w, s);
    soldier_integrate(s, w->gravity);
    s->vel = vec2_add(s->vel, s->next_push);
    s->next_push = (Vec2){0};
    if (s->hit_spray > 0) s->hit_spray--;

    s->cmd_seq = cmd.seq;
    s->controls = w->rules.frozen ? 0 : cmd.buttons; // between rounds nobody moves
    if (s->controls != 0) s->spawn_still = false;
    // The aim leads by the soldier's own motion, to the whole unit (ControlSprite).
    s->aim = vec2((float)round_half_even(cmd.aim.x + s->vel.x), (float)round_half_even(cmd.aim.y + s->vel.y));
    if (s->controls & BUTTON_SUICIDE) event_emit(events, (Event){.type = EVENT_HIT, .hit = suicide_hit(w, index)});

    soldier_control(ctx, w, index, events, armed);
    s->direction = s->aim.x >= s->pos.x ? 1 : -1;
    anim_advance(ctx->anims, &s->body);
    anim_advance(ctx->anims, &s->legs);

    if (soldier_out_of_bounds(ctx, s->pos)) return; // off the map: it waits for the server to place it

    soldier_collide(ctx, w, index, events);
    weapon_timers(ctx, s);
    antics_apply(ctx, w, index, events, armed);
    soldier_swing(ctx, w, s);

    // the bow heals its archer
    if (w->tick % 3 == 0 && (s->weapon.id == WEAPON_BOW || s->weapon.id == WEAPON_BOW2) && s->health < DEFAULT_HEALTH) s->health += 1.0f;
    parachute_carry(w, s);

    // Jet fuel regenerates when not jetting: every tick on the ground, every other in the air.
    if (s->jets < ctx->map->start_jet && !(s->controls & BUTTON_JET)) {
        if (s->on_ground || w->tick % 2 == 0) s->jets++;
    }
}

bool soldier_out_of_bounds(const Context *ctx, Vec2 pos)
{
    float bound = (float)(ctx->map->sectors_num * ctx->map->sectors_division - 50);
    return fabsf(pos.x) > bound || fabsf(pos.y) > bound;
}

Hit suicide_hit(const World *w, uint8_t index)
{
    return (Hit){.shooter = index, .target = index, .amount = 4.0f * DEFAULT_HEALTH, .pos = w->soldiers[index].pos};
}

void soldier_served_tick(const Context *ctx, World *w, uint8_t index, Events *events)
{
    Soldier *s = &w->soldiers[index];
    if (!s->active || s->team == TEAM_SPECTATOR) return; // a spectator is neither alive nor to be respawned

    if (s->dead) {
        // CheckSkeletonOutOfBounds: a corpse that slid off the map is placed again at once;
        // otherwise the count runs out, checked before it is counted down
        const Ragdoll *body = &w->ragdolls[index];
        if (s->respawn_counter < 1 || (body->active && ragdoll_out_of_bounds(ctx, body))) soldier_respawn(ctx, w, index, events);
        else s->respawn_counter--;
        return;
    }
    if (soldier_out_of_bounds(ctx, s->pos)) {
        soldier_respawn(ctx, w, index, events);
        return;
    }

    if (s->cease_fire_counter > -1) s->cease_fire_counter--;
    if (s->bonus_time > -1) {
        s->bonus_time--;
        if (s->bonus_time < 1) s->bonus = BONUS_NONE;
    } else {
        s->bonus = BONUS_NONE;
    }
}

// An animation arrives as its id and frame; its pace is looked up here.
static void anim_copy(const Anims *anims, Anim *dst, Anim src)
{
    if (dst->id != src.id || dst->speed == 0) anim_set(anims, dst, src.id, src.frame);
    else dst->frame = src.frame;
}

void soldier_copy_owned(const Anims *anims, Soldier *dst, const Soldier *src)
{
    dst->pos = src->pos;
    dst->vel = src->vel;
    dst->next_push = src->next_push;
    dst->controls = src->controls;
    dst->aim = src->aim;
    dst->direction = src->direction;
    dst->stance = src->stance;
    dst->on_ground = src->on_ground;
    dst->jets = src->jets;
    dst->rope = src->rope;
    dst->rope_tip = src->rope_tip;
    dst->rope_tip_vel = src->rope_tip_vel;
    dst->rope_len = src->rope_len;
    dst->rope_grab = src->rope_grab;
    dst->rope_climb = src->rope_climb;
    dst->rope_wraps_count = src->rope_wraps_count;
    for (int i = 0; i < ROPE_WRAPS; i++) dst->rope_wraps[i] = src->rope_wraps[i];
    dst->was_jet = src->was_jet;
    anim_copy(anims, &dst->legs, src->legs);
    anim_copy(anims, &dst->body, src->body);
    dst->weapon = src->weapon;
    dst->secondary = src->secondary;
    dst->grenades = src->grenades;
    dst->spawn_still = src->spawn_still;
    dst->grenade_type = src->grenade_type;
    dst->use_time = src->use_time;
    dst->stat = src->stat;
    dst->idle = src->idle;
    dst->has_cigar = src->has_cigar;
    dst->wear_helmet = src->wear_helmet;
    dst->can_mercy = src->can_mercy;
}

Command soldier_last_command(const Soldier *s, bool quiet)
{
    return (Command){
        .seq = s->cmd_seq,
        .buttons = quiet ? 0 : (Buttons)(s->controls & ~(BUTTONS_ONE_SHOT & ~BUTTON_THROW)),
        .aim = vec2_sub(s->aim, s->vel), // the step leads the aim by the velocity again
    };
}

void soldier_copy_served(Soldier *dst, const Soldier *src)
{
    dst->active = src->active;
    dst->dead = src->dead;
    dst->team = src->team;
    dst->life = src->life;
    dst->health = src->health;
    dst->vest = src->vest;
    dst->respawn_counter = src->respawn_counter;
    dst->cease_fire_counter = src->cease_fire_counter;
    dst->bonus = src->bonus;
    dst->bonus_time = src->bonus_time;
    dst->antic = src->antic;
    dst->antic_seq = src->antic_seq;
    dst->held = src->held;
    dst->flag_grab_cooldown = src->flag_grab_cooldown;
    dst->medikit_cooldown = src->medikit_cooldown;
    dst->kills = src->kills;
    dst->deaths = src->deaths;
    dst->flags = src->flags;
    dst->death_pos = src->death_pos;
    dst->death_vel = src->death_vel;
    dst->death_part = src->death_part;
    dst->torn_apart = src->torn_apart;
    dst->death_fire = src->death_fire;
    dst->rng = src->rng;
    dst->cmd_seq = src->cmd_seq;
    dst->shot_count = src->shot_count;
    dst->gear = src->gear;
    dst->primary_choice = src->primary_choice;
    dst->secondary_choice = src->secondary_choice;
    dst->look = src->look;
    dst->typing = src->typing;
    dst->ping = src->ping;
    dst->bot = src->bot;
}

void soldier_copy_rest(Soldier *dst, const Soldier *src)
{
    dst->old_pos = src->old_pos;
    dst->forces = src->forces;
    dst->old_direction = src->old_direction;
    dst->was_running_left = src->was_running_left;
    dst->was_jumping = src->was_jumping;
    dst->was_jet = src->was_jet;
    dst->on_ground_last = src->on_ground_last;
    dst->on_ground_permanent = src->on_ground_permanent;
    dst->on_ground_for_law = src->on_ground_for_law;
    dst->bg = src->bg;
    dst->fired = src->fired;
    dst->burst_count = src->burst_count;
    dst->grenade_can_throw = src->grenade_can_throw;
    dst->can_auto_reload_spas = src->can_auto_reload_spas;
    dst->auto_reload_when_can_fire = src->auto_reload_when_can_fire;
    dst->collider_distance = src->collider_distance;
    dst->hit_spray = src->hit_spray;
    dst->idle = src->idle;
    dst->dont_drop = src->dont_drop;
    dst->legs.count = src->legs.count;
    dst->body.count = src->body.count;
}
