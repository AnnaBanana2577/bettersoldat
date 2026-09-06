# Asset attribution

The game content here — the art, maps, animations, sounds and bot personalities —
comes from [opensoldat/base][base], the OpenSoldat base game content, and is
licensed under **Creative Commons Attribution 4.0 International (CC BY 4.0)**.
The full licence text is in [LICENSE.txt](LICENSE.txt).

- **Source:** <https://github.com/opensoldat/base>
- **Licence:** CC BY 4.0, <https://creativecommons.org/licenses/by/4.0/>
- **Modifications:** the files were reorganised into this flat `assets/` layout.
  Upstream keeps them under `shared/`, `client/` and `server/configs/`.

Credits named in the upstream `Credits.md`:

- **AEfremov** — kit textures (berserker, flamer, predator, medkit)
- **pewpew** ([@BranDougherty](https://github.com/BranDougherty)) — interface
  icons (friend, microphone, connection)

## What here is not from base

Two files sit in this directory without being part of that content, and neither
is under CC BY 4.0:

**`config.cfg`** is this project's own settings file, under the MIT licence in
[../license.md](../license.md). It ships here so that it is unpacked beside the
executable, which is where the game reads it at startup.

**`play-regular.ttf`** is licensed under the SIL Open Font License, Version 1.1:

> Copyright (c) 2011, Jonas Hecksher, Playtypes, e-types AS
> (lasse@e-types.com), with Reserved Font Name 'Play', 'Playtype',
> 'Playtype Sans'.

The full licence text is in [OFL.txt](OFL.txt). 'Play', 'Playtype' and 'Playtype
Sans' are Reserved Font Names: a modified version of the font may not be
distributed under those names.

[base]: https://github.com/opensoldat/base
