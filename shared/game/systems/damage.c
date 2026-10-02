// The one place health changes. A Hit lands here: the knockback, the wound by
// HealthHit's rules (friendly fire, the Flame God, the vest, the berserker), death, and
// the disturbed aim. The wound is the authority's; the knockback and the disturbed aim
// land on every machine, as the original applies them wherever the bullet is flown.
// Ported from OpenSoldat Sprites.pas and Bullets.pas by way of soldat-odin.

#include "game/systems/systems.h"

// Friendly fire and the Flame God stop the wound, not the shove.
static bool wounds(const World *w, Hit hit)
{
    const Soldier *s = &w->soldiers[hit.target];
    const Soldier *attacker = &w->soldiers[hit.shooter];
    bool self = hit.target == hit.shooter;
    if (!w->rules.friendly_fire && s->team != TEAM_NONE && s->team == attacker->team && !self) return false;
    return s->bonus != BONUS_FLAME_GOD;
}

float hit_damage(const World *w, Hit hit)
{
    if (!wounds(w, hit)) return 0.0f;
    const Soldier *s = &w->soldiers[hit.target];
    float amount = hit.amount;
    if (s->vest > 0.0f) amount = (float)round_half_even(0.25f * hit.amount);
    // The server's rule: the berserker hurts itself fourfold too (the client's copy
    // spared it).
    if (w->soldiers[hit.shooter].bonus == BONUS_BERSERKER) amount = 4.0f * hit.amount;
    return amount;
}

static void wound(const Context *ctx, World *w, Hit hit, Events *events)
{
    if (hit.amount <= 0.0f || !wounds(w, hit)) return;
    Soldier *s = &w->soldiers[hit.target];

    float amount = hit_damage(w, hit);
    bool vested = s->vest > 0.0f;
    if (vested) s->vest -= (float)round_half_even(0.33f * hit.amount);

    s->health -= amount;
    if (s->health < BRUTAL_DEATH_HEALTH - 1.0f) s->health = BRUTAL_DEATH_HEALTH;
    if (s->health > DEFAULT_HEALTH) s->health = DEFAULT_HEALTH;
    // Die, on any wound that leaves the body below 1: a berserker's tears it apart
    if (s->health < 1.0f && w->soldiers[hit.shooter].bonus == BONUS_BERSERKER) s->torn_apart = true;

    // A wound on a corpse lands and nothing else does: no tally, no second death. But
    // the health goes on down, and with it the state the corpses are torn from, so a
    // body shot enough comes apart.
    if (s->dead) {
        if (s->health <= HEADCHOP_DEATH_HEALTH) s->death_part = hit.part;
        return;
    }

    event_emit(events, (Event){
        .type = EVENT_DAMAGE,
        .damage = {.attacker = hit.shooter, .target = hit.target, .weapon = hit.weapon, .amount = amount, .vest = vested},
    });
    if (s->health < 1.0f) die(ctx, w, hit, events);
}

// The shove and the bink of a Hit: the bullet's, not the wound's, so they land on the
// living whoever fired it, and on every machine that flew the bullet. In the original
// the bullet writes the victim's NextPush as it meets it, wherever it is simulated;
// here every world flies every bullet and the owner's word about its soldier stands,
// so the owner must feel the knock itself, or nobody does. The shove goes on the
// victim's next step, as the original's NextPush[0] does.
void hit_shove(const Context *ctx, World *w, Hit hit)
{
    Soldier *s = &w->soldiers[hit.target];
    if (!s->active) return;
    if (!s->dead) s->next_push = vec2_add(s->next_push, hit.push);
    if (hit.spray) hit_spray(ctx, w, hit.target, hit.shooter);
}

void damage_apply(const Context *ctx, World *w, Hit hit, Events *events)
{
    Soldier *s = &w->soldiers[hit.target];
    if (!s->active) return;

    hit_shove(ctx, w, hit);
    wound(ctx, w, hit, events);
}

void wounds_apply(const Context *ctx, World *w, const Events *last, Events *events)
{
    EventCursor pending = events_pending(last, events, PASS_WOUNDS);
    for (const Event *e = events_next(&pending); e; e = events_next(&pending)) {
        if (e->type != EVENT_HIT) continue;
        if (w->authority) damage_apply(ctx, w, e->hit, events);
        else hit_shove(ctx, w, e->hit); // the wound is the server's; the knock is felt here
    }
}

void die(const Context *ctx, World *w, Hit hit, Events *events)
{
    Soldier *s = &w->soldiers[hit.target];

    // the corpse starts from these, wherever it is drawn
    s->death_pos = s->pos;
    s->death_vel = s->vel;
    s->death_part = hit.part;
    // and whether it burns (Sprites.pas Die, "Fire on from bullet")
    s->death_fire = 0;
    switch (hit.weapon) {
    case WEAPON_FLAMER: s->death_fire = 1; break;
    case WEAPON_BOW2: if (rand_int(&w->rng, 4) == 0) s->death_fire = 1; break;
    case WEAPON_M79: if (rand_int(&w->rng, 8) == 0) s->death_fire = 2; break;
    case WEAPON_CLUSTER_NADE: if (rand_int(&w->rng, 3) == 0) s->death_fire = 3; break;
    case WEAPON_FRAG: if (rand_int(&w->rng, 12) == 0) s->death_fire = 4; break;
    default: break;
    }

    // the gun leaves the hand with the blow that killed; the things pass lays it down
    if (s->weapon.id != WEAPON_FLAMER && weapon_droppable(s->weapon.id)) {
        Pose pose = soldier_pose(ctx->anims, s, s->pos);
        event_emit(events, (Event){
            .type = EVENT_WEAPON_DROP,
            .weapon_drop = {.player = hit.target, .weapon = s->weapon.id, .ammo = s->weapon.ammo, .pos = pose.p[15], .impact = hit.impact},
        });
    }
    // What it held is the things': the things pass lets the flag go on the kill, and a
    // parachute keeps holding the body, so the corpse floats down under it. The rope
    // drops with the hand that held it.
    s->weapon = weapon_state(ctx, WEAPON_NONE);
    if (s->rope != ROPE_NONE) rope_cut(w, hit.target, s->pos, events);
    s->dead = true;
    s->vel = (Vec2){0};
    s->respawn_counter = w->rules.respawn_time;
    s->deaths++;

    Soldier *killer = &w->soldiers[hit.shooter];
    if (hit.shooter != hit.target) killer->kills++;
    else if (s->kills > 0) s->kills--;

    event_emit(events, (Event){
        .type = EVENT_KILL,
        .kill = {
            .killer = hit.shooter,
            .target = hit.target,
            .weapon = hit.weapon,
            .pos = s->pos,
            .health = s->health,
            .part = hit.part,
            .kills = killer->kills,
            .distance = hit.distance,
            .airtime = hit.airtime,
            .ricochets = hit.ricochets,
        },
    });
}
