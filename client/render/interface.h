#pragma once

// The HUD: what the original's InterfaceGraphics.pas draws over the world while
// playing, with its default interface (LoadDefaultInterfaceData). The interface is laid
// out for a 640x480 screen and drawn in a view 480 units tall whose width follows the
// window (r_scaleinterface): x anchors stretch by GameWidth/640, offsets from them
// don't, and every image is its pixel size over its mod.ini scale.
//
// Here for now: the health, vest, ammo, reload, fire and jet bars with their icons,
// the grenades, the weapon's name and its bullets, the crosshair, and the FPS line
// (the original's ConInfoShow, F5). The kill console, the chat, the console, the big
// messages, the team boxes, the score and the menus come with what feeds them.

#include "game/game.h"
#include "gfx/gfx.h"
#include "render/camera.h"
#include "render/render_state.h"
#include "render/scale_data.h"

// An interface image with its size in game units.
typedef struct HudSprite {
    GfxTexture tex;
    float width, height;
} HudSprite;

typedef struct Interface {
    HudSprite health, ammo, jet;          // the icons
    HudSprite health_bar, jet_bar, reload_bar, vest_bar;
    HudSprite fire_bar, fire_bar_r;       // the fire bar and its frame
    HudSprite nade, cluster_nade;
    HudSprite dot;                        // the ping dot, once there is a ping
    HudSprite cursor;                     // the crosshair
    bool show_info;                       // the FPS (and ping) line
} Interface;

// The images from <base>/interface-gfx, keyed on pure green as the original's are.
// The context must be up.
void interface_load(Interface *hud, const char *base, const ScaleData *scales);
void interface_unload(Interface *hud);

// The HUD for `me`, over the world: sets its own transform. `cursor` is the game's
// cursor in the 480-tall view's units, `viewport` the window's pixels.
void interface_draw(const Interface *hud, const RenderSoldier *me, const Context *ctx, Vec2 cursor, int fps,
                    Rect viewport);
