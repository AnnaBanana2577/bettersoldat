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

Two things sit in this directory without being part of that content, and neither is
under CC BY 4.0.

**`hook-gfx/`**, the grappling hook's head and chain, is Teeworlds' art: the
`hook_head` and `hook_chain` sprites, cut unchanged from its `data/game.png`. It is
licensed under **Creative Commons Attribution-ShareAlike 3.0 Unported (CC BY-SA 3.0)**,
<https://creativecommons.org/licenses/by-sa/3.0/>, and stays under it here.

- **Source:** <https://github.com/teeworlds/teeworlds> (`datasrc/game.png`)
- **Authors**, as Teeworlds credits its content: android272, Chi11y (chi1), Crises,
  Daniel, Echchouik, Fisico, leovilok, Landil, Lappi, LordSk, maikka, matricks,
  Pocram, red_com, serpis, SkizZ, somerunce, Sonix, Stephanator, teetow, Ubu, Zatline
- **Modifications:** the two sprites were cut out of the sprite sheet; the game draws
  them scaled.

The hook's rules (shared/game/systems/hook.c) are a port of Teeworlds' code, Copyright
(C) 2007-2024 Magnus Auvinen, under the zlib licence; this is an altered version of it.

The game's own `config.cfg`, under the MIT licence in [../license.md](../license.md),
sits beside this directory rather than in it.

**`play-regular.ttf`** is licensed under the SIL Open Font License, Version 1.1:

> Copyright (c) 2011, Jonas Hecksher, Playtypes, e-types AS
> (lasse@e-types.com), with Reserved Font Name 'Play', 'Playtype',
> 'Playtype Sans'.

The full licence text is in [OFL.txt](OFL.txt). 'Play', 'Playtype' and 'Playtype
Sans' are Reserved Font Names: a modified version of the font may not be
distributed under those names.

[base]: https://github.com/opensoldat/base
