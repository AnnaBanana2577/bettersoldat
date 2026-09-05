// The one place health changes. A Hit becomes a wound here: the vest and berserker
// rules, then death. Ported from soldat-odin.

#include "game/systems/systems.h"

void damage_apply(const Context *ctx, World *w, Hit hit, Events *events)
{
    Soldier *s = &w->soldiers[hit.target];
    if (!s->active) return;
    const Soldier *attacker = &w->soldiers[hit.shooter];
    bool self = hit.target == hit.shooter;
    if (!w->rules.friendly_fire && s->team != TEAM_NONE && s->team == attacker->team && !self) return;
    if (s->bonus == BONUS_FLAME_GOD) return;

    // A wound on a corpse lands and nothing else does: no knockback, no tally, no second
    // death. But the health goes on down, and with it the state the corpses are torn
    // from, so a body shot enough comes apart.
    if (s->dead) {
        float amount = attacker->bonus == BONUS_BERSERKER && !self ? 4.0f * hit.amount : hit.amount;
        s->health = clampf(s->health - amount, BRUTAL_DEATH_HEALTH, DEFAULT_HEALTH);
        if (s->health <= HEADCHOP_DEATH_HEALTH) s->death_part = hit.part;
        return;
    }

    float amount = hit.amount;
    bool vested = s->vest > 0.0f;
    if (vested) {
        s->vest -= 0.33f * amount;
        amount = 0.25f * amount;
    }
    if (attacker->bonus == BONUS_BERSERKER && !self) amount = 4.0f * hit.amount;

    s->health = clampf(s->health - amount, BRUTAL_DEATH_HEALTH, DEFAULT_HEALTH);
    s->next_push = vec2_add(s->next_push, hit.push);
    event_emit(events, (Event){
        .type = EVENT_DAMAGE,
        .damage = {.attacker = hit.shooter, .target = hit.target, .weapon = hit.weapon, .amount = amount, .vest = vested},
    });
    if (s->health < 1.0f) die(ctx, w, hit, events);
}

void die(const Context *ctx, World *w, Hit hit, Events *events)
{
    Soldier *s = &w->soldiers[hit.target];

    // the corpse starts from these, wherever it is drawn
    s->death_pos = s->pos;
    s->death_vel = s->vel;
    s->death_part = hit.part;

    if (s->weapon.id != WEAPON_FLAMER) dropped_gun_from_death(ctx, w, hit.target, s, hit.push, events);
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
