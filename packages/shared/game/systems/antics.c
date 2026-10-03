// The idle antics and the taunts: the IDLE block of Control.pas, ControlSprite. A
// soldier standing still long enough chews tobacco and spits, lights a cigar, wipes
// its brow or scratches; and by chat a player asks for those and the rest (/tabac,
// /smoke, /takeoff, /victory, /piss, /mercy, /pwn; ServerCommands.pas). The server
// picks the idle antic and relays every ask as a numbered Soldier.antic in the served
// half. The machine itself runs on the soldier's own animations wherever the soldier
// is stepped, as the original's does on every sprite: the owner's run is the one that
// counts (its animations are its word), and the others' keep the sparks and the sounds
// in step. The sounds are the audio's, read off the animations; the sparks go out as
// EVENT_ANTIC.

#include "game/systems/systems.h"

static void effect(Events *events, uint8_t index, AnticKind kind, Vec2 pos, Vec2 vel, int odds, int life)
{
    event_emit(events, (Event){
        .type = EVENT_ANTIC,
        .antic = {.player = index, .kind = kind, .pos = pos, .vel = vel, .odds = (uint8_t)odds, .life = (uint8_t)life},
    });
}

// The weapons the mercy is done with the muzzle under the chin (Mercy2) rather than at
// the temple (Mercy).
static bool long_barrelled(WeaponId id)
{
    return id == WEAPON_M79 || id == WEAPON_M249 || id == WEAPON_SPAS || id == WEAPON_LAW || id == WEAPON_CHAINSAW ||
           id == WEAPON_BARRETT || id == WEAPON_MINIGUN;
}

void antics_apply(const Context *ctx, World *w, uint8_t index, Events *events, bool armed)
{
    Soldier *s = &w->soldiers[index];
    const Anims *anims = ctx->anims;
    Anim *body = &s->body, *legs = &s->legs;
    Idle *idle = &s->idle;
    float dir = (float)s->direction;

    // An ask of the server's, taken once: what the original does to the sprite on the
    // server (IdleRandom := n; IdleTime := 1), done here where the soldier is played.
    if (idle->seen != s->antic_seq) {
        idle->seen = s->antic_seq;
        idle->random = s->antic;
        idle->time = 1;
    }

    // IDLE: the clock runs down while standing still, and on through an antic's longer
    // wait; as it runs out the server rolls one of the four idle antics.
    if (s->stat == 0) {
        bool still = body->id == ANIM_STAND && legs->id == ANIM_STAND && !s->dead && idle->time > 0;
        if (still || idle->time > DEFAULT_IDLE_TIME) idle->time--;
        else idle->time = DEFAULT_IDLE_TIME;
        if (w->authority && idle->time == 1 && idle->random < 0) {
            idle->time = 0;
            idle->random = (int8_t)rand_int(&w->rng, 4);
            s->antic = idle->random; // and to its owner, if that is elsewhere
            s->antic_seq++;
            idle->seen = s->antic_seq;
        }
    }

    switch (idle->random) {
    case 0: // STUFF: a chew of tobacco, and the spit when the clock next runs out
        if (idle->time == 0) {
            anim_apply(anims, body, ANIM_SMOKE, 1);
            idle->time = DEFAULT_IDLE_TIME;
        }
        if (body->id == ANIM_SMOKE && body->frame == 17) body->frame++; // (the "stuff" is heard as 17 is stepped over)
        if (!s->dead && idle->time == 1 && body->id != ANIM_SMOKE && legs->id == ANIM_STAND) {
            Pose pose = soldier_pose(anims, s, s->pos);
            effect(events, index, ANTIC_SPIT, pose.p[11], vec2_scale(hands_aim_direction(&pose), 2.0f), 1, 245);
            idle->time = DEFAULT_IDLE_TIME;
            idle->random = -1;
        }
        break;

    case 1: // CIGAR: out of the pocket (step 1, 2), lit with a match (3 to 5), smoked (6, 7), thrown away (8)
        if (s->dead) break;
        if (idle->time == 0) {
            if (s->has_cigar == 0) {
                if (body->id == ANIM_STAND) { // step 1 of 8
                    anim_apply(anims, body, ANIM_CIGAR, 1);
                    idle->time = DEFAULT_IDLE_TIME;
                }
            } else if (s->has_cigar == 5) {
                if (body->id != ANIM_SMOKE && body->id != ANIM_CIGAR) { // interrupted between 2 and 5: from the top
                    s->has_cigar = 0;
                    anim_apply(anims, body, ANIM_CIGAR, 1);
                    idle->time = DEFAULT_IDLE_TIME;
                }
            } else if (s->has_cigar == 10) {
                if (body->id != ANIM_SMOKE) { // step 6
                    anim_apply(anims, body, ANIM_SMOKE, 1);
                    idle->time = DEFAULT_IDLE_TIME;
                }
            }
        }
        if (body->id == ANIM_CIGAR && body->frame == 37 && s->has_cigar == 5) { // step 3: the hand goes back for the match
            anim_apply(anims, body, ANIM_STAND, 1);
            anim_apply(anims, body, ANIM_CIGAR, 1);
        }
        if (body->id == ANIM_CIGAR && body->frame == 9 && s->has_cigar == 5) body->frame++; // step 4: the match (heard)
        if (body->id == ANIM_CIGAR && body->frame == 26) {
            Pose pose = soldier_pose(anims, s, s->pos);
            if (s->has_cigar == 5) { // step 5: lit
                s->has_cigar = 10;
                effect(events, index, ANTIC_CIGAR_PUFF, vec2_add(pose.p[11], vec2(dir * 4, 0)), vec2(0, -0.7f), 1, 65);
                effect(events, index, ANTIC_MATCH, pose.p[14], vec2(dir / 2, 0.15f), 1, 245);
                body->frame++;
                idle->time = LONGER_IDLE_TIME;
            } else if (s->has_cigar == 0) { // step 2: in the mouth
                s->has_cigar = 5;
                body->frame++;
            }
        }
        if (body->id == ANIM_SMOKE && (body->frame == 17 || body->frame == 37)) { // step 7: a puff
            Pose pose = soldier_pose(anims, s, s->pos);
            effect(events, index, ANTIC_CIGAR_PUFF, vec2_add(pose.p[11], vec2(dir * 4, 0)), vec2(0, -0.7f), 1, 65);
            body->frame++;
        }
        if (body->id == ANIM_SMOKE && body->frame == 38) { // step 8: the stub flicked away
            Pose pose = soldier_pose(anims, s, s->pos);
            s->has_cigar = 0;
            effect(events, index, ANTIC_CIGAR_THROW, pose.p[14], vec2(dir / 1.5f, 0.1f), 1, 245);
            body->frame++;
            idle->time = DEFAULT_IDLE_TIME;
            idle->random = -1;
        }
        break;

    case 2: // WIPE
        if (idle->time == 0) {
            anim_apply(anims, body, ANIM_WIPE, 1);
            idle->time = DEFAULT_IDLE_TIME;
            idle->random = -1;
        }
        break;

    case 3: // EGGS
        if (idle->time == 0) {
            anim_apply(anims, body, ANIM_GROIN, 1);
            idle->time = DEFAULT_IDLE_TIME;
            idle->random = -1;
        }
        break;

    case 4: // TAKE OFF HELMET: off from the first frame, back on from the tenth
        if (s->weapon.id == WEAPON_BOW || s->weapon.id == WEAPON_BOW2) break;
        if (idle->time == 0) {
            if (s->wear_helmet == 1) anim_apply(anims, body, ANIM_TAKE_OFF, 1);
            if (s->wear_helmet == 2) anim_apply(anims, body, ANIM_TAKE_OFF, 10);
            idle->time = DEFAULT_IDLE_TIME;
        }
        if (s->wear_helmet == 1) {
            if (body->id == ANIM_TAKE_OFF && body->frame == 15) {
                s->wear_helmet = 2;
                body->frame++;
            }
        } else if (s->wear_helmet == 2) {
            if (body->id == ANIM_TAKE_OFF && body->frame == 22) {
                anim_apply(anims, body, ANIM_STAND, 1);
                idle->random = -1;
            }
            if (body->id == ANIM_TAKE_OFF && body->frame == 15) {
                s->wear_helmet = 1;
                body->frame++;
            }
        }
        break;

    case 5: // VICTORY (the roar is heard as it begins)
        if (idle->time == 0) {
            anim_apply(anims, body, ANIM_VICTORY, 1);
            idle->time = DEFAULT_IDLE_TIME;
            idle->random = -1;
        }
        break;

    case 6: // PISS: the stream through the animation, in three strengths, as sparks
        if (idle->time == 0) {
            anim_apply(anims, body, ANIM_PISS, 1);
            idle->time = DEFAULT_IDLE_TIME;
        }
        if (body->id == ANIM_PISS) {
            int32_t f = body->frame;
            float speed = 0;
            int odds = 0, life = 0;
            if (f > 8 && f < 22) speed = 1.3f, odds = 2, life = 165;
            else if (f > 21 && f < 34) speed = 1.9f, odds = 3, life = 120;
            else if (f > 33 && f < 35) speed = 1.3f, odds = 4, life = 120;
            if (odds) {
                Pose pose = soldier_pose(anims, s, s->pos);
                Vec2 stream = vec2_scale(vec2_normalize(vec2_sub(pose.p[19], s->aim)), -speed);
                effect(events, index, ANTIC_PISS, pose.p[19], stream, odds, life);
            }
            if (f == 37) idle->random = -1;
        }
        break;

    case 7: // SELFKILL: armed by the first ask, done on the second (CanMercy)
        if (idle->time == 0) {
            if (s->can_mercy) {
                if (long_barrelled(s->weapon.id)) {
                    anim_apply(anims, body, ANIM_MERCY2, 1);
                    anim_apply(anims, legs, ANIM_MERCY2, 1);
                } else if (s->weapon.id != WEAPON_MINIGUN) {
                    anim_apply(anims, body, ANIM_MERCY, 1);
                    anim_apply(anims, legs, ANIM_MERCY, 1);
                }
                idle->time = DEFAULT_IDLE_TIME;
                s->can_mercy = false;
            } else {
                idle->random = -1;
                s->can_mercy = true;
            }
        }
        if ((body->id == ANIM_MERCY || body->id == ANIM_MERCY2) && body->frame == 20) {
            body->frame++;
            idle->random = -1;
        }
        break;

    case 8: // PWN!
        if (idle->time == 0) {
            anim_apply(anims, body, ANIM_OWN, 1);
            anim_apply(anims, legs, ANIM_OWN, 1);
            idle->time = DEFAULT_IDLE_TIME;
            idle->random = -1;
        }
        break;

    default: break;
    }

    // The mercy's shot and the death it asks for, at the animation's 20th frame (its
    // owner fires; the original's client then sends /kill, and the server wounds it for
    // 150, torn apart when bare-handed). Read off the animation rather than the antic,
    // and once per run of it, so the server, which has the owner's animation but not
    // its antic's course, gives the wound whichever frame it first sees past the 20th.
    bool mercy = body->id == ANIM_MERCY || body->id == ANIM_MERCY2;
    if (mercy && body->frame >= 20 && !s->mercy_shot) {
        s->mercy_shot = true;
        if (armed) combat_fire(ctx, w, index, events);
        if (w->authority) s->vest = 0.0f;
        float amount = s->weapon.id == WEAPON_NONE ? 3423.0f : 150.0f;
        event_emit(events, (Event){.type = EVENT_HIT, .hit = {.shooter = index, .target = index, .weapon = s->weapon.id, .amount = amount, .pos = s->pos}});
    }
    if (!mercy) s->mercy_shot = false;
}
