#include "ui/feed.h"

#include <stdio.h>
#include <string.h>

#include "game/systems/systems.h"

const char *team_name(Team team)
{
    switch (team) {
    case TEAM_ALPHA: return "Alpha";
    case TEAM_BRAVO: return "Bravo";
    case TEAM_CHARLIE: return "Charlie";
    case TEAM_DELTA: return "Delta";
    case TEAM_SPECTATOR: return "Spectator";
    default: return "Nobody";
    }
}

// The original's ALPHA_K .. DELTA_K message colours.
Rgba team_color(Team team)
{
    switch (team) {
    case TEAM_ALPHA: return (Rgba){0xDF, 0x31, 0x31, 0xFF}; // ALPHA_MESSAGE_COLOR and the rest
    case TEAM_BRAVO: return (Rgba){0x31, 0x31, 0xDF, 0xFF};
    case TEAM_CHARLIE: return (Rgba){0xDF, 0xDF, 0x31, 0xFF};
    case TEAM_DELTA: return (Rgba){0x31, 0xDF, 0x31, 0xFF};
    default: return (Rgba){0xFF, 0xFF, 0xFF, 0xFF};
    }
}

// The team whose flag a style is.
static Team flag_team(ThingStyle style)
{
    return style == THING_ALPHA_FLAG ? TEAM_ALPHA : style == THING_BRAVO_FLAG ? TEAM_BRAVO : TEAM_NONE;
}

// The oldest line of the kill console goes (ScrollConsole).
static void kill_scroll(Feed *f)
{
    if (f->kill_count == 0) return;
    memmove(f->kills, f->kills + 1, sizeof f->kills - sizeof f->kills[0]);
    f->kill_count--;
}

// A line at the bottom of the kill console (ConsoleNum); the oldest goes when it is full.
static void kill_line(Feed *f, const char *text, Rgba color, WeaponId weapon, bool icon)
{
    if (f->kill_count == HUD_KILL_LINES) kill_scroll(f);
    HudKillLine *l = &f->kills[f->kill_count++];
    snprintf(l->text, sizeof l->text, "%s", text);
    l->color = color;
    l->weapon = weapon;
    l->has_icon = icon;
    f->scroll_tick = -FEED_NEW_MESSAGE_WAIT;
}

// A big message on `layer`, replacing what was there.
static void big_text(Feed *f, int layer, const char *text, Rgba color, float scale, float x, float y, int delay)
{
    HudBigMessage *m = &f->big[layer];
    snprintf(m->text, sizeof m->text, "%s", text);
    m->color = color;
    m->scale = scale;
    m->delay = delay;
    m->x = x;
    m->y = y;
    m->centered = false;
}

// The original's BigMessage: on layer 1, at full size or narrower to fit, in the
// middle, low on the screen.
static void big_message(Feed *f, const char *text, Rgba color, int delay)
{
    big_text(f, 1, text, color, 1.0f / 4.8f, 0, 420, delay);
    f->big[1].centered = true;
}

// The kill console's colours (the original's *_K_ and *_D_MESSAGE_COLOR): the killer's
// line by its team, the victim's by its; with no teams the killer green and the victim
// dark red; a suicide the spectator's gold.
static Rgba killer_color(Team team)
{
    switch (team) {
    case TEAM_ALPHA: return (Rgba){0xFF, 0xE3, 0xE3, 0xEB};
    case TEAM_BRAVO: return (Rgba){0xD3, 0xE3, 0xFF, 0xEB};
    case TEAM_CHARLIE: return (Rgba){0xFF, 0xFF, 0xE3, 0xEB};
    case TEAM_DELTA: return (Rgba){0xD3, 0xFF, 0xE3, 0xEB};
    default: return (Rgba){0x52, 0xD1, 0x19, 0xEE};
    }
}

static Rgba victim_color(Team team)
{
    switch (team) {
    case TEAM_ALPHA: return (Rgba){0xDA, 0xB0, 0xB0, 0xEB};
    case TEAM_BRAVO: return (Rgba){0xA0, 0xB0, 0xDA, 0xEB};
    case TEAM_CHARLIE: return (Rgba){0xD0, 0xD0, 0xB0, 0xEB};
    case TEAM_DELTA: return (Rgba){0xA0, 0xD0, 0xBA, 0xEB};
    default: return (Rgba){0x80, 0x13, 0x04, 0xEE};
    }
}

// The kill console (NetworkClientSprite.pas's death): the killer with its tally over
// its weapon's icon, the victim under; a suicide is the one line, in gold. And the big
// words for me: whom I killed, who killed me.
static void kill(Feed *f, const Game *g, const char names[MAX_PLAYERS][HUD_NAME], const EventKill *k, int me)
{
    char text[HUD_TEXT];
    Team killer_team = g->world.soldiers[k->killer].team, victim_team = g->world.soldiers[k->target].team;
    snprintf(text, sizeof text, "%s (%d)", names[k->killer], k->kills);
    if (k->killer != k->target) {
        kill_line(f, text, killer_color(killer_team), k->weapon, true);
        kill_line(f, names[k->target], victim_color(victim_team), k->weapon, false);
    } else {
        kill_line(f, text, (Rgba){0xD3, 0xB7, 0x27, 0xEB}, k->weapon, true);
    }
    if (k->killer == me && k->target != me) { // my weapon's tally, and the shot's readout
        f->stats[k->weapon].kills++;
        if (k->part == 12) f->stats[k->weapon].headshots++;
        if (k->distance > 0.0f) {
            f->shot_ticks = FEED_KILL_MESSAGE_TICKS - 30;
            f->shot_distance = k->distance;
            f->shot_airtime = (float)k->airtime / 60.0f;
            f->shot_ricochets = k->ricochets;
        }
    }
    if (k->killer == me && k->target != me) { // the original's multikill words, over "You killed"
        static const char *const MULTIKILL[] = {"DOUBLE KILL", "TRIPLE KILL", "MULTI KILL", "MULTI KILL X2", "SERIAL KILL",
                                               "INSANE KILLS", "GIMME MORE!", "MASTA KILLA!", "MASTA KILLA!", "MASTA KILLA!",
                                               "STOP IT!!!!", "MERCY!!!!!!!!!!", "CHEATER!!!!!!!!",
                                               "Phased-plasma rifle in the forty watt range", "Hey, just what you see, pal",
                                               "just what you see, pal..."};
        f->multi_time = FEED_MULTIKILL_TICKS;
        f->multi_kills++;
        snprintf(text, sizeof text, "You killed %s", names[k->target]);
        big_message(f, text, (Rgba){0xEA, 0x35, 0x30, 0xFF}, FEED_KILL_MESSAGE_TICKS);
        if (f->multi_kills > 1 && f->multi_kills < 18) big_message(f, MULTIKILL[f->multi_kills - 2], (Rgba){0xEA, 0x35, 0x30, 0xFF}, FEED_KILL_MESSAGE_TICKS);
        else if (f->multi_kills > 17) big_message(f, MULTIKILL[7], (Rgba){0xEA, 0x35, 0x30, 0xFF}, FEED_KILL_MESSAGE_TICKS);
    }
    if (k->killer == me && k->target == me) {
        big_message(f, "You killed yourself", (Rgba){0xC5, 0x30, 0x25, 0xFF}, FEED_KILL_MESSAGE_TICKS);
    } else if (k->target == me) {
        snprintf(text, sizeof text, "Killed by %s", names[k->killer]);
        big_message(f, text, (Rgba){0xC5, 0x30, 0x25, 0xFF}, FEED_KILL_MESSAGE_TICKS);
    }
}

// The original's names for the flags: the alpha team's is red, the bravo team's blue.
static const char *flag_name(ThingStyle flag) { return flag == THING_ALPHA_FLAG ? "Red" : "Blue"; }
static Rgba flag_color(ThingStyle flag) { return team_color(flag_team(flag)); }

// The match's end: the team that won, or with no teams the player with the most kills.
static void match_end(Feed *f, Console *con, const Game *g, const char names[MAX_PLAYERS][HUD_NAME], Team winner)
{
    char text[HUD_TEXT];
    Rgba color = team_color(winner);
    if (winner != TEAM_NONE) {
        snprintf(text, sizeof text, "%s Team Wins!", team_name(winner));
    } else {
        int best = -1;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            const Soldier *s = &g->world.soldiers[i];
            if (s->active && (best < 0 || s->kills > g->world.soldiers[best].kills)) best = i;
        }
        if (best >= 0) snprintf(text, sizeof text, "%s Wins!", names[best]);
        else snprintf(text, sizeof text, "Draw!");
    }
    big_message(f, text, color, FEED_CAPTURE_MESSAGE_TICKS);
}

void feed_tick(Feed *f, Console *con, const Game *g, const char names[MAX_PLAYERS][HUD_NAME], bool team_game, int me)
{
    (void)team_game;
    if (f->shot_ticks > 0) f->shot_ticks--;
    if (f->multi_time > -1) f->multi_time--;
    else f->multi_kills = 0;
    // the kill console scrolls once, a while after the last kill (UpdateFrame.pas)
    if (++f->scroll_tick == FEED_SCROLL_TICKS) {
        kill_scroll(f);
        if (f->kill_count > 0 && !f->kills[f->kill_count - 1].has_icon) kill_scroll(f);
    }
    for (int i = 0; i < HUD_BIG_MESSAGES; i++)
        if (f->big[i].delay > 0) f->big[i].delay--;

    // "Time Left:" (UpdateFrame.pas) with the clock's beeps: in seconds under an hour,
    // every minute inside the last ten and every ten before; in minutes past it
    if (g->match.state == MATCH_PLAYING) {
        int32_t t = g->match.time_left;
        bool due = t > 0 && (t <= 600 ? t % 60 == 0 : t <= 3600 ? t % 600 == 0 : t <= 18000 ? t % 3600 == 0 : t % 18000 == 0);
        if (due && t <= 3600) console_print_color(con, HUD_COLOR_GAME, "Time Left: %d seconds\n", t / 60);
        else if (due) console_print_color(con, HUD_COLOR_GAME, "Time Left: %d minutes\n", t / 3600);
    }

    for (int i = 0; i < g->events.count; i++) {
        const Event *e = &g->events.items[i];
        char text[HUD_TEXT];
        switch (e->type) {
        case EVENT_KILL: kill(f, g, names, &e->kill, me); break;
        case EVENT_FIRE:
            if (e->fire.player == me) f->stats[e->fire.weapon].shots++;
            break;
        case EVENT_DAMAGE:
            if (e->damage.attacker == me && e->damage.target != me) f->stats[e->damage.weapon].hits++;
            break;
        case EVENT_FLAG_SCORE: { // the team that scores is the one whose flag it isn't
            Team scoring = flag_team(e->flag_score.flag) == TEAM_ALPHA ? TEAM_BRAVO : TEAM_ALPHA;
            snprintf(text, sizeof text, "%s Team Scores!", team_name(scoring));
            big_message(f, text, team_color(scoring), FEED_SCORE_MESSAGE_TICKS);
            console_print_color(con, team_color(scoring), "%s scores for %s Team\n", names[e->flag_score.player], team_name(scoring));
            break;
        }
        case EVENT_FLAG_GRAB: { // the enemy's flag taken: mine to me, theirs to the rest; no
                                // line in the console, as the original (its SmallCapText is never drawn)
            ThingStyle flag = e->flag_grab.flag;
            if (e->flag_grab.player == me) snprintf(text, sizeof text, "You got the %s Flag!", flag_name(flag));
            else snprintf(text, sizeof text, "%s Flag captured!", flag_name(flag));
            big_message(f, text, flag_color(flag), FEED_CAPTURE_MESSAGE_TICKS);
            break;
        }
        case EVENT_FLAG_RETURN: { // by a player: said; by the clock: nothing, as the original
            ThingStyle flag = e->flag_return.flag;
            if (e->flag_return.player == 255) break;
            snprintf(text, sizeof text, "%s Flag returned!", flag_name(flag));
            big_message(f, text, team_color(TEAM_ALPHA), FEED_CAPTURE_MESSAGE_TICKS); // alpha's colour for either, as the original
            console_print_color(con, flag_color(flag), "%s returned the %s Flag\n", names[e->flag_return.player], flag_name(flag));
            break;
        }
        case EVENT_FLAG_DROP: { // a carrier's death: its teammates are told big
            ThingStyle flag = e->flag_drop.flag;
            console_print_color(con, flag_color(flag), "%s dropped the %s Flag\n", names[e->flag_drop.player], flag_name(flag));
            if (g->world.soldiers[e->flag_drop.player].team == g->world.soldiers[me].team) {
                snprintf(text, sizeof text, "%s Flag dropped!", flag_name(flag));
                big_message(f, text, (Rgba){0x77, 0xD3, 0x34, 0xFF}, FEED_CAPTURE_MESSAGE_TICKS);
            }
            break;
        }
        case EVENT_KIT_PICKUP: { // the bonuses, to the one who took them
            if (e->kit_pickup.player != me) break;
            const Rgba bonus = {0xEF, 0x31, 0x21, 0xFF}, capture = {0x77, 0xD3, 0x34, 0xFF};
            switch (e->kit_pickup.kit) {
            case THING_FLAMER_KIT: big_message(f, "Flame God Mode!", bonus, FEED_CAPTURE_MESSAGE_TICKS); break;
            case THING_PREDATOR_KIT: big_message(f, "Predator Mode!", bonus, FEED_CAPTURE_MESSAGE_TICKS); break;
            case THING_BERSERK_KIT: big_message(f, "Berserker Mode!", bonus, FEED_CAPTURE_MESSAGE_TICKS); break;
            case THING_VEST_KIT: big_message(f, "Bulletproof Vest!", capture, FEED_CAPTURE_MESSAGE_TICKS); break;
            case THING_CLUSTER_KIT: big_message(f, "Cluster grenades!", capture, FEED_CAPTURE_MESSAGE_TICKS); break;
            default: break;
            }
            break;
        }
        case EVENT_MATCH_END: match_end(f, con, g, names, e->match_end.winner); break;
        default: break;
        }
    }
}

void feed_fill(const Feed *f, HudData *d, const Weapons *weapons)
{
    d->weapon_stat_count = 0;
    for (int w = 0; w < WEAPON_COUNT && d->weapon_stat_count < HUD_WEAPON_STATS; w++) {
        if (!weapons->info[w].name) continue;
        HudWeaponStat *s = &d->weapon_stats[d->weapon_stat_count++];
        *s = f->stats[w];
        s->weapon = (WeaponId)w;
        snprintf(s->name, sizeof s->name, "%s", weapons->info[w].name);
    }
    d->shot_distance_shown = f->shot_ticks > 0;
    d->shot_distance = f->shot_distance;
    d->shot_airtime = f->shot_airtime;
    d->shot_ricochets = f->shot_ricochets;
    d->kill_count = f->kill_count;
    for (int i = 0; i < f->kill_count; i++) d->kills[i] = f->kills[i];
    d->big_count = HUD_BIG_MESSAGES;
    for (int i = 0; i < HUD_BIG_MESSAGES; i++) d->big[i] = f->big[i];
}
