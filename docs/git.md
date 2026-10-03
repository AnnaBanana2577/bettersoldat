# Git

## Commit messages

	type(scope): what the change is

Lower case after the colon, no full stop, short enough to read down a log. The subject
says what the change *is*, not what was done to the files: `feat(net): interpolate
others, number own bullets, drop guessed knockback`, not `updated netcode files`.

The types:

- `feat` — something the game or the tools can now do.
- `fix` — behaviour that was wrong and now is not.
- `refactor` — the same behaviour, arranged differently.
- `perf` — the same behaviour, faster or smaller on the wire.
- `docs` — the readme, docs/, the comments that carry reasoning.
- `test` — tests, and the tools that run them.
- `build` — build.odin, the flags, the packaging.
- `chore` — everything else: files in, files out, housekeeping.

The scope is the part of the tree the change lands in, named as the tree names it:
`geom`, `polymap`, `anim`, `weapons`, `game`, `net`, `pms`, `cvar`, `timer`, `client`,
`server`, `editor`, `hud`, `shared`, `data`, `mods`, `readme`, `docs`, `dev`. Leave it out
when the change is the whole repo's.

### The body

A subject is enough for a small change. Anything that changes how the game behaves gets
a body, and the body is for the *why*: what was wrong, what rule the new code follows,
and what it measured. The netcode only makes sense as a series of arguments, and those
arguments live in the log. From the history:

> **fix(server): drop duplicate queued commands and advance tick during replay**
>
> A resent command was found in the queue and inserted again anyway, so the server
> applied commands several times over and every prediction was a few ticks off. The
> replay now advances the world tick per command, as the server will, so what reads
> the tick (jet fuel) predicts the same.

Numbers belong there too. A change to the netcode says what it measured, on what line,
so the next person can tell whether they made it better or worse.

Nothing is co-authored to a tool. A commit-msg hook strips those trailers if one
arrives.

## Branches

- `main` is the line of work. It builds and its tests pass.
- Work small enough to land in one go lands on `main`. Work that is not gets a branch
  named after its commit type: `feat/po-editor`, `fix/burst-bullets`.
- A branch merges with its history rather than squashed. The bodies of those commits
  are the reasoning, and squashing throws it away.
- Anything else is deleted once it has been merged or abandoned. A stale branch that
  nobody will say is stale costs more than it stores.

## Releases

Tags are `vMAJOR.MINOR.PATCH`, annotated, on `main`. Nothing is released yet; the first
will be `v0.1.0`.

While the major is 0:

- MINOR for anything a player would notice: a mode, a menu, a weapon, an editor.
- PATCH for fixes and for work nobody can see.

The wire decides the rest. A Hello carries the layout of the state and a build that
does not match is refused, so any release that changes the protocol will not talk to
the one before it. Say so in the tag's message, every time.

A tag is the version; what ships beside it is the client, the server, the launcher and
the contents of `runtime/` (`data/`, `mods/default/`, `config/defaults/` and `scripts/`), unpacked flat so that the config and the
art sit beside the executable: the packages `xmake dist` makes (see xmake.lua). The tag
alone is not a release until those exist.

Players start the launcher (`Soldat Reloaded.exe`, `soldatreloaded-launcher` on
Linux), at the top of the install, which keeps their copy at the newest release
(launcher/update.h) and starts `bin/client.exe`; `bin/server.exe` is the dedicated
server, and the server package's own sits at its top, the one executable there. Each
release carries, for each platform, the game (`soldatreloaded-<version>-<platform>`, what a player downloads)
and a manifest naming every file of an install by its hash; the launcher compares the
install with it and downloads the small update package (`-patch`, the executables and
`config/defaults/`) when only those differ, and the full package when anything in
`data/`, `mods/default/` or `scripts/` does. `config/defaults/` and `mods/default/` are the release's and replaced with it;
`config/client/`, `config/server/` and any other mod in `mods/` are the player's and the server owner's, in no
package, and never touched. So:

- A release that adds a cvar registers it in code with its default (`cvar_register`), and
  puts it in `runtime/config/defaults/` with its comment. A player's own files hold only what
  they set otherwise, so a new or changed default reaches everyone who hasn't.
- A new default bind goes in `runtime/config/defaults/binds.client.cfg` and reaches every player
  who hasn't bound that key otherwise.
- The newest *published* release is the one every launcher moves to, so a release that
  shouldn't go out to players is made a pre-release or left a draft.

Pushing the tag makes them. The release workflow (.github/workflows/release.yml)
builds the packages on Windows and Linux and runs the tests (ci.yml, which runs the same
on every push to main and every pull request), attaches the archives to a GitHub release
named after the tag, with the tag's message as its notes, and announces it on Discord
(discord-notify.yml), each step only if the one before succeeded. So the
version in xmake.lua's `set_version` is bumped in a commit before the tag, the tag's
message is written for players to read, and a tag whose tests fail releases nothing.
