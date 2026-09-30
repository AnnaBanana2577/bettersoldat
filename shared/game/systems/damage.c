// The one place health changes. A Hit lands here: the knockback, the wound by
// HealthHit's rules (friendly fire, the Flame God, the vest, the berserker), death, and
// the disturbed aim. Ported from OpenSoldat Sprites.pas and Bullets.pas by way of
// soldat-odin.

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

void damage_apply(const Context *ctx, World *w, Hit hit, Events *events)
{
    Soldier *s = &w->soldiers[hit.target];
    if (!s->active) return;

    // The knockback is the bullet's, not the wound's: it lands on the living whoever
    // fired it.
    if (!s->dead) s->next_push = vec2_add(s->next_push, hit.push);
    wound(ctx, w, hit, events);
    if (hit.spray) hit_spray(ctx, w, hit.target, hit.shooter);
}

void die(const Context *ctx, World *w, Hit hit, Events *events)
{
    Soldier *s = &w->soldiers[hit.target];

    // the corpse starts from these, wherever it is drawn
    s->death_pos = s->pos;
    s->death_vel = s->vel;
    s->death_part = hit.part;

    if (s->weapon.id != WEAPON_FLAMER) dropped_gun_from_death(ctx, w, hit.target, s, hit.impact, events);
    things_let_go(w, hit.target);
    // The server forgets what the soldier held; a parachute still holds the body and
    // says so again on its next tick, so the corpse floats down under it.
    s->held = 0;
    s->weapon = weapon_state(ctx, WEAPON_NONE);
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
        },
    });
}
