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
    case TEAM_ALPHA: return (Rgba){0xEA, 0x35, 0x30, 0xFF};
    case TEAM_BRAVO: return (Rgba){0x31, 0x31, 0xDF, 0xFF};
    case TEAM_CHARLIE: return (Rgba){0xDF, 0xDF, 0x53, 0xFF};
    case TEAM_DELTA: return (Rgba){0x53, 0xDF, 0x53, 0xFF};
    default: return (Rgba){0xFF, 0xFF, 0xFF, 0xFF};
    }
}

// The team whose flag a style is.
static Team flag_team(ThingStyle style)
{
    return style == THING_ALPHA_FLAG ? TEAM_ALPHA : style == THING_BRAVO_FLAG ? TEAM_BRAVO : TEAM_NONE;
}

// A line at the bottom of the kill console; the oldest goes when it is full.
static void kill_line(Feed *f, const char *text, Rgba color, WeaponId weapon, bool icon)
{
    if (f->kill_count == HUD_KILL_LINES) {
        memmove(f->kills, f->kills + 1, sizeof f->kills - sizeof f->kills[0]);
        memmove(f->kill_ticks, f->kill_ticks + 1, sizeof f->kill_ticks - sizeof f->kill_ticks[0]);
        f->kill_count--;
    }
    HudKillLine *l = &f->kills[f->kill_count];
    snprintf(l->text, sizeof l->text, "%s", text);
    l->color = color;
    l->weapon = weapon;
    l->has_icon = icon;
    f->kill_ticks[f->kill_count++] = FEED_KILL_TICKS;
}

// A big message on `layer`, replacing what was there.
static void big_text(Feed *f, int layer, const char *text, Rgba color, float scale, float x, float y)
{
    HudBigMessage *m = &f->big[layer];
    snprintf(m->text, sizeof m->text, "%s", text);
    m->color = color;
    m->scale = scale;
    m->delay = FEED_BIG_TICKS;
    m->x = x;
    m->y = y;
}

// A name's colour in the texts: its team's, or white with no teams.
static Rgba name_color(const Game *g, int player, bool team_game)
{
    return team_game ? team_color(g->world.soldiers[player].team) : (Rgba){0xFF, 0xFF, 0xFF, 0xFF};
}

// The kill console: the killer over its weapon's icon, the victim under; a suicide is the
// victim alone, with what did it.
static void kill(Feed *f, const Game *g, const char names[MAX_PLAYERS][HUD_NAME], bool team_game, const EventKill *k)
{
    if (k->killer != k->target) kill_line(f, names[k->killer], name_color(g, k->killer, team_game), k->weapon, true);
    kill_line(f, names[k->target], k->killer == k->target ? HUD_COLOR_DEATH : name_color(g, k->target, team_game), k->weapon,
              k->killer == k->target);
}

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
    big_text(f, 1, text, color, 0.1f, 80, 200);
    console_print_color(con, HUD_COLOR_GAME, "%s\n", text);
}

void feed_tick(Feed *f, Console *con, const Game *g, const char names[MAX_PLAYERS][HUD_NAME], bool team_game)
{
    // the lines age
    int kept = 0;
    for (int i = 0; i < f->kill_count; i++) {
        if (--f->kill_ticks[i] <= 0) continue;
        f->kills[kept] = f->kills[i];
        f->kill_ticks[kept++] = f->kill_ticks[i];
    }
    f->kill_count = kept;
    for (int i = 0; i < HUD_BIG_MESSAGES; i++)
        if (f->big[i].delay > 0) f->big[i].delay--;

    for (int i = 0; i < g->events.count; i++) {
        const Event *e = &g->events.items[i];
        char text[HUD_TEXT];
        switch (e->type) {
        case EVENT_KILL: kill(f, g, names, team_game, &e->kill); break;
        case EVENT_FLAG_SCORE: {
            Team flag = flag_team(e->flag_score.flag);
            snprintf(text, sizeof text, "%s Flag Captured!", team_name(flag));
            big_text(f, 0, text, (Rgba){0xD3, 0xCA, 0x34, 0xFF}, 0.0625f, 80, 240);
            console_print_color(con, HUD_COLOR_GAME, "%s captured the %s flag\n", names[e->flag_score.player], team_name(flag));
            break;
        }
        case EVENT_FLAG_GRAB:
            console_print_color(con, HUD_COLOR_GAME, "%s took the %s flag\n", names[e->flag_grab.player],
                                team_name(flag_team(e->flag_grab.flag)));
            break;
        case EVENT_FLAG_RETURN:
            if (e->flag_return.player == 255)
                console_print_color(con, HUD_COLOR_GAME, "The %s flag was returned\n", team_name(flag_team(e->flag_return.flag)));
            else
                console_print_color(con, HUD_COLOR_GAME, "%s returned the %s flag\n", names[e->flag_return.player],
                                    team_name(flag_team(e->flag_return.flag)));
            break;
        case EVENT_MATCH_END: match_end(f, con, g, names, e->match_end.winner); break;
        default: break;
        }
    }
}

void feed_fill(const Feed *f, HudData *d)
{
    d->kill_count = f->kill_count;
    for (int i = 0; i < f->kill_count; i++) d->kills[i] = f->kills[i];
    d->big_count = HUD_BIG_MESSAGES;
    for (int i = 0; i < HUD_BIG_MESSAGES; i++) d->big[i] = f->big[i];
}
