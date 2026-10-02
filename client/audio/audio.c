#include "audio/audio.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <string.h>

#include "game/systems/systems.h"
#include "render/sparks.h"

#define SOUND_MAXDIST 750.0f
#define SOUND_METERLENGTH 2000.0f
#define GRENADE_EFFECT_DIST 38.0f
#define GRENADE_EFFECT_TIME 320
#define CORPSE_CRACK_FALL 2.5f // a body falling this fast onto the map cracks
#define CORPSE_CRACK_HITS 3    // for its first landings only

// ---- the device ----

// SDL's thread: every playing voice into the buffer.
static void mix(void *user, Uint8 *stream, int len)
{
    Audio *a = user;
    float *out = (float *)stream;
    int frames = len / (int)(2 * sizeof(float));
    memset(stream, 0, (size_t)len);
    for (int v = 0; v < AUDIO_VOICES; v++) {
        Voice *voice = &a->voices[v];
        if (!voice->sample || voice->paused) continue;
        const float *in = voice->sample->frames;
        int left = voice->sample->count - voice->cursor;
        int n = left < frames ? left : frames;
        for (int i = 0; i < n; i++) {
            out[2 * i] += in[2 * (voice->cursor + i)] * voice->left;
            out[2 * i + 1] += in[2 * (voice->cursor + i) + 1] * voice->right;
        }
        voice->cursor += n;
        if (voice->cursor >= voice->sample->count) voice->sample = NULL; // over
    }
    for (int i = 0; i < 2 * frames; i++) out[i] = clampf(out[i], -1.0f, 1.0f);
}

bool audio_init(Audio *a, const char *base)
{
    *a = (Audio){.rng = 0x9E3779B1ull, .volume = 0.12f};
    snprintf(a->dir, sizeof a->dir, "%s/sfx", base);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "no audio: %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want = {.freq = AUDIO_RATE, .format = AUDIO_F32SYS, .channels = 2, .samples = 1024, .callback = mix, .userdata = a};
    SDL_AudioSpec have;
    a->device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!a->device) {
        fprintf(stderr, "no audio device: %s\n", SDL_GetError());
        return false;
    }
    a->ready = true;
    SDL_PauseAudioDevice(a->device, 0);
    return true;
}

void audio_shutdown(Audio *a)
{
    if (a->device) SDL_CloseAudioDevice(a->device);
    for (int i = 0; i < a->sample_count; i++) SDL_free(a->samples[i].frames);
    *a = (Audio){0};
}

void audio_volume(Audio *a, float volume) { a->volume = clampf(volume, 0.0f, 1.0f); }

// A sample by file name, read the first time it is asked for, into the device's format.
static const Sample *sample(Audio *a, const char *name)
{
    for (int i = 0; i < a->sample_count; i++)
        if (strcmp(a->samples[i].name, name) == 0) return &a->samples[i];
    if (a->sample_count == AUDIO_SAMPLES) return NULL;
    Sample *s = &a->samples[a->sample_count++];
    snprintf(s->name, sizeof s->name, "%s", name);

    char path[600];
    snprintf(path, sizeof path, "%s/%s", a->dir, name);
    SDL_AudioSpec spec;
    Uint8 *data;
    Uint32 size;
    if (!SDL_LoadWAV(path, &spec, &data, &size)) {
        fprintf(stderr, "sfx %s not found\n", name);
        return s;
    }
    SDL_AudioCVT cvt;
    int ok = SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_F32SYS, 2, AUDIO_RATE);
    if (ok < 0) {
        SDL_FreeWAV(data);
        return s;
    }
    if (ok == 0) { // already the device's format
        s->frames = SDL_malloc(size);
        if (s->frames) memcpy(s->frames, data, size);
        s->count = (int)(size / (2 * sizeof(float)));
        SDL_FreeWAV(data);
        return s;
    }
    cvt.len = (int)size;
    cvt.buf = SDL_malloc((size_t)cvt.len * (size_t)cvt.len_mult);
    if (!cvt.buf) {
        SDL_FreeWAV(data);
        return s;
    }
    memcpy(cvt.buf, data, size);
    SDL_FreeWAV(data);
    if (SDL_ConvertAudio(&cvt) != 0) {
        SDL_free(cvt.buf);
        return s;
    }
    s->frames = (float *)cvt.buf;
    s->count = cvt.len_cvt / (int)(2 * sizeof(float));
    return s;
}

// ---- playing ----

// Gain and pan for a sound at `at`, and whether it is within earshot. The pan is what
// OpenAL gave the original: the source at (dx, dy, -1000) meters, heard on x.
static bool place(const Audio *a, Vec2 at, bool distant, float *left, float *right)
{
    Vec2 d = vec2_sub(at, a->listener);
    float dist = vec2_length(d) / SOUND_MAXDIST;
    if (distant) dist = dist > 1.0f ? dist - 1.0f : 1.0f - 2.0f * dist;
    if (a->ringing > 0) dist += (1.0f - dist) * sqrtf((float)a->ringing / 280.0f);
    if (dist > 1.0f) return false;
    float gain = clampf(a->volume * (1.0f - dist), 0.0f, 1.0f);
    float sx = d.x / SOUND_METERLENGTH, sy = d.y / SOUND_METERLENGTH, sz = -1000.0f / SOUND_METERLENGTH;
    float pan = 0.5f + 0.5f * sx / sqrtf(sx * sx + sy * sy + sz * sz);
    *left = gain * cosf(pan * (float)M_PI / 2.0f);
    *right = gain * sinf(pan * (float)M_PI / 2.0f);
    return true;
}

// A free voice, or the oldest playing one.
static int voice_take(Audio *a)
{
    int oldest = 0;
    for (int v = 0; v < AUDIO_VOICES; v++) {
        if (!a->voices[v].sample) return v;
        if (a->voices[v].started < a->voices[oldest].started) oldest = v;
    }
    // a reserved voice stolen is let go of
    for (int i = 0; i < MAX_PLAYERS; i++)
        for (int k = 0; k < VOICE_COUNT; k++)
            if (a->reserved[i][k].voice == oldest + 1) a->reserved[i][k] = (Reserved){0};
    if (a->weather.voice == oldest + 1) a->weather = (Reserved){0};
    return oldest;
}

static void voice_start(Audio *a, int v, const Sample *s, float left, float right)
{
    a->voices[v] = (Voice){.sample = s, .left = left, .right = right, .started = ++a->plays};
}

static const char *pick(Audio *a, const char *const *names, int n) { return names[rand_int(&a->rng, n)]; }

// The far versions (Sound.pas FPlaySound): blasts by kind, gunfire as one of four.
static const char *distant_sample(Audio *a, const char *name)
{
    static const char *const DIST_GUNS[] = {"dist-gun1.wav", "dist-gun2.wav", "dist-gun3.wav", "dist-gun4.wav"};
    if (strcmp(name, "m79-explosion.wav") == 0) return "dist-m79.wav";
    if (strcmp(name, "grenade-explosion.wav") == 0 || strcmp(name, "clustergrenade.wav") == 0 ||
        strcmp(name, "cluster-explosion.wav") == 0)
        return "dist-grenade.wav";
    static const char *const GUNS[] = {"ak74-fire.wav",      "m249-fire.wav",     "ruger77-fire.wav", "spas12-fire.wav",  "deserteagle-fire.wav",
                                       "steyraug-fire.wav",  "barretm82-fire.wav", "minigun-fire.wav", "colt1911-fire.wav"};
    for (size_t i = 0; i < sizeof GUNS / sizeof GUNS[0]; i++)
        if (strcmp(name, GUNS[i]) == 0) return pick(a, DIST_GUNS, 4);
    return NULL;
}

// Sound.pas FPlaySound: a one-shot at `at`. Past half the range a shot or blast also
// plays its distant sample, which has its own fade.
static void sound_at(Audio *a, const char *name, Vec2 at, bool distant)
{
    if (!a->ready || !name) return;
    if (!distant && vec2_length(vec2_sub(at, a->listener)) > SOUND_MAXDIST / 2) {
        const char *alt = distant_sample(a, name);
        if (alt) sound_at(a, alt, at, true);
    }
    float left, right;
    if (!place(a, at, distant, &left, &right)) return;
    const Sample *s = sample(a, name);
    if (!s || !s->frames) return;
    SDL_LockAudioDevice(a->device);
    voice_start(a, voice_take(a), s, left, right);
    SDL_UnlockAudioDevice(a->device);
}

static void sound_play(Audio *a, const char *name, Vec2 at) { sound_at(a, name, at, false); }

void audio_flat(Audio *a, const char *name) { sound_at(a, name, a->camera, false); }

static bool reserved_playing(const Audio *a, const Reserved *r)
{
    if (!r->voice) return false;
    const Voice *v = &a->voices[r->voice - 1];
    return v->sample && v->started == r->started;
}

static void reserved_stop(Audio *a, Reserved *r)
{
    if (reserved_playing(a, r)) {
        SDL_LockAudioDevice(a->device);
        a->voices[r->voice - 1].sample = NULL;
        SDL_UnlockAudioDevice(a->device);
    }
    *r = (Reserved){0};
}

static void voice_stop(Audio *a, int slot, ReservedVoice kind) { reserved_stop(a, &a->reserved[slot][kind]); }

// A reserved voice: refreshed while it plays, restarted with `name` once it has ended,
// so a loop lives by being played every tick.
static void reserved_play(Audio *a, Reserved *r, const char *name, Vec2 at)
{
    if (!a->ready || !name) return;
    float left, right;
    if (!place(a, at, false, &left, &right)) {
        reserved_stop(a, r);
        return;
    }
    const Sample *s = sample(a, name); // read before the lock: a file is slow
    if (!s || !s->frames) {
        reserved_stop(a, r);
        return;
    }
    SDL_LockAudioDevice(a->device);
    bool playing = reserved_playing(a, r) && !a->voices[r->voice - 1].paused;
    if (!playing) {
        int v = voice_take(a);
        voice_start(a, v, s, left, right);
        r->voice = v + 1;
        r->started = a->voices[v].started;
        snprintf(r->name, sizeof r->name, "%s", name);
    }
    a->voices[r->voice - 1].left = left;
    a->voices[r->voice - 1].right = right;
    SDL_UnlockAudioDevice(a->device);
}

// A soldier's reserved voice (the layout of Sprites.pas: reload, jets, gattling, gattling2).
static void voice_play(Audio *a, int slot, ReservedVoice kind, const char *name, Vec2 at)
{
    reserved_play(a, &a->reserved[slot][kind], name, at);
}

// SetSoundPaused: pauses only a playing voice, resumes only a paused one.
static void voice_pause(Audio *a, int slot, ReservedVoice kind, bool paused)
{
    Reserved *r = &a->reserved[slot][kind];
    if (!reserved_playing(a, r)) return;
    a->voices[r->voice - 1].paused = paused;
}

// ---- the tables ----

// Fire sounds by weapon (TSprite.Fire). The knife, the fists and the chainsaw make no
// sound when they fire: their swings and the chainsaw loop follow the animation.
static const char *const FIRE_SOUNDS[WEAPON_COUNT] = {
    [WEAPON_EAGLE] = "deserteagle-fire.wav", [WEAPON_MP5] = "mp5-fire.wav",       [WEAPON_AK74] = "ak74-fire.wav",
    [WEAPON_STEYR] = "steyraug-fire.wav",    [WEAPON_SPAS] = "spas12-fire.wav",   [WEAPON_RUGER] = "ruger77-fire.wav",
    [WEAPON_M79] = "m79-fire.wav",           [WEAPON_BARRETT] = "barretm82-fire.wav", [WEAPON_M249] = "m249-fire.wav",
    [WEAPON_MINIGUN] = "minigun-fire.wav",   [WEAPON_COLT] = "colt1911-fire.wav", [WEAPON_LAW] = "law.wav",
    [WEAPON_BOW] = "bow-fire.wav",           [WEAPON_BOW2] = "bow-fire.wav",      [WEAPON_M2] = "m2fire.wav",
    [WEAPON_FRAG] = "grenade-throw.wav",     [WEAPON_CLUSTER_NADE] = "grenade-throw.wav",
};

static const char *const RELOAD_SOUNDS[WEAPON_COUNT] = {
    [WEAPON_EAGLE] = "deserteagle-reload.wav", [WEAPON_MP5] = "mp5-reload.wav",     [WEAPON_AK74] = "ak74-reload.wav",
    [WEAPON_STEYR] = "steyraug-reload.wav",    [WEAPON_RUGER] = "ruger77-reload.wav", [WEAPON_M79] = "m79-reload.wav",
    [WEAPON_BARRETT] = "barretm82-reload.wav", [WEAPON_M249] = "m249-reload.wav",   [WEAPON_MINIGUN] = "minigun-reload.wav",
    [WEAPON_COLT] = "colt1911-reload.wav",
};

static const char *const KIT_SOUNDS[THING_STYLE_COUNT] = {
    [THING_MEDICAL_KIT] = "takemedikit.wav", [THING_GRENADE_KIT] = "pickupgun.wav", [THING_FLAMER_KIT] = "godflame.wav",
    [THING_PREDATOR_KIT] = "predator.wav",   [THING_VEST_KIT] = "vesttake.wav",     [THING_BERSERK_KIT] = "berserker.wav",
    [THING_CLUSTER_KIT] = "pickupgun.wav",
};

static const char *const RIC[] = {"ric.wav", "ric2.wav", "ric3.wav", "ric4.wav"};
static const char *const RICOCHETS[] = {"ric5.wav", "ric6.wav", "ric7.wav"};
static const char *const HIT_ARG[] = {"hit-arg.wav", "hit-arg2.wav", "hit-arg3.wav"};
static const char *const DEATHS[] = {"death.wav", "death2.wav", "death3.wav"};
static const char *const FLAGS[] = {"flag.wav", "flag2.wav"};
static const char *const KIT_FALL[] = {"kit-fall.wav", "kit-fall2.wav"};
static const char *const WHIZ[] = {"bulletby2.wav", "bulletby3.wav", "bulletby4.wav", "bulletby5.wav"};
static const char *const STEPS[] = {"step.wav",  "step2.wav", "step3.wav", "step4.wav",
                                    "step5.wav", "step6.wav", "step7.wav", "step8.wav"};

// ---- the events ----

// TSprite.Die and the kill message: the death by how bad it was.
static void audio_kill(Audio *a, const EventKill *e, int me)
{
    Vec2 head = vec2_sub(e->pos, vec2(0, 12));
    bool headchop = e->health <= HEADCHOP_DEATH_HEALTH || (e->part == 12 && e->weapon == WEAPON_RUGER);
    if (e->health <= BRUTAL_DEATH_HEALTH) {
        sound_play(a, "bryzg.wav", head);
    } else if (headchop) {
        if (e->part == 12 && (e->weapon == WEAPON_BARRETT || e->weapon == WEAPON_RUGER)) {
            if (e->weapon == WEAPON_BARRETT) sound_play(a, "bryzg.wav", head);
            if (e->killer == me) audio_flat(a, "boomheadshot.wav");
        }
        sound_play(a, "headchop.wav", head);
    } else {
        sound_play(a, pick(a, DEATHS, 3), e->pos);
    }
    if (e->weapon == WEAPON_FLAMER) sound_play(a, "burn.wav", head);
    voice_stop(a, e->target, VOICE_RELOAD);
    if (e->target == me) audio_flat(a, "playerdeath.wav");
}

static void audio_event(Audio *a, const Event *e, const World *w, int me)
{
    switch (e->type) {
    case EVENT_FIRE: {
        const EventFire *f = &e->fire;
        if (w->soldiers[f->player].bonus == BONUS_PREDATOR) return; // a predator fires silently
        if (f->weapon == WEAPON_FLAMER) voice_play(a, f->player, VOICE_GATTLING, "flamer.wav", f->pos);
        else sound_play(a, FIRE_SOUNDS[f->weapon], f->pos);
        break;
    }
    case EVENT_EXPLOSION: {
        const EventExplosion *x = &e->explosion;
        const Soldier *mine = &w->soldiers[me];
        if (mine->active && mine->health > -50.0f && vec2_length(vec2_sub(x->pos, mine->pos)) < GRENADE_EFFECT_DIST) {
            a->ringing = GRENADE_EFFECT_TIME;
            audio_flat(a, "hum.wav");
        }
        const char *name = "explosion-erg.wav";
        switch (x->weapon) {
        case WEAPON_M79: name = "m79-explosion.wav"; break;
        case WEAPON_M2: name = "m2explode.wav"; break;
        case WEAPON_FRAG: name = x->radius <= 35.0f ? "cluster-explosion.wav" : "grenade-explosion.wav"; break;
        default: break;
        }
        sound_play(a, name, x->pos);
        for (int i = 0; i < MAX_PLAYERS; i++) {
            const Soldier *s = &w->soldiers[i];
            if (s->active && !s->dead && s->team != TEAM_SPECTATOR && vec2_length(vec2_sub(s->pos, x->pos)) < x->radius) {
                sound_play(a, "explosion-erg.wav", s->pos);
            }
        }
        break;
    }
    case EVENT_CLUSTER_SPLIT: sound_play(a, "clustergrenade.wav", e->cluster_split.pos); break;
    case EVENT_GRENADE_BOUNCE: sound_play(a, "grenade-bounce.wav", e->grenade_bounce.pos); break;
    case EVENT_RICOCHET: sound_play(a, pick(a, RICOCHETS, 3), e->ricochet.pos); break;
    case EVENT_WALL_HIT: sound_play(a, pick(a, RIC, 4), e->wall_hit.pos); break;
    case EVENT_COLLIDER_HIT:
        sound_play(a, "colliderhit.wav", e->collider_hit.pos);
        sound_play(a, pick(a, RIC, 4), e->collider_hit.pos);
        break;
    case EVENT_BLOOD: {
        const Soldier *target = &w->soldiers[e->blood.target];
        if (target->dead) sound_play(a, "dead-hit.wav", e->blood.pos);
        else if (target->vest > 0.0f) sound_play(a, "vesthit.wav", e->blood.pos);
        else sound_play(a, pick(a, HIT_ARG, 3), e->blood.pos);
        break;
    }
    case EVENT_CORPSE_HIT:
        // the thud of a body landing, and the crack of bones on a hard one; both quieten
        // as the body settles, so a corpse rolling to a stop does not rattle on
        sound_play(a, "bodyfall.wav", e->corpse_hit.pos);
        if (e->corpse_hit.fall > CORPSE_CRACK_FALL && e->corpse_hit.count < CORPSE_CRACK_HITS) sound_play(a, "bonecrack.wav", e->corpse_hit.pos);
        break;
    case EVENT_KILL:
        audio_kill(a, &e->kill, me);
        if (w->soldiers[e->kill.killer].bonus == BONUS_BERSERKER && e->kill.killer != e->kill.target) {
            sound_play(a, "killberserk.wav", vec2_sub(e->kill.pos, vec2(0, 12)));
        }
        break;
    case EVENT_RESPAWN:
        if (e->respawn.target == me) sound_play(a, "wermusic.wav", e->respawn.pos);
        else sound_play(a, "spawn.wav", e->respawn.pos);
        break;
    case EVENT_POLY_EFFECT:
        switch (e->poly_effect.type) {
        case POLY_HURTS: sound_play(a, "arg.wav", e->poly_effect.pos); break;
        case POLY_LAVA: sound_play(a, "lava.wav", e->poly_effect.pos); break;
        case POLY_REGENERATES: sound_play(a, "regenerate.wav", e->poly_effect.pos); break;
        case POLY_EXPLODES: sound_play(a, "explosion-erg.wav", e->poly_effect.pos); break;
        case POLY_BOUNCY: sound_play(a, "bounce.wav", e->poly_effect.pos); break;
        default: break;
        }
        break;
    // The flag's sounds, all from where it happened. The original places the grab and
    // plays the return, the score and the drop flat, from the camera, so a flag returned
    // across the map sounds as if it were returned beside you; a departure, on purpose.
    case EVENT_FLAG_GRAB: sound_play(a, "capture.wav", e->flag_grab.pos); break;
    case EVENT_FLAG_RETURN: sound_play(a, "capture.wav", e->flag_return.pos); break;
    case EVENT_FLAG_SCORE: sound_play(a, "ctf.wav", e->flag_score.pos); break;
    case EVENT_FLAG_DROP:
        if (w->soldiers[e->flag_drop.player].team == w->soldiers[me].team) sound_play(a, "infilt-point.wav", e->flag_drop.pos);
        break;
    case EVENT_KIT_PICKUP: {
        const char *name = KIT_SOUNDS[e->kit_pickup.kit];
        sound_play(a, name ? name : "pickupgun.wav", e->kit_pickup.pos);
        break;
    }
    case EVENT_WEAPON_PICKUP: sound_play(a, "takegun.wav", e->weapon_pickup.pos); break;
    case EVENT_ANTIC: // the spit, and the puff of smoke that lights the cigar or is drawn on it
        if (e->antic.kind == ANTIC_SPIT) sound_play(a, "spit.wav", w->soldiers[e->antic.player].pos);
        if (e->antic.kind == ANTIC_CIGAR_PUFF) sound_play(a, "smoke.wav", w->soldiers[e->antic.player].pos);
        break;
    case EVENT_THING_HIT:
        // a landing, or (part 0) cloth flapping
        switch (e->thing_hit.thing) {
        case THING_ALPHA_FLAG:
        case THING_BRAVO_FLAG:
        case THING_PARACHUTE: sound_play(a, pick(a, FLAGS, 2), e->thing_hit.pos); break;
        case THING_WEAPON: sound_play(a, "weaponhit.wav", e->thing_hit.pos); break;
        case THING_STAT_GUN: break;
        default:
            if (e->thing_hit.part != 0) sound_play(a, pick(a, KIT_FALL, 2), e->thing_hit.pos);
            break;
        }
        break;
    default: break;
    }
}

// The time-left beeps, closer together toward the end.
static void audio_clock(Audio *a, const Match *m)
{
    if (m->state != MATCH_PLAYING) return;
    int32_t t = m->time_left;
    bool beep = false;
    if (t >= 1 && t <= 600) beep = t % 60 == 0;
    else if (t > 600 && t <= 3600) beep = t % 600 == 0;
    else if (t > 3600 && t <= 18000) beep = t % 3600 == 0;
    else if (t > 18000) beep = t % 18000 == 0;
    if (beep) audio_flat(a, "signal.wav");
}

// ---- the soldiers, tick to tick ----

// Whether an animation went past `frame` between two ticks. A restart of the same
// animation (the frame going backwards) counts the frames after the wrap.
static bool crossed(const Anim *prev, const Anim *cur, AnimId id, int32_t frame)
{
    if (cur->id != id) return false;
    if (prev->id != id) return cur->frame >= frame && cur->frame <= frame + 1;
    if (cur->frame >= prev->frame) return prev->frame < frame && frame <= cur->frame;
    return frame > prev->frame || frame <= cur->frame;
}

static bool is_one_of(AnimId id, const AnimId *ids, int n)
{
    for (int i = 0; i < n; i++)
        if (ids[i] == id) return true;
    return false;
}

// One soldier for one tick against how it was the tick before: the sounds its
// animations and weapon make (the play sites of Sprites.pas).
static void audio_soldier(Audio *a, const Context *ctx, int slot, const Soldier *s, uint32_t tick)
{
    Soldier p = a->prev[slot];
    a->prev[slot] = *s;
    if (!s->active || s->dead) {
        if (p.active && !p.dead)
            for (int v = 0; v < VOICE_COUNT; v++) voice_stop(a, slot, (ReservedVoice)v);
        return;
    }
    bool fresh = !p.active || p.dead;
    const WeaponInfo *info = &ctx->weapons.info[s->weapon.id];
    Vec2 at = s->pos;
    Buttons c = s->controls;

    // jets: the rocket loop while jetting, except during a jet-assisted backflip
    bool backflip = (c & BUTTON_JET) &&
                    ((s->legs.id == ANIM_JUMP_SIDE && ((s->direction == -1 && (c & BUTTON_RIGHT)) || (s->direction == 1 && (c & BUTTON_LEFT)))) ||
                     (s->legs.id == ANIM_ROLL_BACK && (c & BUTTON_JUMP)));
    if (!backflip) {
        if (s->gear == GEAR_JETS && (c & BUTTON_JET) && s->jets > 0) voice_play(a, slot, VOICE_JETS, "rocketz.wav", at);
        else voice_stop(a, slot, VOICE_JETS);
    }

    // the chainsaw: its idle rattle every 15 ticks, the cutting loop while the trigger is held
    bool fire = (c & BUTTON_FIRE) && s->cease_fire_counter < 0;
    if (s->weapon.id == WEAPON_CHAINSAW) {
        if (tick % 15 == 0) {
            if (s->weapon.ammo == 0) voice_play(a, slot, VOICE_GATTLING, "chainsaw-o.wav", at);
            else sound_play(a, "chainsaw-m.wav", at);
        }
        if ((c & BUTTON_FIRE) && s->weapon.ammo > 0) voice_play(a, slot, VOICE_GATTLING, "chainsaw-r.wav", at);
    }

    // wind-ups: the first wind-up tick shows as the start-up counter stepping down
    static const AnimId NOT_FIRING[] = {ANIM_ROLL, ANIM_ROLL_BACK, ANIM_MELEE, ANIM_CHANGE};
    bool firing_allowed = (s->weapon.id == WEAPON_CHAINSAW || !is_one_of(s->body.id, NOT_FIRING, 4)) &&
                          (s->body.id != ANIM_HANDS_UP_AIM || s->body.frame == 11);
    if (firing_allowed && !fresh && p.weapon.id == s->weapon.id) {
        int32_t su = info->stats.startup;
        bool law_ready = s->on_ground && (s->legs.id == ANIM_CROUCH_RUN || s->legs.id == ANIM_CROUCH_RUN_BACK ||
                                          (s->legs.id == ANIM_CROUCH && s->legs.frame > 13) || (s->legs.id == ANIM_PRONE && s->legs.frame > 23));
        if (fire) {
            if (su > 0 && p.weapon.startup_count == su && s->weapon.startup_count == su - 1) {
                voice_stop(a, slot, VOICE_GATTLING2);
                switch (s->weapon.id) {
                case WEAPON_BARRETT: voice_play(a, slot, VOICE_GATTLING, "law-start.wav", at); break;
                case WEAPON_MINIGUN: voice_play(a, slot, VOICE_GATTLING, "minigun-start.wav", at); break;
                case WEAPON_LAW:
                    if (law_ready) voice_play(a, slot, VOICE_GATTLING, "law-start.wav", at);
                    break;
                default: break;
                }
            }
        } else {
            voice_stop(a, slot, VOICE_GATTLING);
            if (su > 0 && p.weapon.startup_count < su && s->weapon.startup_count == su) {
                if (s->weapon.id == WEAPON_MINIGUN) voice_play(a, slot, VOICE_GATTLING2, "minigun-end.wav", at);
                else if (s->weapon.id == WEAPON_LAW && law_ready) voice_play(a, slot, VOICE_GATTLING2, "law-end.wav", at);
            }
        }
    } else if (firing_allowed && !fire) {
        voice_stop(a, slot, VOICE_GATTLING);
    }

    // reloading: the clip sound starts with the reload and pauses while the soldier
    // rolls, changes weapons or throws a grenade
    if (s->weapon.ammo == 0) {
        bool started = fresh || p.weapon.ammo != 0 || p.weapon.id != s->weapon.id || s->weapon.reload_count > p.weapon.reload_count;
        if (started) voice_play(a, slot, VOICE_RELOAD, RELOAD_SOUNDS[s->weapon.id], at);
        static const AnimId BUSY[] = {ANIM_ROLL, ANIM_ROLL_BACK, ANIM_MELEE, ANIM_CHANGE, ANIM_THROW, ANIM_THROW_WEAPON};
        bool busy = is_one_of(s->body.id, BUSY, 6);
        if (s->weapon.id == WEAPON_CHAINSAW || !busy) voice_pause(a, slot, VOICE_RELOAD, false);
    }
    if (!fresh) {
        if ((s->body.id == ANIM_CHANGE && p.body.id != ANIM_CHANGE) || (s->body.id == ANIM_THROW && p.body.id != ANIM_THROW)) {
            voice_pause(a, slot, VOICE_RELOAD, true);
        }
        if (s->body.id == ANIM_THROW_WEAPON && p.body.id != ANIM_THROW_WEAPON) voice_stop(a, slot, VOICE_RELOAD);
    }
    if (crossed(&p.body, &s->body, ANIM_RELOAD, 7)) voice_play(a, slot, VOICE_RELOAD, "spas12-reload.wav", at);

    // the weapon change: the sound of the one being drawn
    if (crossed(&p.body, &s->body, ANIM_CHANGE, 2)) {
        switch (s->secondary.id) {
        case WEAPON_COLT: sound_play(a, "changespin.wav", at); break;
        case WEAPON_KNIFE: sound_play(a, "knife.wav", at); break;
        case WEAPON_CHAINSAW: sound_play(a, "chainsaw-d.wav", at); break;
        default: sound_play(a, "changeweapon.wav", at); break;
        }
    }
    if (crossed(&p.body, &s->body, ANIM_THROW_WEAPON, 2)) sound_play(a, "throwgun.wav", at);

    // melee: a knife stab or a rifle butt
    if (crossed(&p.body, &s->body, ANIM_PUNCH, 11) && s->weapon.id == WEAPON_KNIFE) sound_play(a, "slash.wav", at);
    if (crossed(&p.body, &s->body, ANIM_MELEE, 12)) sound_play(a, "slash.wav", at);

    // the grenade's pin, from about where the hand is
    if (crossed(&p.body, &s->body, ANIM_THROW, 15) && s->grenades > 0 && s->cease_fire_counter < 0) {
        sound_play(a, "grenade-pullout.wav", vec2_sub(at, vec2(0, 2)));
    }

    if (fresh) return;

    // the stationary gun (Things.pas): the clank of taking it, and its clicking on the
    // firing beat while the trigger is held past the overheat
    if (p.stat == 0 && s->stat != 0) sound_play(a, "m2use.wav", at);
    if (s->stat != 0 && (c & BUTTON_FIRE) && s->legs.id == ANIM_STAND && s->use_time > M2_OVERHEAT &&
        (tick - 1) % (uint32_t)ctx->weapons.info[WEAPON_M2].stats.fire_interval == 0) {
        sound_play(a, "m2overheat.wav", at);
    }

    // the antics (the IDLE block of Control.pas): the tobacco's "stuff" as the chew steps
    // over its 17th frame, the match struck as the cigar's ninth passes with the cigar in
    // the mouth, the roar of a victory, the piss, the mercy's plea (and the minigun's
    // spin-up with it), and at its 20th frame the blade, the saw or the bare hand on
    // the head, on the gattling voice as the original has them
    if (crossed(&p.body, &s->body, ANIM_SMOKE, 17) && s->idle.random == 0) sound_play(a, "stuff.wav", at);
    if (crossed(&p.body, &s->body, ANIM_CIGAR, 9) && s->has_cigar == 5) sound_play(a, "match.wav", at);
    if (s->body.id == ANIM_VICTORY && p.body.id != ANIM_VICTORY) sound_play(a, "roar.wav", at);
    if (s->body.id == ANIM_PISS && p.body.id != ANIM_PISS) sound_play(a, "piss.wav", at);
    bool mercy_now = s->body.id == ANIM_MERCY || s->body.id == ANIM_MERCY2;
    bool mercy_then = p.body.id == ANIM_MERCY || p.body.id == ANIM_MERCY2;
    if (mercy_now && !mercy_then) {
        sound_play(a, "mercy.wav", at);
        if (s->weapon.id == WEAPON_MINIGUN) sound_play(a, "minigun-start.wav", at);
    }
    if (mercy_now && mercy_then && crossed(&p.body, &s->body, s->body.id, 20)) {
        if (s->weapon.id == WEAPON_KNIFE) voice_play(a, slot, VOICE_GATTLING, "slash.wav", at);
        if (s->weapon.id == WEAPON_CHAINSAW) voice_play(a, slot, VOICE_GATTLING, "chainsaw-r.wav", at);
        if (s->weapon.id == WEAPON_NONE) voice_play(a, slot, VOICE_GATTLING, "dead-hit.wav", at);
    }

    // legs: going prone, standing up, rolling, jumping, crouching, stopping
    static const AnimId PRONES[] = {ANIM_PRONE, ANIM_PRONE_MOVE, ANIM_GET_UP};
    static const AnimId ROLLS[] = {ANIM_ROLL, ANIM_ROLL_BACK};
    static const AnimId JUMPS[] = {ANIM_JUMP, ANIM_JUMP_SIDE};
    static const AnimId CROUCHES[] = {ANIM_CROUCH, ANIM_CROUCH_RUN, ANIM_CROUCH_RUN_BACK};
    if (s->legs.id == ANIM_PRONE && !is_one_of(p.legs.id, PRONES, 3)) sound_play(a, "goprone.wav", at);
    if (s->legs.id == ANIM_GET_UP && p.legs.id != ANIM_GET_UP) sound_play(a, "standup.wav", at);
    if (is_one_of(s->legs.id, ROLLS, 2) && !is_one_of(p.legs.id, ROLLS, 2)) {
        sound_play(a, "roll.wav", at);
        voice_pause(a, slot, VOICE_RELOAD, true);
    }
    if (is_one_of(s->legs.id, JUMPS, 2) && !is_one_of(p.legs.id, JUMPS, 2) && p.on_ground) sound_play(a, "jump.wav", at);
    if (s->legs.id == ANIM_CROUCH && !is_one_of(p.legs.id, CROUCHES, 3) && s->on_ground) sound_play(a, "crouch.wav", at);
    if (s->legs.id == ANIM_STAND && p.legs.id != ANIM_STAND && s->on_ground && !(c & BUTTON_LEFT) && !(c & BUTTON_RIGHT)) {
        sound_play(a, "stop.wav", at);
    }

    // footsteps, while touching the ground
    if (s->on_ground) {
        bool running = s->legs.id == ANIM_RUN || s->legs.id == ANIM_RUN_BACK;
        if (running && (crossed(&p.legs, &s->legs, s->legs.id, 16) || crossed(&p.legs, &s->legs, s->legs.id, 32))) {
            if (ctx->map->weather == 1) sound_play(a, "water-step.wav", at);
            else if (ctx->map->steps == 0) sound_play(a, STEPS[rand_int(&a->rng, 4)], at);
            else sound_play(a, STEPS[4 + rand_int(&a->rng, 4)], at);
        }
        bool crouching = s->legs.id == ANIM_CROUCH_RUN || s->legs.id == ANIM_CROUCH_RUN_BACK;
        if (crouching && (crossed(&p.legs, &s->legs, s->legs.id, 15) || crossed(&p.legs, &s->legs, s->legs.id, 1))) {
            if (rand_int(&a->rng, 2) == 0) sound_play(a, "crouch-move.wav", at);
            else if (rand_int(&a->rng, 2) == 0) sound_play(a, "crouch-movel.wav", at);
        }
        if (crossed(&p.legs, &s->legs, ANIM_PRONE_MOVE, 8)) sound_play(a, "prone-move.wav", at);
    }

    // the sniper view (Control.pas): the scope as it begins and as it is back, and its
    // running while the aim distance moves, every 27 ticks
    if (p.aim_dist >= DEFAULT_AIM_DIST && s->aim_dist < DEFAULT_AIM_DIST) sound_play(a, "scope.wav", at);
    else if (p.aim_dist < DEFAULT_AIM_DIST && s->aim_dist >= DEFAULT_AIM_DIST) {
        sound_play(a, s->weapon.id == WEAPON_BARRETT && s->weapon.fire_count == 0 ? "scope.wav" : "scopeback.wav", at);
    }
    if (s->aim_dist != p.aim_dist && tick % 27 == 0) sound_play(a, "scoperun.wav", at);

    // landing, by how fast the soldier was falling
    if (s->on_ground && !p.on_ground) {
        float vy = fabsf(p.vel.y);
        if (vy > 2.2f && vy < 3.4f) sound_play(a, "fall.wav", at);
        if (vy > 3.5f) sound_play(a, "fall-hard.wav", at);
    }
}

// ---- bullets passing me ----

// A whistle 25 ticks into any round's flight but a shotgun's, and a whiz the first time
// a bullet enters the box around the soldier the camera follows, unless it is that
// soldier's own (CreateBullet marks those whizzed); nothing whizzes past the free camera.
static void audio_bullets(Audio *a, const Context *ctx, const World *w, int followed)
{
    for (int i = 0; i < MAX_BULLETS; i++) {
        const Bullet *b = &w->bullets[i];
        if (!b->active) {
            a->whizzed[i] = false;
            continue;
        }
        if (b->timeout == ctx->weapons.info[b->weapon].timeout - 25 && b->style != BULLET_SHOTGUN) sound_play(a, "bulletby.wav", b->pos);
        if (a->whizzed[i] || followed < 0 || b->owner == followed || b->style == BULLET_PUNCH || b->style == BULLET_FLAME) continue;
        Vec2 d = vec2_sub(b->pos, a->listener);
        if (d.x > -200 && d.x < 200 && d.y > -350 && d.y < 100) {
            sound_play(a, pick(a, WHIZ, 4), b->pos);
            a->whizzed[i] = true;
        }
    }
}

// What the sparks sounded like: casings and clips landing, a body burning.
static void audio_sparks(Audio *a, const Sparks *sparks)
{
    static const char *const SHELLS[] = {"shell.wav", "shell2.wav"};
    if (!sparks) return;
    for (int i = 0; i < sparks->sound_count; i++) {
        const SparkSound *s = &sparks->sounds[i];
        switch (s->noise) {
        case SPARK_NOISE_SHELL: sound_play(a, pick(a, SHELLS, 2), s->pos); break;
        case SPARK_NOISE_GAUGE_SHELL: sound_play(a, "gaugeshell.wav", s->pos); break;
        case SPARK_NOISE_CLIP: sound_play(a, "clipfall.wav", s->pos); break;
        case SPARK_NOISE_ONFIRE: sound_play(a, "onfire.wav", s->pos); break;
        case SPARK_NOISE_FIRECRACK: sound_play(a, "firecrack.wav", s->pos); break;
        }
    }
}

void audio_tick(Audio *a, const Game *g, int me, int followed, Vec2 camera, const Sparks *sparks)
{
    if (!a->ready) return;
    const World *w = &g->world;
    if (followed >= 0 && !w->soldiers[followed].active) followed = -1;
    a->camera = camera;
    a->listener = followed >= 0 ? w->soldiers[followed].pos : camera;
    if (a->ringing > -1) a->ringing--;
    // paused, the soldiers' loops stop (ClientHandleServerSyncMsg): a jet or a reload
    // would sound on for as long as the pause lasts; nothing new sounds till it ends
    if (g->match.state == MATCH_PAUSED) {
        for (int slot = 0; slot < MAX_PLAYERS; slot++)
            for (int v = 0; v < VOICE_COUNT; v++) voice_stop(a, slot, (ReservedVoice)v);
        return;
    }
    audio_clock(a, &g->match);
    for (int i = 0; i < g->events.count; i++) audio_event(a, &g->events.items[i], w, me);
    for (int i = 0; i < MAX_PLAYERS; i++) audio_soldier(a, &g->ctx, i, &w->soldiers[i], w->tick);
    audio_bullets(a, &g->ctx, w, followed);
    audio_sparks(a, sparks);

    // the weather (WeatherEffects.pas): the wind, the one loop the original gives rain,
    // sandstorm and snow alike, from the camera, kept up by being played every tick
    uint8_t weather = g->ctx.map->weather;
    if (weather >= 1 && weather <= 3) reserved_play(a, &a->weather, "sfx_wind.wav", a->camera);
    else reserved_stop(a, &a->weather);
}
