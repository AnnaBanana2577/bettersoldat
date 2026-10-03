#pragma once

// Sounds, from Sound.pas and the play sites in Sprites.pas, Bullets.pas and
// SpriteEffects.pas by way of soldat-odin's ss_audio.odin, on SDL's audio device:
//
//   - one sample per wav, read on first use into the device's format; a play takes a
//     free voice from a small pool, or the oldest playing
//   - every play is placed from the listener, the soldier the camera follows (me while I
//     live, whom I watch while dead) or the free camera: gain volume * (1 - d / 750), cut
//     past that, panned by the direction; a sound with no place comes from the camera
//   - four reserved voices per soldier (reload, jets, gattling, gattling2, the layout
//     of Sprites.pas): a voice already playing is refreshed, not restarted, and a
//     wind-up is cut by stopping its voice. The loops (jets, chainsaw, flamer, wind)
//     wrap in the mixer, as the original's AL_LOOPING sources do, while they are played
//     every tick; out of earshot a playing one is kept, silent, not stopped, as the
//     original leaves its source playing
//   - with snd_effects_battle, past half the range a shot or blast also plays its
//     distant sample; with snd_effects_explosions, a blast next to me rings the ears
//     (hum) and fades every sound but the hum for a few seconds. Both are off unless
//     asked for, as the original's are
//   - one departure from the original: everything that happens in the game is placed
//     where it happened, the flag's return, score and drop included, which the
//     original plays flat from the camera. What is mine alone (my death, my headshot,
//     the ringing, the clock, the menus) stays flat.
//
// What plays when: the events (each tick's), each soldier's state against the tick
// before, bullets passing me, and the clock's beeps, all from audio_tick once per
// tick. Purely a listener: nothing here changes the game.

#include <SDL.h>

#include "game/game.h"

typedef struct Sparks Sparks; // render/sparks.h: the sparks that make a sound

#define AUDIO_RATE 44100
#define AUDIO_VOICES 256 // the original's sources: 128 free and four per soldier
#define AUDIO_SAMPLES 192 // distinct wavs kept
#define AUDIO_NAME 48

typedef enum ReservedVoice { VOICE_RELOAD, VOICE_JETS, VOICE_GATTLING, VOICE_GATTLING2, VOICE_COUNT } ReservedVoice;

typedef struct Sample {
    char name[AUDIO_NAME];
    float *frames; // stereo, interleaved, at AUDIO_RATE; NULL when the file wasn't found
    int count;     // frames
} Sample;

typedef struct Voice {
    const Sample *sample; // NULL: free
    int cursor;           // frames played
    float left, right;    // the gains, from the placing
    bool paused;
    uint32_t started; // the play's number, to steal the oldest
    // A loop (jets, the chainsaw, the flamer, the wind: the original's AL_LOOPING) wraps
    // in the mixer, seamless, for as long as it is kept up: `held` frames more, which
    // each refresh renews, so one nobody refreshes (the ticks stopped) falls silent.
    bool loop;
    int held;
} Voice;

typedef struct Reserved {
    int voice; // index + 1 into the pool, 0 for none
    char name[AUDIO_NAME];
    uint32_t started;
} Reserved;

typedef struct Audio {
    SDL_AudioDeviceID device;
    char dir[512]; // the sfx directory
    Sample samples[AUDIO_SAMPLES];
    int sample_count;
    Voice voices[AUDIO_VOICES];
    Reserved reserved[MAX_PLAYERS][VOICE_COUNT];
    Reserved weather; // the wind, while the map has weather
    bool weather_off; // r_weathereffects 0: no wind, as the original plays it with the weather's sparks
    bool battle;      // snd_effects_battle: a far shot or blast also plays its distant sample
    bool explosions;  // snd_effects_explosions: a blast next to me rings the ears and deafens
    Soldier prev[MAX_PLAYERS];  // everyone as of the tick before
    bool whizzed[MAX_BULLETS];  // bullets that have already whizzed past the listener
    Vec2 listener, camera;
    int ringing; // ticks of ringing ears left
    uint64_t rng;
    float volume; // 0..1, the master
    uint32_t plays;
    bool ready;
} Audio;

// Opens the device; false, with the reason on stderr, when there is none. The game
// runs without sound then.
bool audio_init(Audio *a, const char *base);
void audio_shutdown(Audio *a);

// The master volume, 0 to 1 (snd_volume, through the original's curve).
void audio_volume(Audio *a, float volume);

// Once per tick, after the game's: where I listen from, then everything that sounded
// this tick. `me` is my soldier, for what is mine (my death, my kills, my ears);
// `followed` is the soldier the camera follows, whose place the listener takes (the
// original's CameraFollowSprite: me alive, whom I watch dead), or -1 for the free
// camera, which listens from the view's centre `camera`. `sparks` are this tick's, for
// the sounds they made (a casing landing, a body burning); NULL for none.
void audio_tick(Audio *a, const Game *g, int me, int followed, Vec2 camera, const Sparks *sparks);

// A sound with no place, from the camera: the menus' clicks and the like.
void audio_flat(Audio *a, const char *name);
