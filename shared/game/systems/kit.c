// The kits: the medical and grenade kits of the map, which move to another of their
// spawn points when taken, and the bonus kits (flamer, predator, berserker, vest,
// cluster) that turn up on the server's schedule and go once taken or timed out. Who
// may take which, and what each gives. Ported from OpenSoldat Things.pas
// (CheckSpriteCollision's kit cases), Server.pas (SpawnThings) and ServerLoop.pas (the
// bonuses).
//
// sv_healthcooldown: the original sets HasPack on taking a medikit and clears it only
// when the player joins, so with the setting on (its default) a player takes one
// medikit a game. The setting says it is the wait before a second one, and that is what
// this does.

#include "game/systems/systems.h"

#define SPAWN_JITTER 25 // a kit comes up this far either way of its spawn point (SPAWNRANDOMVELOCITY)
#define DEFAULT_VEST 100.0f
#define CLUSTER_GRENADES 3
#define FLAMER_BONUS_TIME 600
#define PREDATOR_BONUS_TIME 1500
#define BERSERKER_BONUS_TIME 900

static int32_t kit_spawn_kind(ThingStyle style)
{
    switch (style) {
    case THING_MEDICAL_KIT: return SPAWN_MEDICAL_KIT;
    case THING_GRENADE_KIT: return SPAWN_GRENADE_KIT;
    case THING_FLAMER_KIT: return SPAWN_FLAMER_KIT;
    case THING_PREDATOR_KIT: return SPAWN_PREDATOR_KIT;
    case THING_VEST_KIT: return SPAWN_VEST_KIT;
    case THING_BERSERK_KIT: return SPAWN_BERSERK_KIT;
    case THING_CLUSTER_KIT: return SPAWN_CLUSTER_KIT;
    default: return 0;
    }
}

void kits_spawn(const Context *ctx, World *w, ThingStyle style, int amount)
{
    int32_t kind = kit_spawn_kind(style);
    for (int i = 1; i <= amount; i++) {
        // In capture the flag the medical and grenade kits go round their spawn points,
        // not twice running at the same; the original keeps that memory in the pool's
        // next-to-last slot, whatever thing is in it.
        bool rotate = style == THING_MEDICAL_KIT || style == THING_GRENADE_KIT;
        Vec2 pos;
        if (!(rotate && thing_spawn_boxes(ctx->map, kind, &w->things[MAX_THINGS - 2], &w->rng, &pos)) &&
            !thing_spawn_point(ctx->map, kind, &w->rng, &pos)) {
            return;
        }
        pos.x = pos.x - SPAWN_JITTER + (float)rand_int(&w->rng, 2 * 100 * SPAWN_JITTER) / 100.0f;
        pos.y = pos.y - SPAWN_JITTER + (float)rand_int(&w->rng, 2 * 100 * SPAWN_JITTER) / 100.0f;
        thing_create(ctx, w, style, pos, WEAPON_NONE, 0, -1);
    }
}

bool kit_wanted(const World *w, ThingStyle style, const Soldier *s)
{
    bool no_bonus = s->bonus == BONUS_NONE && s->cease_fire_counter < 1;
    switch (style) {
    case THING_MEDICAL_KIT: return s->health < DEFAULT_HEALTH && s->medikit_cooldown <= 0;
    case THING_GRENADE_KIT: return s->grenades < w->rules.max_grenades && (s->grenade_type != WEAPON_CLUSTER_NADE || s->grenades == 0);
    case THING_FLAMER_KIT: return no_bonus && s->weapon.id != WEAPON_BOW && s->weapon.id != WEAPON_BOW2;
    case THING_PREDATOR_KIT:
    case THING_BERSERK_KIT: return no_bonus;
    case THING_VEST_KIT: return s->vest < DEFAULT_VEST;
    case THING_CLUSTER_KIT: return s->grenade_type == WEAPON_FRAG || s->grenades == 0;
    default: return false;
    }
}

void kit_take(const Context *ctx, World *w, int index, uint8_t soldier, Events *events)
{
    Thing *t = &w->things[index];
    Soldier *s = &w->soldiers[soldier];
    ThingStyle style = t->style;
    if (!kit_wanted(w, style, s)) return;

    event_emit(events, (Event){.type = EVENT_KIT_PICKUP, .kit_pickup = {.player = soldier, .thing = (uint8_t)index, .kit = style, .pos = t->pos[0]}});
    if (style == THING_MEDICAL_KIT) s->medikit_cooldown = w->rules.medikit_cooldown;
    // the map's kits come up again at another of their spawn points; a bonus goes
    if (style == THING_MEDICAL_KIT || style == THING_GRENADE_KIT) thing_respawn(ctx, w, index);
    else thing_kill(t);
}

void kit_give(const Context *ctx, const World *w, Soldier *s, ThingStyle style)
{
    switch (style) {
    case THING_MEDICAL_KIT:
        s->health = DEFAULT_HEALTH;
        break;
    case THING_GRENADE_KIT:
        s->grenade_type = WEAPON_FRAG;
        s->grenades = w->rules.max_grenades;
        break;
    case THING_FLAMER_KIT:
        s->secondary = weapon_state(ctx, s->weapon.id); // the gun in hand, fresh, to come back to
        s->weapon = weapon_state(ctx, WEAPON_FLAMER);
        s->bonus = BONUS_FLAME_GOD;
        s->bonus_time = FLAMER_BONUS_TIME;
        s->health = DEFAULT_HEALTH;
        break;
    case THING_PREDATOR_KIT:
        s->bonus = BONUS_PREDATOR;
        s->bonus_time = PREDATOR_BONUS_TIME;
        s->health = DEFAULT_HEALTH;
        break;
    case THING_BERSERK_KIT:
        s->bonus = BONUS_BERSERKER;
        s->bonus_time = BERSERKER_BONUS_TIME;
        s->health = DEFAULT_HEALTH;
        break;
    case THING_VEST_KIT:
        s->vest = DEFAULT_VEST;
        break;
    case THING_CLUSTER_KIT:
        s->grenade_type = WEAPON_CLUSTER_NADE;
        s->grenades = CLUSTER_GRENADES;
        break;
    default:
        break;
    }
}

void bonuses_spawn(const Context *ctx, World *w, const MatchSettings *settings, uint32_t tick)
{
    static const int32_t FREQUENCIES[] = {0, 7400, 4300, 2500, 1600, 800};
    if (settings->bonus_frequency < 1 || settings->bonus_frequency > 5) return;
    int32_t freq = FREQUENCIES[settings->bonus_frequency];

    if (settings->bonus_berserker && tick % freq == 0 && rand_int(&w->rng, 4) == 0) kits_spawn(ctx, w, THING_BERSERK_KIT, 1);
    if (settings->bonus_flamer && tick % 444 == 0 && rand_int(&w->rng, 5) == 0) kits_spawn(ctx, w, THING_FLAMER_KIT, 1);
    if (settings->bonus_predator && tick % freq == 0 && rand_int(&w->rng, 5) == 0) kits_spawn(ctx, w, THING_PREDATOR_KIT, 1);
    if (settings->bonus_vest && tick % (freq / 2) == 0 && rand_int(&w->rng, 4) == 0) kits_spawn(ctx, w, THING_VEST_KIT, 1);
    // capture the flag makes the clusters likelier: Random(Round(4 * 0.75))
    if (settings->bonus_cluster && tick % (freq / 2) == 0 && rand_int(&w->rng, 3) == 0) kits_spawn(ctx, w, THING_CLUSTER_KIT, 1);
}
