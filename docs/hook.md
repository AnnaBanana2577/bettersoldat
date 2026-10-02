# The grappling hook

The gear that replaces the jets, played with the same key: Teeworlds' hook, its rules
in their order (`CCharacterCore::Tick`, src/game/gamecore.cpp) and its art, brought to
this game's scale. It is experimental, and off unless the server allows it: `sv_hook 1`
on a dedicated server, or Grappling hook On on the main menu's Local Play page. Off,
everyone has jets and the weapons menu shows no Boots row; on, a player picks the hook
there (Boots: Hook, or `cl_player_gear 1`).

## How it plays

Hold the jets' key and the hook leaves the hand at the aim, flying straight at its
pace. The first poly its owner would stand on holds it. If it gets as far as its length
first, it gives up where it last flew to, hangs there three ticks and is put away; the
key must then be let go before it fires again.

Holding a poly, it pulls its owner toward its head every tick until they are near it:

- three times harder up than down (a downward pull is three tenths of an upward one),
  so a wall is climbed more easily than a drop is taken;
- a little harder sideways the way the owner is pressing (nineteen twentieths of its
  pull against three quarters), so they steer the swing;
- adding speed only up to its pace: a pull that would take them past it is not given,
  unless it slows them.

Let the key go and it is put away at once. It takes nothing else away: the soldier
walks, jumps, crouches and shoots on it, and the jets' fuel is left alone.

Teeworlds' hook also catches players and drags them; this one holds polys alone.

## The numbers

Teeworlds runs at 50 ticks a second with a gravity of 0.5 and a view about 1150 wide;
this game at 60 with a gravity of 0.06 and a view 640 wide. Taken as they are its
numbers would make a hook several times too strong and too long here, so they are
brought over the way the rest of the world would be:

- its lengths by the views' ratio, 0.55, so the hook reaches the same part of the
  screen it does in Teeworlds;
- its pull and pace so that they stand to this game's gravity, at its tick, as
  Teeworlds' stand to its own: the pull six times the gravity, as there, and the pace
  about one and a half times a run, as there.

| | Teeworlds | here | setting |
|---|---|---|---|
| length | 380 | 209 | `sv_hook_length` |
| fire speed, a tick | 80 | 20.6 | `sv_hook_fire_speed` |
| pull, a tick | 3 | 0.36 | `sv_hook_drag_accel` |
| pace | 15 | 3.9 | `sv_hook_drag_speed` |
| leaves the hand at | 42 | 23.1 | |
| stops pulling within | 46 | 25.3 | |

The four settings are the dedicated server's, told to every client with the map, as
every machine plays its own soldier's hook and all must play it alike; a change takes
the next round. A game hosted from the menu plays the numbers above.

## The art

The head and a link of the chain are Teeworlds' own (`data/game.png`, the
`hook_head` and `hook_chain` sprites), in `assets/hook-gfx`. They are drawn as
Teeworlds' `RenderHook` draws them (src/game/client/components/players.cpp): the
head, 24 by 16, at the hook, turned the way it flew, and a 16 by 16 link every 16 back
to the hand, each at the same 0.55. Behind the soldier, whose sprites cover the hand.

Teeworlds' art is licensed CC BY-SA 3.0, and its code under the zlib licence, which
this port's rules follow; both are credited in assets/NOTICE.md.

## On the wire

The hook's state rides the soldier's owned half (`hook`, `hook_pos`, `hook_dir`), as
its owner's client plays it and every machine draws it; the server keeps none from a
client in a game without the hook. The Map says whether the game has it and its four
numbers. Wire version 17.
