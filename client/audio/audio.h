#pragma once

// Sounds, from Sound.pas and the play sites in Sprites.pas, Bullets.pas and
// SpriteEffects.pas by way of soldat-odin's ss_audio.odin, on SDL's audio device:
//
//   - one sample per wav, read on first use into the device's format; a play takes a
//     free voice from a small pool, or the oldest playing
//   - every play is placed from the listener, my soldier: gain volume * (1 - d / 750),
//     cut past that, panned by the direction; a sound with no place comes from the camera
//   - four reserved voices per soldier (reload, jets, gattling, gattling2, the layout
//     of Sprites.pas): a voice already playing is refreshed, not restarted. That is how
//     the loops (jets, chainsaw, flamer) live, by being played every tick, and how a
//     wind-up is cut by stopping its voice
//   - past half the range a shot or blast also plays its distant sample; a blast next
//     to me rings the ears (hum) and fades everything else for a few seconds
//
// What plays when: the events (each tick's), each soldier's state against the tick
// before, bullets passing me, and the clock's beeps, all from audio_tick once per
// tick. Purely a listener: nothing here changes the game.

#include <SDL.h>

#include "game/game.h"

#define AUDIO_RATE 44100
#define AUDIO_VOICES 48
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
    Soldier prev[MAX_PLAYERS];  // everyone as of the tick before
    bool whizzed[MAX_BULLETS];  // bullets that have already whizzed past me
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

// Once per tick, after the game's: where I listen from (my soldier, or the view's
// centre `camera` when there is none), then everything that sounded this tick.
void audio_tick(Audio *a, const Game *g, int me, Vec2 camera);

// A sound with no place, from the camera: the menus' clicks and the like.
void audio_flat(Audio *a, const char *name);
