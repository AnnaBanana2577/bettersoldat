#include "resources/animation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ANIM_SCALE 3.0f

const AnimInfo ANIM_INFO[ANIM_COUNT] = {
    [ANIM_STAND] = {"stoi.poa", 3, true},
    [ANIM_RUN] = {"biega.poa", 1, true},
    [ANIM_RUN_BACK] = {"biegatyl.poa", 1, true},
    [ANIM_JUMP] = {"skok.poa", 1, false},
    [ANIM_JUMP_SIDE] = {"skokwbok.poa", 1, false},
    [ANIM_FALL] = {"spada.poa", 1, false},
    [ANIM_CROUCH] = {"kuca.poa", 1, false},
    [ANIM_CROUCH_RUN] = {"kucaidzie.poa", 2, true},
    [ANIM_RELOAD] = {"laduje.poa", 2, false},
    [ANIM_THROW] = {"rzuca.poa", 1, false},
    [ANIM_RECOIL] = {"odrzut.poa", 1, false},
    [ANIM_SMALL_RECOIL] = {"odrzut2.poa", 1, false},
    [ANIM_SHOTGUN] = {"shotgun.poa", 1, false},
    [ANIM_CLIP_OUT] = {"clipout.poa", 3, false},
    [ANIM_CLIP_IN] = {"clipin.poa", 3, false},
    [ANIM_SLIDE_BACK] = {"slideback.poa", 2, true},
    [ANIM_CHANGE] = {"change.poa", 1, false},
    [ANIM_THROW_WEAPON] = {"wyrzuca.poa", 1, false},
    [ANIM_WEAPON_NONE] = {"bezbroni.poa", 3, false},
    [ANIM_PUNCH] = {"bije.poa", 1, false},
    [ANIM_RELOAD_BOW] = {"strzala.poa", 1, false},
    [ANIM_BARRET] = {"barret.poa", 9, false},
    [ANIM_ROLL] = {"skokdolobrot.poa", 1, false},
    [ANIM_ROLL_BACK] = {"skokdolobrottyl.poa", 1, false},
    [ANIM_CROUCH_RUN_BACK] = {"kucaidzietyl.poa", 2, true},
    [ANIM_CIGAR] = {"cigar.poa", 3, false},
    [ANIM_MATCH] = {"match.poa", 3, false},
    [ANIM_SMOKE] = {"smoke.poa", 4, false},
    [ANIM_WIPE] = {"wipe.poa", 4, false},
    [ANIM_GROIN] = {"krocze.poa", 2, false},
    [ANIM_PISS] = {"szcza.poa", 8, false},
    [ANIM_MERCY] = {"samo.poa", 3, false},
    [ANIM_MERCY2] = {"samo2.poa", 3, false},
    [ANIM_TAKE_OFF] = {"takeoff.poa", 2, false},
    [ANIM_PRONE] = {"lezy.poa", 1, false},
    [ANIM_VICTORY] = {"cieszy.poa", 3, false},
    [ANIM_AIM] = {"celuje.poa", 2, false},
    [ANIM_HANDS_UP_AIM] = {"gora.poa", 2, false},
    [ANIM_PRONE_MOVE] = {"lezyidzie.poa", 2, true},
    [ANIM_GET_UP] = {"wstaje.poa", 1, false},
    [ANIM_AIM_RECOIL] = {"celujeodrzut.poa", 1, false},
    [ANIM_HANDS_UP_RECOIL] = {"goraodrzut.poa", 1, false},
    [ANIM_MELEE] = {"kolba.poa", 1, false},
    [ANIM_OWN] = {"rucha.poa", 3, false},
};

// Parsed through double and then narrowed, as the Odin port's strconv does, so the
// keyframes round identically.
static float parse_float(const char *s)
{
    return s ? (float)strtod(s, NULL) : 0.0f;
}

void anim_parse(AnimData *anim, AnimInfo info, char *text)
{
    memset(anim, 0, sizeof(*anim));
    anim->num_frames = 1;
    anim->speed = info.speed;
    anim->loop = info.loop;

    char *cursor = text;
    for (;;) {
        char *tag = text_next_line(&cursor);
        if (!tag || strcmp(tag, "ENDFILE") == 0) break;
        if (strcmp(tag, "NEXTFRAME") == 0) {
            if (anim->num_frames == MAX_ANIM_FRAMES) break;
            anim->num_frames++;
            continue;
        }
        char *xs = text_next_line(&cursor);
        text_next_line(&cursor); // y: depth, unused in 2D
        char *zs = text_next_line(&cursor);

        long point = strtol(tag, NULL, 10);
        float x = parse_float(xs);
        float z = parse_float(zs);
        if (point >= 1 && point <= MAX_ANIM_POINTS) {
            anim->frames[anim->num_frames - 1][point - 1] = (Vec2){-ANIM_SCALE * x / 1.1f, -ANIM_SCALE * z};
        }
    }
}

Anims *anims_load(const char *base_dir)
{
    Anims *anims = malloc(sizeof(Anims));
    if (!anims) return NULL;

    for (int id = 0; id < ANIM_COUNT; id++) {
        char path[512];
        path_join(path, sizeof(path), base_dir, "anims", ANIM_INFO[id].file);

        char *text = (char *)file_read_all(path, NULL);
        if (!text) {
            fprintf(stderr, "failed to read %s\n", path);
            free(anims);
            return NULL;
        }
        anim_parse(&anims->data[id], ANIM_INFO[id], text);
        free(text);
    }
    return anims;
}

const Vec2 *anim_frame(const Anims *anims, Anim a)
{
    int32_t f = a.frame < 1 ? 1 : a.frame > MAX_ANIM_FRAMES ? MAX_ANIM_FRAMES : a.frame;
    return anims->data[a.id].frames[f - 1];
}

void anim_advance(const Anims *anims, Anim *a)
{
    a->count++;
    if (a->count == a->speed) {
        a->count = 0;
        a->frame++;
        const AnimData *data = &anims->data[a->id];
        if (a->frame > data->num_frames) a->frame = data->loop ? 1 : data->num_frames;
    }
}

void anim_set(const Anims *anims, Anim *a, AnimId id, int32_t frame)
{
    *a = (Anim){.id = id, .frame = frame, .speed = anims->data[id].speed};
}

void anim_apply(const Anims *anims, Anim *a, AnimId id, int32_t frame)
{
    if (a->id != id) anim_set(anims, a, id, frame);
}
