#pragma once

// The game's entry in a Linux desktop's menu: soldatreloaded.desktop in
// $XDG_DATA_HOME/applications (~/.local/share/applications), naming the launcher and
// data/icon.png wherever the install was unpacked. A Linux executable holds no icon,
// so this is how a menu, a dock or a taskbar shows the badge. Nothing on Windows, where
// the executables hold it themselves (xmake.lua's icon rule).

#include <stdbool.h>
#include <stddef.h>

// The entry's name, and the class the launcher's and the client's windows are given
// (SDL_VIDEO_X11_WMCLASS, SDL_VIDEO_WAYLAND_WMCLASS), so a desktop pairs them with it.
#define DESKTOP_APP_ID "soldatreloaded"

// The entry's text for the launcher at `launcher`, an absolute path; false if it doesn't
// fit in `size` or the path can't be put in one (a control character).
bool desktop_entry_text(const char *launcher, char *out, size_t size);

// Writes the entry for this launcher, where it isn't already what the entry says: on a
// first start, and when the install has moved. Only from an install, with
// data/icon.png beside it; false if it should have been written and couldn't be.
bool desktop_entry_install(void);
