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
`server`, `editor`, `hud`, `shared`, `assets`, `readme`, `docs`, `dev`. Leave it out
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

A tag is the version; what ships beside it is the client, the server and the contents
of `assets/`, unpacked flat so that config.cfg and the art sit beside the executable
(see the readme). The tag alone is not a release until that exists.
