# SoldatReloaded

A work-in-progress port of [Soldat](https://soldat.pl) to C, with improvements to the
game and the tooling around it built in.

Soldat is a fast 2D multiplayer shooter: side-scrolling, physics-driven, with jets,
ragdolls and a few dozen players a server. Its open-source successor,
[OpenSoldat](https://github.com/opensoldat/opensoldat), is written in Object Pascal.
SoldatReloaded rewrites it in C11, system by system, keeping how the game plays as it
plays there (the physics, the weapons, the movement, the maps and art), and changes
what is around it: the netcode, the menus, how you find a game, how a server is run
and extended, and how the game stays up to date.

It is playable now, online and against bots, but it is not finished: see
[What's not done yet](#whats-not-done-yet).

## What's different from Soldat

- **Netcode built for today's connections.** Every soldier is simulated on every
  machine from the last word about it; your own soldier is never corrected, and a shot
  is judged against what the shooter saw on their screen, rewound on the server, so the
  round trip never robs you of a hit. Delta-compressed snapshots, a view clock that
  hides jitter, and remote shots drawn from the muzzle however high the ping. The design
  is in [docs/netcode.md](docs/netcode.md).
- **A main menu again.** Join from a server browser, host a game with bots (Local
  Play), set up your player, controls, taunts and options, all inside the game, as the
  original Soldat let you; OpenSoldat has no menu of its own and leaves this to a
  separate launcher.
- **A server browser and a lobby.** Servers that want to be found list themselves with
  [the lobby](https://github.com/soldatreloaded/soldatreloaded-lobby); the browser asks
  each one directly for its players and ping.
- **A launcher that keeps the game up to date.** Start the game through it and each new
  release is fetched before you play: only what changed, with your settings left alone.
- **Scriptable servers.** A Lua 5.4 script on the server hears the game's events (joins,
  kills, chat, rounds), drives it back (say, kick, change map, pause), and can talk to
  the web over HTTP with JSON. See [docs/scripting.md](docs/scripting.md).
- **More ways to look.** Five gostek types (male, female, waifu, rat, furry), colour
  pickers for every part, grenades in your own colour, and the sky and scenery as you
  like them.
- **Faithful where it counts.** Weapons, movement, bots, votes, the radio menu and the
  HUD are ported from OpenSoldat's code and kept to how it plays, with headless tests
  holding much of it there.

## Playing

Download the latest release for Windows or Linux from
[Releases](https://github.com/soldatreloaded/soldatreloaded/releases) and unpack it
anywhere. Start **Soldat Reloaded.exe** on Windows, or **soldatreloaded-launcher** on
Linux: it checks for updates, then starts the game.

From the main menu, **Servers** lists the games being played, **Join by Address**
connects to one you know, and **Local Play** hosts a game here, against bots or for
friends on your network. **Taunts** sets what a key says: a message to everyone or to
your team, or your own words as a radio call.

Some keys, all rebindable on the Controls page: A/D to move, W to jump, S to crouch,
X to go prone, the right mouse button for the jets, Space to throw a grenade, Q to
switch weapons, R to reload, Tab for the weapons menu, Escape for the game menu, T to
chat, Y to chat to your team, V for the radio menu, Alt+letter for the taunts, F1 for
the scoreboard.

Settings live in `config.cfg` beside the game, which it reads at start and saves on the
way out.

## Running a server

The release's `-server` package is a headless server: the game, the maps and nothing to
draw. Unpack it and run `server` (`server.exe` on Windows); settings are the `sv_*`
lines in its `config.cfg`, or given on the command line:

```
server +sv_hostname "My server" +sv_maps "ctf_Ash ctf_Kampf" +sv_password secret
```

`sv_public 1` lists it in the game's server browser, once its UDP port (23073 by
default) can be reached from outside. A script in `scripts/server.lua` runs with it;
`scripts/example.lua` shows what a script can do.

## Building

You need [xmake](https://xmake.io) and a C compiler: Visual Studio's on Windows, gcc or
clang on Linux. xmake fetches and builds the libraries itself: SDL2, ENet, Lua, libcurl,
stb and miniz. On Linux, SDL2 builds against the system's X11, Wayland and audio
headers, which have to be installed first (the list is in
[.github/workflows/release.yml](.github/workflows/release.yml)).

```
xmake              # the client, the server and the launcher
xmake run client   # play, from the project directory
xmake run server   # a dedicated server
xmake test         # the headless tests
xmake dist         # the release packages, into build/dist/
```

The game finds `config.cfg` and `assets/` in the directory it runs from, so run it from
the project directory or from an unpacked package.

## How it's put together

| | |
|---|---|
| `shared/` | The simulation (`game/`), the maps, animations and skeletons it reads (`resources/`), the wire (`network/`), the console and the utilities. Built into both the client and the server, so both run the same game. |
| `client/` | The window, input, rendering (OpenGL 2.1), audio, the HUD and menus, and the client's end of the netcode. |
| `server/` | The headless server: connections, rounds, bots, votes, the Lua scripting and the lobby heartbeat. |
| `launcher/` | The updater: fetches releases from GitHub, checks every file against the release's hashes, and starts the game. |
| `tests/` | Headless checks of the simulation on real maps and of the netcode over the loopback. |
| `assets/` | The game's art, maps, sounds and bots, from [opensoldat/base](https://github.com/opensoldat/base). |
| `scripts/` | The server's Lua scripts. |
| `docs/` | How it works and how to work on it. |

The docs go deeper:

- [docs/design.md](docs/design.md): the simulation, its passes and events, and what
  reads them.
- [docs/netcode.md](docs/netcode.md): the netcode, its reasoning and what it measured.
- [docs/scripting.md](docs/scripting.md): the server's Lua API.
- [docs/git.md](docs/git.md): commits, branches and releases.
- [docs/todo.md](docs/todo.md): what's planned.

## What's not done yet

Demos, a map editor, a mod maker and editors for the skeletons and animations (the
`.po` and `.poa` files) are planned but not started, and only deathmatch and capture
the flag of OpenSoldat's game modes are in so far, with some of its server settings
still to be ported. [docs/todo.md](docs/todo.md) keeps the
list. Each release can change the network protocol, so clients and servers must be on
the same version.

## Contributing

Issues and pull requests are welcome. [docs/git.md](docs/git.md) describes how commits
are written and how work lands; a pull request should build, pass `xmake test`, and say
in its description what it changes in play.

## Licence

The code is under the MIT licence: [license.md](license.md). The game's content in
`assets/` is OpenSoldat's, under CC BY 4.0, with a few exceptions such as the menu's
font; [assets/NOTICE.md](assets/NOTICE.md) has the details and the credits.
