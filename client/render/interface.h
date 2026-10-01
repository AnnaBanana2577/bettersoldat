#pragma once

// The HUD: what the original's InterfaceGraphics.pas draws over the world while
// playing, with its default interface (LoadDefaultInterfaceData). The interface is laid
// out for a 640x480 screen and drawn in a view 480 units tall whose width follows the
// window (r_scaleinterface): x anchors stretch by GameWidth/640, offsets from them
// don't, and every image is its pixel size over its mod.ini scale.
//
// Everything the HUD shows that isn't in the frame's render state comes through a
// HudData (ui/hud_data.h); the menus through a GameMenus (ui/menus.h). The drawing
// never reads the game.

#include "game/game.h"
#include "gfx/gfx.h"
#include "render/camera.h"
#include "render/render_state.h"
#include "render/map_view.h"
#include "render/scale_data.h"
#include "ui/hud_data.h"
#include "ui/menus.h"


// An interface image with its size in game units.
typedef struct HudSprite {
    GfxTexture tex;
    float width, height;
} HudSprite;

typedef struct Interface {
    HudSprite health, ammo, jet; // the icons
    HudSprite health_bar, jet_bar, reload_bar, vest_bar;
    HudSprite fire_bar, fire_bar_r; // the fire bar and its frame
    HudSprite nade, cluster_nade;
    HudSprite dot;      // the ping dot
    HudSprite cursor;   // the crosshair
    HudSprite back;     // the translucent box behind the team box and the frags menu
    HudSprite noflag;   // a team's flag away from its base
    HudSprite arrow;    // the indicator above my head
    HudSprite scroll;   // the frags menu's scroll hint
    HudSprite menucursor; // the pointer in the menus
    HudSprite smalldot;   // the minimap's dots
    HudSprite overlay;    // the bonus tint over the screen
    HudSprite sight;      // the sniper line
    HudSprite deaddot, flag, bot, connection; // the scoreboard's rows: dead, carrying, a bot, the line's quality
    HudSprite guns[WEAPON_COUNT]; // the kill console's icons, by weapon
} Interface;

// The images from <base>/interface-gfx, keyed on pure green as the original's are.
// The context must be up.
void interface_load(Interface *hud, const char *base, const ScaleData *scales);
void interface_unload(Interface *hud);

// For the main menu, drawn in the HUD's units under its own transform: the translucent
// box the menus use, and the pointer.
void interface_draw_box(const Interface *hud, float x, float y, float w, float h, Rgba color);
void interface_draw_pointer(const Interface *hud, Vec2 at);

// The HUD over the world: sets its own transform. `cursor` is the game's cursor in the
// 480-tall view's units, `viewport` the window's pixels.
void interface_draw(const Interface *hud, const HudData *data, const GameMenus *menus, const RenderState *state,
                    const Context *ctx, const MapView *map_view, const GameCamera *camera, Vec2 cursor, Rect viewport);
