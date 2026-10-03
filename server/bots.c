#include "bots.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The original's distances, on one axis (AI.pas).
#define DIST_AWAY 731
#define DIST_TOO_FAR 730
#define DIST_VERY_FAR 500
#define DIST_FAR 350
#define DIST_ROCK_THROW 180
#define DIST_CLOSE 95
#define DIST_VERY_CLOSE 55
#define DIST_TOO_CLOSE 35

#define WAYPOINT_TIMEOUT_SMALL (TICK_RATE * 5 + 20) // Constants.pas
#define WAYPOINT_TIMEOUT_BIG (TICK_RATE * 8)
#define WAYPOINTSEEKRADIUS 21
#define HURT_HEALTH 25
#define FRAGGRENADE_EXPLOSION_RADIUS 85.0f
#define SEE_DISTANCE 651.0f // how far a bot's ray reaches (Map.RayCast's MaxDist in ControlBot)
#define HEAD 11             // the skeleton's point 12, the original's LookPoint

// --- the profiles ------------------------------------------------------------------

// A Delphi colour as the files write it, "$00BBGGRR": the original's ReadConfColor
// turns it round; its ReadConfMagicColor (the skin's) takes the bytes as they are.
static Rgba parse_color(const char *text, bool magic)
{
    unsigned long v = strtoul(text[0] == '$' ? text + 1 : text, NULL, 16);
    uint8_t lo = (uint8_t)(v & 0xFF), mid = (uint8_t)((v >> 8) & 0xFF), hi = (uint8_t)((v >> 16) & 0xFF);
    return magic ? (Rgba){hi, mid, lo, 255} : (Rgba){lo, mid, hi, 255};
}

static WeaponId weapon_by_name(const Weapons *weapons, const char *name)
{
    for (int i = 0; i < WEAPON_COUNT; i++)
        if (weapons->info[i].name && strcmp(weapons->info[i].name, name) == 0) return (WeaponId)i;
    return WEAPON_NONE;
}

static void copy_text(char *dst, size_t size, const char *src) { snprintf(dst, size, "%s", src); }

bool bot_profile_load(const char *path, const Weapons *weapons, BotProfile *out)
{
    size_t size;
    uint8_t *data = file_read_all(path, &size);
    if (!data) {
        fprintf(stderr, "bots: could not read %s\n", path);
        return false;
    }
    BotProfile p = {
        .favourite = WEAPON_NONE,
        .secondary = WEAPON_COLT,
        .accuracy = 20,
        .grenade_frequency = 100,
        .chat_frequency = 10,
        .look = {.shirt = {255, 255, 255, 255}, .pants = {255, 255, 255, 255}, .skin = {230, 180, 120, 255},
                 .hair = {0, 0, 0, 255}, .jet = {0xFF, 0xBD, 0x24, 255}}, // DEFAULT_JETCOLOR, as a bot's is
    };
    bool found_weapon = false;
    char *cursor = (char *)data, *line;
    bool in_bot = false;
    while ((line = text_next_line(&cursor))) {
        if (line[0] == '[') {
            in_bot = strncmp(line, "[BOT]", 5) == 0;
            continue;
        }
        if (!in_bot) continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line, *value = eq + 1;
        if (strcmp(key, "Name") == 0) copy_text(p.name, sizeof p.name, value);
        else if (strcmp(key, "Color1") == 0) p.look.shirt = parse_color(value, false);
        else if (strcmp(key, "Color2") == 0) p.look.pants = parse_color(value, false);
        else if (strcmp(key, "Skin_Color") == 0) p.look.skin = parse_color(value, true);
        else if (strcmp(key, "Hair_Color") == 0) p.look.hair = parse_color(value, false);
        else if (strcmp(key, "Favourite_Weapon") == 0) {
            p.favourite = weapon_by_name(weapons, value);
            found_weapon = p.favourite != WEAPON_NONE || strcmp(value, "Hands") == 0;
        } else if (strcmp(key, "Secondary_Weapon") == 0) p.secondary = (WeaponId)(WEAPON_COLT + clampi(atoi(value), 0, 3));
        else if (strcmp(key, "Friend") == 0) copy_text(p.friend, sizeof p.friend, value);
        else if (strcmp(key, "Accuracy") == 0) p.accuracy = atoi(value);
        else if (strcmp(key, "Shoot_Dead") == 0) p.shoot_dead = atoi(value) == 1;
        else if (strcmp(key, "Grenade_Frequency") == 0) p.grenade_frequency = atoi(value);
        else if (strcmp(key, "Camping") == 0) p.camping = atoi(value);
        else if (strcmp(key, "Hair") == 0) p.look.hair_style = (uint8_t)clampi(atoi(value), 0, 4);
        else if (strcmp(key, "Headgear") == 0) {
            int h = atoi(value); // 0 nothing, 2 the hat, anything else the helmet
            p.look.head_style = (uint8_t)(h == 0 ? 0 : h == 2 ? 2 : 1);
        } else if (strcmp(key, "Chain") == 0) p.look.chain_style = (uint8_t)clampi(atoi(value), 0, 2);
        else if (strcmp(key, "Chat_Frequency") == 0) p.chat_frequency = atoi(value);
        else if (strcmp(key, "Chat_Kill") == 0) copy_text(p.chat_kill, sizeof p.chat_kill, value);
        else if (strcmp(key, "Chat_Dead") == 0) copy_text(p.chat_dead, sizeof p.chat_dead, value);
        else if (strcmp(key, "Chat_Lowhealth") == 0) copy_text(p.chat_low_health, sizeof p.chat_low_health, value);
        else if (strcmp(key, "Chat_SeeEnemy") == 0) copy_text(p.chat_see_enemy, sizeof p.chat_see_enemy, value);
        else if (strcmp(key, "Chat_Winning") == 0) copy_text(p.chat_winning, sizeof p.chat_winning, value);
    }
    free(data);
    if (!found_weapon) { // the original gives up on a bot whose weapon it doesn't know
        fprintf(stderr, "bots: %s names no weapon\n", path);
        return false;
    }
    if (!p.name[0]) copy_text(p.name, sizeof p.name, "Bot");
    *out = p;
    return true;
}

int bot_profiles_load(const char *data, const Weapons *weapons, BotProfile *out, int max)
{
    char dir[512], names[BOT_PROFILES][64];
    snprintf(dir, sizeof dir, "%s/bots", data);
    int found = list_files(dir, ".bot", names, BOT_PROFILES), n = 0;
    for (int i = 0; i < found && n < max; i++) {
        char path[600];
        snprintf(path, sizeof path, "%s/%s.bot", dir, names[i]);
        if (bot_profile_load(path, weapons, &out[n])) n++;
    }
    return n;
}

const BotProfile *bot_profile_random(const BotProfile *profiles, int count, uint64_t *rng)
{
    if (count <= 0) return NULL;
    const BotProfile *pick = &profiles[rand_int(rng, count)];
    if (strcmp(pick->name, "Boogie Man") == 0 || strcmp(pick->name, "Dummy") == 0) {
        for (int i = 0; i < count; i++)
            if (strcmp(profiles[i].name, "Sniper") == 0) return &profiles[i];
    }
    return pick;
}

// --- the brains --------------------------------------------------------------------

void bots_init(Bots *b, BotSettings settings, BotSay say, void *say_user)
{
    *b = (Bots){.settings = settings, .say = say, .say_user = say_user};
    if (b->settings.difficulty <= 0) b->settings.difficulty = BOT_DIFFICULTY_NORMAL;
}

void bots_attach(Bots *b, int slot, const BotProfile *profile, uint64_t seed)
{
    Brain *br = &b->brains[slot];
    *br = (Brain){
        .active = true,
        .profile = *profile,
        .accuracy = (int)(profile->accuracy * (b->settings.difficulty / 100.0)),
        .chat_freq = (int)(2.5 * profile->chat_frequency + 0.5),
        .rng = seed ? seed : 1,
        .target = 0, // the original's TargetNum of 1 after a spawn
        .waypoint_timeout_counter = WAYPOINT_TIMEOUT_SMALL,
        .life_seen = -1,
    };
}

void bots_detach(Bots *b, int slot) { b->brains[slot] = (Brain){0}; }

bool bots_has(const Bots *b, int slot) { return slot >= 0 && slot < MAX_PLAYERS && b->brains[slot].active; }

int bots_count(const Bots *b)
{
    int n = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) n += b->brains[i].active;
    return n;
}

void bots_new_round(Bots *b)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Brain *br = &b->brains[i];
        if (!br->active) continue;
        br->current_waypoint = br->next_waypoint = br->old_waypoint = br->last_waypoint = 0;
        br->waypoint_time = br->one_place_count = 0;
        br->go_thing = false;
        br->pissed_off = 0;
        br->waypoint_timeout_counter = WAYPOINT_TIMEOUT_SMALL;
    }
}

// --- the port ----------------------------------------------------------------------

static const Waypoint BLANK_WAYPOINT; // what an index off the map reads as

static const Waypoint *wp(const Map *m, int i)
{
    return i > 0 && i < m->waypoint_count ? &m->waypoints[i] : &BLANK_WAYPOINT;
}

// TWaypoints.FindClosest: the first active waypoint within `radius`, other than `curr`.
static int find_closest(const Map *m, Vec2 p, float radius, int curr)
{
    for (int i = 1; i < m->waypoint_count; i++) {
        if (!m->waypoints[i].active || i == curr) continue;
        if (vec2_length(vec2_sub(p, m->waypoints[i].pos)) < radius) return i;
    }
    return 0;
}

static int check_distance(float a, float b)
{
    float d = fabsf(a - b);
    if (d <= DIST_TOO_CLOSE) return DIST_TOO_CLOSE;
    if (d <= DIST_VERY_CLOSE) return DIST_VERY_CLOSE;
    if (d <= DIST_CLOSE) return DIST_CLOSE;
    if (d <= DIST_ROCK_THROW) return DIST_ROCK_THROW;
    if (d <= DIST_FAR) return DIST_FAR;
    if (d <= DIST_VERY_FAR) return DIST_VERY_FAR;
    if (d <= DIST_TOO_FAR) return DIST_TOO_FAR;
    return DIST_AWAY;
}

static void free_controls(BotControls *c) { *c = (BotControls){0}; }

static int roll(Brain *br, int n) { return rand_int(&br->rng, n); } // the original's Random(n): 0 for n <= 0

static Vec2 head_of(const Context *ctx, const Soldier *s) { return soldier_pose(ctx->anims, s, s->pos).p[HEAD]; }

static float weapon_speed(const Context *ctx, const Weapon *w)
{
    float speed = ctx->weapons.info[w->id].stats.speed;
    return speed > 0.0f ? speed : 1.0f;
}

static bool is_melee(WeaponId id) { return id == WEAPON_NONE || id == WEAPON_KNIFE || id == WEAPON_CHAINSAW; }

// The team's flag thing, or -1: the original's TeamFlag[team].
static int team_flag(const World *w, Team team)
{
    ThingStyle style = team == TEAM_ALPHA ? THING_ALPHA_FLAG : team == TEAM_BRAVO ? THING_BRAVO_FLAG : THING_NONE;
    if (style == THING_NONE) return -1;
    for (int i = 0; i < MAX_THINGS; i++)
        if (w->things[i].style == style) return i;
    return -1;
}

static bool holding_flag(const World *w, const Soldier *s)
{
    return s->held && thing_is_flag(w->things[s->held - 1].style);
}

// The player with the most kills: the original's SortedPlayers[1].
static int top_scorer(const World *w)
{
    int best = -1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Soldier *s = &w->soldiers[i];
        if (!s->active || s->team == TEAM_SPECTATOR) continue;
        if (best < 0 || s->kills > w->soldiers[best].kills) best = i;
    }
    return best;
}

static void chat(Bots *b, int slot, const char *text)
{
    if (b->say && text && text[0]) b->say(b->say_user, slot, text);
}

// SimpleDecision: the fight with the target, by the distance on each axis.
static void simple_decision(Bots *b, Brain *br, const Game *g, int me)
{
    const Context *ctx = &g->ctx;
    const World *w = &g->world;
    const Soldier *s = &w->soldiers[me], *target = &w->soldiers[br->target];
    BotControls *c = &br->controls;
    Vec2 m = s->pos, t = target->pos;
    int difficulty = b->settings.difficulty;

    if (!br->go_thing) {
        c->right = c->left = false;
        if (t.x > m.x) c->right = true;
        else if (t.x < m.x) c->left = true;
    }

    int dist_x = check_distance(m.x, t.x);
    if (dist_x == DIST_TOO_CLOSE) {
        if (!br->go_thing) {
            c->right = c->left = false;
            if (t.x < m.x) c->right = true;
            else if (t.x > m.x) c->left = true;
        }
        c->fire = true;
    } else if (dist_x == DIST_VERY_CLOSE) {
        if (!br->go_thing) c->right = c->left = false;
        c->fire = true;
        if (s->weapon.ammo == 0) { // reloading
            if (!br->go_thing) {
                c->right = c->left = false;
                if (t.x < m.x) c->right = true;
                else if (t.x > m.x) c->left = true;
            }
            c->fire = false;
        }
    } else if (dist_x == DIST_CLOSE) {
        if (!br->go_thing) c->right = c->left = false;
        c->down = true;
        c->fire = true;
        if (s->weapon.ammo == 0) {
            if (!br->go_thing) {
                c->right = c->left = false;
                if (t.x < m.x) c->right = true;
                else if (t.x > m.x) c->left = true;
            }
            c->down = false;
            c->fire = false;
        }
    } else if (dist_x == DIST_ROCK_THROW) {
        c->down = true;
        c->fire = true;
        if (s->weapon.ammo == 0) {
            if (!br->go_thing) {
                c->right = c->left = false;
                if (t.x < m.x) c->right = true;
                else if (t.x > m.x) c->left = true;
            }
            c->down = false;
            c->fire = false;
        }
    } else if (dist_x == DIST_FAR) {
        c->fire = true;
        if (br->profile.camping > 127 && !br->go_thing) {
            c->up = false;
            c->down = true;
        }
    } else if (dist_x == DIST_VERY_FAR) {
        c->up = true;
        if (roll(br, 2) == 0 || s->weapon.id == WEAPON_MINIGUN) c->fire = true;
        if (br->profile.camping > 0) {
            if (roll(br, 250) == 0 && s->body.id != ANIM_PRONE) c->prone = true;
            if (!br->go_thing) {
                c->right = c->left = c->up = false;
                c->down = true;
            }
        }
    } else if (dist_x == DIST_TOO_FAR) {
        if (roll(br, 4) == 0 || s->weapon.id == WEAPON_MINIGUN) c->fire = true;
        if (br->profile.camping > 0) {
            if (roll(br, 300) == 0 && s->body.id != ANIM_PRONE) c->prone = true;
            if (!br->go_thing) {
                c->right = c->left = c->up = false;
                c->down = true;
            }
        }
    }

    // move when the other player camps
    if (!br->go_thing && bots_has(b, br->target)) {
        const Brain *theirs = &b->brains[br->target];
        if (theirs->current_waypoint > 0 && wp(ctx->map, theirs->current_waypoint)->action != 0) {
            c->right = c->left = false;
            if (t.x > m.x) c->right = true;
            else if (t.x < m.x) c->left = true;
        }
    }

    // hide behind a collider
    if (difficulty < 101 && s->collider_distance < 255) {
        c->down = true;
        if (br->profile.camping > 0) {
            c->left = c->right = false;
            if (roll(br, 4) == 0 || s->weapon.id == WEAPON_MINIGUN) c->fire = true;
        }
        if (s->body.id == ANIM_HANDS_UP_AIM && s->body.frame != 11) c->fire = false;
    }

    // the target behind a collider and the bot not: go round
    if (difficulty < 201 && target->collider_distance < 255 && s->collider_distance > 254 && br->profile.camping > 0) {
        if (t.x < m.x) c->right = true;
        else if (t.x > m.x) c->left = true;
    }

    // fists against a gun, or a target below: close in
    if (is_melee(s->weapon.id) && (!is_melee(target->weapon.id) || t.y > m.y)) {
        c->right = c->left = c->down = false;
        c->fire = true;
        if (t.x > m.x) c->right = true;
        else if (t.x < m.x) c->left = true;
    }

    int dist_y = check_distance(m.y, t.y);
    if (!br->go_thing && dist_y >= DIST_ROCK_THROW && m.y > t.y) c->jet = true;

    if (target->bonus == BONUS_FLAME_GOD) { // run from a flame god
        c->right = c->left = false;
        if (t.x < m.x) c->right = true;
        else if (t.x > m.x) c->left = true;
    }

    if (s->stat > 0) { // manning a stationary gun: fire from it
        c->right = c->left = c->up = c->down = false;
        c->fire = true;
    }

    // a grenade
    if (br->profile.grenade_frequency > -1) {
        int gr = br->profile.grenade_frequency;
        if (s->weapon.ammo == 0 || s->weapon.fire_count > 125) gr /= 2;
        if (br->current_waypoint > 0 && wp(ctx->map, br->current_waypoint)->action != 0) gr /= 2;
        if (difficulty < 100) gr /= 2;
        if (difficulty < 201 && roll(br, gr) == 0 && dist_x < DIST_FAR && s->grenades > 0 &&
            ((dist_y < DIST_VERY_CLOSE && m.y > t.y) || m.y < t.y))
            c->throw_nade = true;
    }

    // a knife, thrown
    if (s->cease_fire_counter < 30 && s->weapon.id == WEAPON_KNIFE && br->profile.favourite == WEAPON_KNIFE) {
        c->fire = false;
        c->throw_weapon = true;
    }

    // the aim: ahead of the target, by its speed, above it by the drop, spread by the accuracy
    t = vec2_add(t, vec2_scale(target->vel, 10.0f));
    float speed = weapon_speed(ctx, &s->weapon);
    float lead = dist_x < DIST_FAR ? 0.5f * (float)dist_x / speed : 1.75f * (float)dist_x / speed;
    br->aim.x = (float)round_half_even(t.x);
    br->aim.y = (float)round_half_even(t.y - lead - (float)br->accuracy + (float)roll(br, br->accuracy));
    if (s->stat > 0) br->aim.y = (float)round_half_even(t.y - 0.5f * (float)dist_x / 30.0f - (float)br->accuracy + (float)roll(br, br->accuracy));

    // impossible: a sniper's target led to the shot
    if (difficulty < 60 && (target->weapon.id == WEAPON_BARRETT || target->weapon.id == WEAPON_RUGER)) {
        Vec2 tp = target->pos;
        int dist = round_half_even(vec2_length(vec2_sub(m, tp)));
        br->aim = vec2((float)round_half_even(tp.x), (float)round_half_even(tp.y));
        int steps = round_half_even((float)dist / weapon_speed(ctx, &target->weapon));
        for (int i = 1; i <= steps; i++) {
            br->aim.x += (float)round_half_even(target->vel.x);
            br->aim.y += (float)round_half_even(target->vel.y);
        }
        if (s->weapon.fire_count < 3) {
            free_controls(c);
            c->fire = true;
            c->down = true;
            AnimId a = s->body.id;
            if (a != ANIM_STAND && a != ANIM_RECOIL && a != ANIM_PRONE && a != ANIM_SHOTGUN && a != ANIM_BARRET &&
                a != ANIM_SMALL_RECOIL && a != ANIM_AIM_RECOIL && a != ANIM_HANDS_UP_RECOIL && a != ANIM_AIM &&
                a != ANIM_HANDS_UP_AIM)
                c->fire = false;
        }
    }
}

// GoToThing: walk to a thing, by whichever of its two top points is nearer.
static void go_to_thing(Brain *br, const Game *g, int me, int thing)
{
    const World *w = &g->world;
    const Soldier *s = &w->soldiers[me];
    const Thing *th = &w->things[thing];
    BotControls *c = &br->controls;
    Vec2 m = s->pos, t = th->pos[1];
    Vec2 p1 = th->pos[0], p2 = th->pos[1];
    if (p2.x > p1.x && m.x < p2.x) t = p2;
    if (p2.x > p1.x && m.x > p1.x) t = p1;
    if (p2.x < p1.x && m.x < p1.x) t = p1;
    if (p2.x < p1.x && m.x > p2.x) t = p2;
    if (th->holder > 0) t.y += 5.0f;

    if (t.x >= m.x) c->right = true;
    else if (t.x < m.x) c->left = true;

    // following a teammate carrying the flag: keep behind it, jet as it does
    if (th->holder > 0 && team_flag(w, s->team) >= 0) {
        const Soldier *carrier = &w->soldiers[th->holder - 1];
        if (s->team == carrier->team && !th->in_base) {
            int dist_x = check_distance(m.x, t.x);
            if (dist_x == DIST_TOO_CLOSE || dist_x == DIST_VERY_CLOSE) {
                c->right = c->left = false;
                c->down = true;
            }
            c->jet = (carrier->controls & BUTTON_JET) != 0;
        }
    }

    int dist_y = check_distance(m.y, t.y);
    if (dist_y >= DIST_VERY_CLOSE && m.y > t.y) c->jet = true;
}

// ControlBot: one tick of one bot's thinking.
static void control_bot(Bots *b, Brain *br, Game *g, const char *const names[MAX_PLAYERS], int me)
{
    const Context *ctx = &g->ctx;
    World *w = &g->world;
    const Map *map = ctx->map;
    Soldier *s = &w->soldiers[me];
    BotControls *c = &br->controls;
    bool ctf = g->match.settings.mode == MATCH_CTF;
    bool teams = match_has_teams(&g->match);
    int difficulty = b->settings.difficulty;

    bool was_throwing = c->throw_nade;
    free_controls(c);
    c->throw_nade = s->body.id == ANIM_THROW ? was_throwing : false;

    Vec2 look = head_of(ctx, s);
    look.y -= 2.0f;

    // who can be seen: the closest enemy, from the head, through the map
    bool see_closest = false;
    float d = 999999.0f, d2 = 0.0f, dt = 0.0f;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Soldier *o = &w->soldiers[i];
        if (!o->active || i == me || o->team == TEAM_SPECTATOR) continue;
        if (names && names[i] && names[i][0] && br->profile.friend[0] && strcmp(names[i], br->profile.friend) == 0) continue;
        if (o->bonus == BONUS_PREDATOR && !o->held) continue; // unseen, unless it carries something
        bool fresh_corpse = o->dead && br->profile.shoot_dead && w->ragdolls[i].active && w->ragdolls[i].dead_time < 180;
        if (o->dead && !fresh_corpse) continue;
        Vec2 start = head_of(ctx, o);
        if (map_collision_test(map, start, false, NULL)) start.y += 6.0f; // the ray's start not in the map
        if (map_ray_cast(map, look, start, SEE_DISTANCE, RAY_FILTER_DEFAULT, &d2)) continue;
        if (d > d2) {
            br->target = i;
            dt = d;
            if (!o->dead) d = d2;
            see_closest = true;
            if (o->dead) { // no grenades or knives at a corpse
                c->throw_nade = false;
                c->throw_weapon = false;
            }
            if (teams && o->team == s->team) see_closest = false;
        }
    }
    (void)dt;

    if (br->pissed_off == me + 1) br->pissed_off = 0;
    if (br->pissed_off > 0 && teams && !g->match.settings.friendly_fire && w->soldiers[br->pissed_off - 1].team == s->team) br->pissed_off = 0;
    if (teams && g->match.settings.friendly_fire && w->soldiers[br->target].team != s->team) br->pissed_off = 0;
    if (br->pissed_off > 0) { // whoever hit me, if I can see them
        const Soldier *o = &w->soldiers[br->pissed_off - 1];
        Vec2 start = head_of(ctx, o);
        if (o->active && !map_ray_cast(map, look, start, SEE_DISTANCE, RAY_FILTER_DEFAULT, &d2)) {
            br->target = br->pissed_off - 1;
            see_closest = true;
        } else {
            br->pissed_off = 0;
        }
    }

    // with the flag and unhurt: run
    bool run_away = false;
    if (see_closest && holding_flag(w, s) && w->soldiers[br->target].held == 0) {
        see_closest = false;
        run_away = true;
    }

    if (!see_closest) { // nobody in sight: the waypoints
        if (!br->go_thing && s->stat == 0) {
            float radius = br->current_waypoint == 0 ? 350.0f : (float)WAYPOINTSEEKRADIUS;
            int k = find_closest(map, s->pos, radius, br->current_waypoint);
            br->old_waypoint = br->current_waypoint;

            if (br->next_waypoint == 0) br->next_waypoint = 1;
            br->path_num = wp(map, br->next_waypoint)->path;
            if (ctf) { // the team's path; the other team's with the flag, which leads home
                br->path_num = (int)s->team;
                if (holding_flag(w, s)) br->path_num = s->team == TEAM_ALPHA ? 2 : s->team == TEAM_BRAVO ? 1 : br->path_num;
            }

            if (k > 0 && (br->path_num == wp(map, k)->path || br->current_waypoint == 0)) br->current_waypoint = k;

            if (br->current_waypoint > 0 && br->current_waypoint < map->waypoint_count) {
                const Waypoint *cur = wp(map, br->current_waypoint);
                if (br->old_waypoint != br->current_waypoint) { // arrived: the next, one of its connections
                    int ci = cur->count > 0 ? roll(br, cur->count) : 0;
                    int to = cur->connections[ci];
                    if (to > 0 && to < map->waypoint_count) {
                        br->next_waypoint = to;
                        br->aim = wp(map, to)->pos; // face it
                    }
                }
                const Waypoint *next = wp(map, br->next_waypoint);
                c->left = next->left;
                c->right = next->right;
                c->up = next->up;
                c->down = next->down;
                c->jet = next->jet;

                // a waypoint that says to wait, or to camp; not on the way home with the flag
                if (!ctf || !s->held) {
                    int a = cur->action, n = br->one_place_count;
                    if (a == 1 || (a == 2 && n < 60) || (a == 3 && n < 300) || (a == 4 && n < 600) || (a == 5 && n < 900) ||
                        (a == 6 && n < 1200)) {
                        c->left = c->right = c->up = c->down = c->jet = false;
                        if (s->stat == 0 && br->profile.camping > 0 && n > 180) c->down = true;
                    }
                }

                // running away, fire back at whoever is shooting
                if (run_away && br->pissed_off > 0) {
                    const Soldier *o = &w->soldiers[br->pissed_off - 1];
                    br->aim.x = (float)round_half_even(o->pos.x);
                    br->aim.y = (float)round_half_even(o->pos.y - 1.75f * 100.0f / weapon_speed(ctx, &s->weapon) -
                                                       (float)br->accuracy + (float)roll(br, br->accuracy));
                    c->fire = true;
                }

                if (br->last_waypoint == br->current_waypoint) br->waypoint_time++;
                else br->waypoint_time = 0;
                br->last_waypoint = br->current_waypoint;

                // standing in one place: stuck?
                if (cur->action == 0) {
                    if ((c->left || c->right) && !c->down) {
                        if (vec2_length(vec2_sub(s->pos, s->old_pos)) < 3.0f) br->one_place_count++;
                        else br->one_place_count = 0;
                    } else {
                        br->one_place_count = 0;
                    }
                } else {
                    br->one_place_count++;
                }
                if (cur->action == 0 && br->one_place_count > 90) { // stuck: jump
                    if (c->left && c->right) c->right = false;
                    c->up = true;
                }

                // the secondary back to the primary
                if (difficulty < 201 && (s->weapon.id == WEAPON_COLT || s->weapon.id == WEAPON_NONE || s->weapon.id == WEAPON_KNIFE ||
                                         s->weapon.id == WEAPON_CHAINSAW || s->weapon.id == WEAPON_LAW) &&
                    s->secondary.id != WEAPON_NONE)
                    c->change = true;
                // reload while it is quiet
                if (difficulty < 201 && s->weapon.ammo < 4 && ctx->weapons.info[s->weapon.id].stats.ammo > 3) c->reload = true;
                // get up
                if (roll(br, 150) == 0 && (s->body.id == ANIM_PRONE || s->body.id == ANIM_PRONE_MOVE)) c->prone = true;
            }
        }
    } else { // a target: fight it
        if (br->current_waypoint != 0 && wp(map, br->current_waypoint)->action == 0) br->current_waypoint = 0;
        simple_decision(b, br, g, me);
        if (br->current_waypoint > 0 && (!ctf || !s->held) && wp(map, br->current_waypoint)->action == 1) { // camp
            c->left = c->right = c->up = c->down = c->jet = false;
        }
        if (b->settings.chat) {
            if (roll(br, 115 * br->chat_freq) == 0) chat(b, me, br->profile.chat_see_enemy);
            if (roll(br, 790 * br->chat_freq) == 0) {
                char line[BOT_TEXT_SIZE];
                const char *name = names && names[br->target] && names[br->target][0] ? names[br->target] : "you";
                snprintf(line, sizeof line, "Die %s!", name);
                chat(b, me, line);
            }
        }
        br->waypoint_time = 0;
    }

    // a flag, a kit it needs, a knife: in sight and near, go for it
    bool see_thing = false;
    look = head_of(ctx, s);
    look.y -= 4.0f;
    for (int i = 0; i < MAX_THINGS && !see_thing; i++) {
        Thing *th = &w->things[i];
        if (th->style == THING_NONE || th->holder == me + 1) continue;
        bool knife = th->style == THING_WEAPON && th->weapon == WEAPON_KNIFE;
        bool wanted = thing_is_flag(th->style) || th->style == THING_FLAMER_KIT || th->style == THING_PREDATOR_KIT ||
                      th->style == THING_VEST_KIT || th->style == THING_BERSERK_KIT || knife ||
                      (th->style == THING_MEDICAL_KIT && s->health < DEFAULT_HEALTH) ||
                      (th->style == THING_GRENADE_KIT && s->grenades < w->rules.max_grenades &&
                       (s->grenade_type != WEAPON_CLUSTER_NADE || s->grenades == 0));
        if (!wanted) continue;
        Vec2 start = vec2(th->pos[1].x, th->pos[1].y - 5.0f);
        if (map_ray_cast(map, look, start, SEE_DISTANCE, RAY_FILTER_DEFAULT, &d2) || d2 >= DIST_FAR) continue;

        see_thing = true;
        bool my_flag = thing_is_flag(th->style) && (int)th->style == (int)s->team;
        int mine = team_flag(w, s->team);
        // not my own flag at home, unless I carry theirs: then it is where I score
        if (ctf && my_flag && th->in_base) {
            see_thing = false;
            if (s->held > 0 && s->held - 1 != i && w->things[s->held - 1].holder == me + 1) see_thing = true;
        }
        // not their flag while mine is away
        if (ctf && !my_flag && mine >= 0 && !w->things[mine].in_base) see_thing = false;
        // not their flag at home from afar
        if (ctf && !my_flag && thing_is_flag(th->style) && th->in_base && d2 > DIST_CLOSE) see_thing = false;
        // hurt and a medikit close: take it
        if (th->style == THING_MEDICAL_KIT && s->health < HURT_HEALTH && d2 < DIST_VERY_CLOSE) see_thing = true;
        // not a kit while running with the flag
        if ((th->style == THING_MEDICAL_KIT || th->style == THING_GRENADE_KIT || th->style == THING_FLAMER_KIT ||
             th->style == THING_PREDATOR_KIT || th->style == THING_BERSERK_KIT) && run_away)
            see_thing = false;
        if ((th->style == THING_FLAMER_KIT || th->style == THING_PREDATOR_KIT || th->style == THING_BERSERK_KIT) && s->bonus > BONUS_NONE)
            see_thing = false;
        if (knife) see_thing = true;

        if (!see_thing) continue;
        if (th->holder == 0) th->interest--;
        if (th->interest > 0) {
            if (b->settings.chat && thing_is_flag(th->style) && roll(br, 400 * br->chat_freq) == 0) chat(b, me, "Flag!");
            br->go_thing = true;
            go_to_thing(br, g, me, i);
        } else {
            br->go_thing = false;
        }
        if (knife && s->weapon.id == WEAPON_NONE && br->profile.favourite == WEAPON_KNIFE) { // my knife, back
            c->fire = false;
            br->target = 0;
            br->go_thing = true;
            go_to_thing(br, g, me, i);
        }
    }
    if (!see_thing) br->go_thing = false;

    // a grenade near: away from it
    if (difficulty < 201) {
        for (int i = 0; i < MAX_BULLETS; i++) {
            const Bullet *bu = &w->bullets[i];
            if (!bu->active || bu->style != BULLET_FRAG_GRENADE) continue;
            if (vec2_length(vec2_sub(bu->pos, s->pos)) >= FRAGGRENADE_EXPLOSION_RADIUS * 1.4f) continue;
            c->left = bu->pos.x > s->pos.x;
            c->right = !c->left;
        }
    }

    // the grenade goes once wound up
    if (s->body.id == ANIM_THROW && s->body.frame > 35) c->throw_nade = false;

    if (--br->waypoint_timeout_counter < 0) { // too long on the way: back to the last one, with a jump
        br->current_waypoint = br->old_waypoint;
        br->waypoint_timeout_counter = WAYPOINT_TIMEOUT_SMALL;
        free_controls(c);
        c->up = true;
    }
    if (br->waypoint_time > WAYPOINT_TIMEOUT_BIG) { // a waypoint that leads nowhere: forget it
        free_controls(c);
        br->current_waypoint = 0;
        br->go_thing = false;
        br->waypoint_time = 0;
    }

    // a fall to break with the jets
    if (s->vel.y > 3.35f) br->fall_save = 1;
    if (s->vel.y < 1.35f) br->fall_save = 0;
    if (br->fall_save > 0) c->jet = true;

    if (b->settings.chat && roll(br, br->chat_freq * 150) == 0 && top_scorer(w) == me) chat(b, me, br->profile.chat_winning);

    if (roll(br, 190) == 0) br->pissed_off = 0;

    if (s->stat > 0) { // on a stationary gun: bursts, the aim wandering
        br->one_place_count++;
        int n = br->one_place_count;
        if ((n > 120 && n < 220) || (n > 350 && n < 620) || (n > 700 && n < 740) || (n > 900 && n < 1100) || (n > 1300 && n < 1500)) {
            c->fire = true;
            if (roll(br, 2) == 0) br->aim.y += (float)roll(br, 4);
            else br->aim.y -= (float)roll(br, 4);
        }
        if (n > 1500) br->one_place_count = 0;
    }
}

static Buttons buttons_of(const BotControls *c)
{
    Buttons b = 0;
    if (c->left) b |= BUTTON_LEFT;
    if (c->right) b |= BUTTON_RIGHT;
    if (c->up) b |= BUTTON_JUMP;
    if (c->down) b |= BUTTON_CROUCH;
    if (c->prone) b |= BUTTON_PRONE;
    if (c->jet) b |= BUTTON_JET;
    if (c->fire) b |= BUTTON_FIRE;
    if (c->throw_nade) b |= BUTTON_THROW;
    if (c->reload) b |= BUTTON_RELOAD;
    if (c->change) b |= BUTTON_CHANGE;
    if (c->throw_weapon) b |= BUTTON_DROP;
    return b;
}

void bots_commands(Bots *b, Game *g, const char *const names[MAX_PLAYERS], Command cmds[MAX_PLAYERS])
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Brain *br = &b->brains[i];
        Soldier *s = &g->world.soldiers[i];
        if (!br->active || !s->active) continue;
        if ((int)s->life != br->life_seen) { // placed anew: as the original's Respawn leaves the brain
            br->life_seen = s->life;
            br->target = 0;
            br->waypoint_timeout_counter = WAYPOINT_TIMEOUT_SMALL;
            br->pissed_off = 0;
            br->aim = vec2(s->pos.x + (float)s->direction * 10.0f, s->pos.y);
        }
        if (s->dead || s->team == TEAM_SPECTATOR) {
            free_controls(&br->controls);
        } else {
            control_bot(b, br, g, names, i);
        }
        // the original's ControlSprite: the aim rides along with the soldier
        br->aim = vec2_add(br->aim, s->vel);
        cmds[i] = (Command){.seq = g->world.tick + 1, .buttons = buttons_of(&br->controls), .aim = br->aim};
    }
}

void bots_hear(Bots *b, const Game *g)
{
    const Events *events = &g->events;
    for (int i = 0; i < events->count; i++) {
        const Event *e = &events->items[i];
        if (e->type == EVENT_HIT) { // whoever shot me: pissed off at them
            int target = e->hit.target, shooter = e->hit.shooter;
            if (target < MAX_PLAYERS && shooter < MAX_PLAYERS && shooter != target && b->brains[target].active)
                b->brains[target].pissed_off = shooter + 1;
        } else if (e->type == EVENT_KILL && b->settings.chat) {
            int target = e->kill.target, killer = e->kill.killer;
            if (target < MAX_PLAYERS && b->brains[target].active) {
                Brain *br = &b->brains[target];
                if (roll(br, br->chat_freq / 2) == 0) chat(b, target, br->profile.chat_dead);
            }
            if (killer < MAX_PLAYERS && killer != target && b->brains[killer].active) {
                Brain *br = &b->brains[killer];
                if (roll(br, br->chat_freq / 3) == 0) chat(b, killer, br->profile.chat_kill);
            }
        }
    }
}
