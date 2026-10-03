# Design

How the game is put together: one simulation, run the same on every machine, that
knows nothing of the wire, the screen or the speakers; the passes inside it that talk
only through events; and the three consumers outside it that read those events and
its state. docs/netcode.md is the wire's own account; this is the shape the wire plugs
into.

## The shape

Three programs share one library, each a folder of packages/ (the paths in these docs are
within it):

- **shared/** is the simulation (shared/game), the data it reads (shared/resources:
  maps, animations, skeletons), the wire (shared/network), the console and the
  utilities. No rendering, no audio, no sockets in the simulation; ENet only under
  shared/network.
- **client/** opens a window, reads the keys, ticks the simulation, draws it, plays
  it, and talks to a server through shared/network.
- **server/** is headless: the console, the simulation with authority, and the
  connections. The tests build its connections and rounds in, so a server and clients
  run in one test binary over the loopback.

Everything that happens in the game happens in shared/game, once, in C that the
client and the server compile alike. The client does not have its own idea of a bullet
and the server another: both run `game_tick` on their own `Game`. What differs is
which decisions each is allowed to make (authority, below) and what each puts into
the tick's mailbox before the passes run (the wire, below).

## The simulation's contract

`Game` (game.h) is:

    Game {
      Context   static data a tick reads and never writes: map, anims, weapons, skeletons
      World     the state: soldiers, bullets, things, corpses; stepped by world_step
      Match     the round around it: clock, scores, settings; run by match_run
      events    what the last tick left behind
      last      the tick before it, for the passes that had run before something was emitted
      incoming  what was heard from elsewhere, for the next tick (game_hear)
    }

The rules a tick keeps:

- **No I/O, no globals.** Loading the Context is the only place files are read.
  Nothing in a tick prints, draws, plays or sends. The world never reaches out; it
  writes state and emits events, and whoever cares reads them afterwards.
- **No allocation per tick.** Fixed-capacity arrays: MAX_PLAYERS soldiers, MAX_BULLETS
  bullets, MAX_THINGS things, MAX_EVENTS events. A full buffer drops silently.
- **Deterministic randomness.** `World.rng` for the world's rolls; a soldier's own
  `rng` for what its player rolls (the spread of its shots), rolled on the machine that
  plays it and carried in its served half; a bullet's own numbers for what happens to
  it in flight. So a bullet made from the same shot flies the same on every machine,
  and every machine sees the same ricochet.
- **Health changes in one place.** Nothing wounds a soldier on its own. Bullets and
  blasts emit a `Hit`, a proposal; only `damage_apply` turns it into a wound, and only
  where the world has authority.
- **The world never hears of the match.** What it does that the match must know (a
  kill, a capture) goes out as an event. What the match decides (the round is frozen,
  friendly fire is on, how long a respawn takes) reaches the world as values in
  `World.rules`, copied in from the match at the top of every tick (`match_rules`).

Time is the tick: 60 a second (TICK_RATE), and `World.tick` counts them. There is no
dt anywhere in the simulation; the original's constants are per tick and stay that way.

### Authority

One flag on the world, `authority`, marks the server's (and a tool's, a test's, or the
client's own local sandbox before it joins). Only with authority:

- hits become wounds (`wounds_apply`), so health, death and the tally change;
- the dead respawn, spawn protection counts down, bonuses run out
  (`soldier_served_tick`);
- things are made, taken, returned, scored and respawned; guns are laid down where
  they were dropped; the flag is thrown (the things pass checks `authority` for each);
- the match runs at all: scores, the clock, the end of the round (`match_run` returns
  at once without it).

Everywhere else the same passes run and the same physics moves everything, but the
decisions above arrive as facts: the server's events into the mailbox, and the
server's state in the snapshot. A client's world is a faithful mover and a poor judge,
by design.

## The tick

`game_tick(g, cmds)` takes one `Command` per player slot (buttons and a world-space
aim, numbered) and runs:

1. `last = events`; `events` cleared. Everything in `incoming` (what `game_hear` was
   given since the last tick) is copied to the front of `events`; `incoming` cleared.
   So what was heard from elsewhere is this tick's first events, ahead of anything the
   passes emit, and the passes do it as they would their own.
2. `world.rules = match_rules(match)`.
3. `world_step`, the passes in order (below).
4. `match_run`, with authority only.
5. `history_record`, if the world has a history ring (the server's), filing the
   soldiers and things under the tick just run.

### The passes

`world_step` is the original's UpdateFrame, kept in its order so the port behaves as
the original does, and named as passes (`Pass` in entities.h):

| pass | routine | what it writes |
|---|---|---|
| PASS_SOLDIERS | `soldier_step`, one soldier at a time | the soldier: integrate, knockback, controls through the state machines, animate, collide with the map, weapon timers, jets |
| PASS_CORPSES | `ragdolls_update` | the ragdolls |
| PASS_BULLETS | `bullets_update` | the bullets: the shots asked for become bullets, then every bullet's tick and flight |
| PASS_WOUNDS | `wounds_apply` | the soldiers' health, death, tally; authority only |
| PASS_THINGS | `things_update` | the things: what was asked of them, then every thing's physics, bases, captures, pickups, timeouts |
| PASS_RECEIPTS | `soldiers_receive` | the soldiers take what the things gave: a kit's gift, a gun into the hands |

Then `tick++`.

Each system is one file under shared/game/systems (systems.h lists them with what each
owns). A pass **writes its own entities and reads any**. Some fields of a soldier
belong to another system's pass and are written there alone: what it holds and mans
(`held`, `stat`, `use_time`) and the things' counters on it are the things pass's; its
health, death and tally are the wounds pass's. A pass that wants something of another's
entities does not reach in; it emits an event, and the owner's pass does it when it
runs.

### The events as the passes' mail

`Events` is the tick's list, and every event has one meaning: something happened, or
something is asked for. The same list serves three readers, which is the whole point:
the passes read it to talk to each other, the client's effects read it to show what
happened, and the wire reads it to carry what was decided.

A pass consumes every event since it last ran, once: `events_pending(last, now, pass)`
gives a cursor over the rest of last tick's list from where this pass stood when it
began, then this tick's list up to where the pass begins now; taking the cursor records
where it began (`passed[pass]`). What the pass emits itself is for the next time round.
So:

- what an **earlier** pass asks of a later one happens **this** tick (a soldier's
  EVENT_SHOT in the soldiers pass becomes a bullet in the bullets pass);
- what a **later** pass asks of an earlier one happens **next** tick (a kit's gift
  told by the things pass is taken in the receipts pass this tick, but the stationary
  gun's shot asked in the things pass flies from the next bullets pass), as it does in
  the original's frame, where a thing's bullet is made in the things' update and first
  flies the frame after.

The events test (tests/events_test.c) holds this: each pass sees each event once, in
order, however the ticks fall.

The asks and their doers:

| event | asked by | done by |
|---|---|---|
| EVENT_SHOT | a weapon (combat), a grenade, a punch, the map's lava and exploding polys, the stationary gun | the bullets pass makes the bullet, numbered as the shooter's next |
| EVENT_HIT | a bullet, a blast, a suicide, a poly | the wounds pass applies it; a client only shows it |
| EVENT_WEAPON_DROP | a throw (combat) or a death (wounds) | the things pass lays the gun down |
| EVENT_KNIFE_LAND | a thrown knife stopping (bullets) | the things pass leaves a knife to pick up |
| EVENT_FLAG_THROW | the key (soldiers) | the things pass throws the flag |
| EVENT_THING_KNOCK | a bullet striking a thing (bullets) | the things pass shoves the point |
| EVENT_KILL | the wounds pass | the things pass lets go of what the dead held |
| EVENT_RESPAWN | the match (soldier_served_tick) | the things pass returns what it held and hands a parachute to a high spawn; a client places its own soldier from it |
| EVENT_KIT_PICKUP, EVENT_WEAPON_PICKUP | the things pass | the receipts pass gives the soldier what it took |

Everything else is a consequence, emitted for whoever is listening: EVENT_FIRE (the
muzzle), EVENT_BULLET_SPAWN and EVENT_BULLET_END, the wall hits, ricochets, bounces and
splits, EVENT_BLOOD, EVENT_EXPLOSION, EVENT_DAMAGE, EVENT_FLAG_GRAB, RETURN, DROP and
SCORE, EVENT_THING_HIT, EVENT_POLY_EFFECT, EVENT_CORPSE_HIT, EVENT_MATCH_END,
EVENT_ANTIC (an antic's spit, puff, match, stub or piss, for the sparks and the audio),
and EVENT_ROPE_CUT (a rope's cut, for the sparks).

An event carries what its listeners need and nothing that requires looking the world
up afterwards: a kill says who, whom, with what, where, the killer's tally now and the
shot's flight for the readout; a respawn says the slot, the new life's number, the
team, the loadout and the spot, which is all a client needs to begin the life itself.

## The soldier's halves

A soldier is run in one place at a time and its fields are grouped by who decides
them (entities.h, `soldier_copy_owned` and `soldier_copy_served`):

- **owned**, by the client that plays it: position, velocity, the knock pending,
  controls, aim, direction, stance, the animations, jets, the weapons and their ammo,
  grenades, the stationary gun manned. Its own client's word is final; the server takes
  it as written after bounds checks.
- **served**, by the server: active, team, health, dead, `life`, the death's record
  for the corpse, the tally, what it holds, the cooldowns, spawn protection, the vest
  and bonus, the loadout chosen for the next spawn, the look, and the two HUD relays
  (typing, ping). Also its rng, its last command's number and its shot count, so a
  client that joins mid-game numbers the soldier's next bullet as the server does.
- **the rest** is this machine's: the integrator's old position and forces, the
  memory the control state machines keep between ticks, and `remote`.

`life` counts the server's placings of the soldier. Word from before a placing is never
taken for word from after it, on either side: the server ignores a client state of an
older life, and a client takes the server's owned half of its own soldier only when the
life changes. That one number is what keeps a respawn from being dragged back to where
the corpse stood.

`remote` is set by the client on everyone but itself, and is never on the wire. A
remote soldier is stepped through the same `soldier_step` with `armed` false: the
controls move it, the animations play, the map stops it, but the trigger fires nothing.
Its shots arrive as events. That is how a soldier heard of moves between words without
the machine inventing bullets for it.

## The consumers

Three things read the simulation from outside, and none of them is read back by it.

### Effects: a sink for the events

After each tick the client hands the tick's events to what shows them:

- `render_tick` and `sparks_tick`: the bursts (chips off walls, blood, smoke, the
  explosions, the casings from EVENT_FIRE, the spawn spark from EVENT_RESPAWN), plus
  what it reads off the world's state each tick: the jets' flames where a soldier is
  jetting, a corpse's bleeding by its cut joints and how long it has been dead. The
  sparks have their own rng and never touch the game.
- `audio_tick`: a sound per event (a fire, a blast, a ricochet, a pickup, a capture),
  placed from the listener; and what has no event because it is a state, found by
  holding every soldier's last tick beside this one (the reload begun, the jets held,
  the chainsaw running) and by watching the bullets pass the listener. The
  sparks' own noises (a casing landing, a body burning) are rolled with the spark, in
  `Sparks.sounds`, and played from there.
- `feed_tick`: the kill console, the big messages, the console's lines about flags and
  the match, and my weapon stats, from EVENT_KILL, FIRE, DAMAGE, the flag events and
  MATCH_END.

Alone or online these read the same list. Online the server's decisions are among it,
having arrived by the wire into the mailbox and come out of `game_tick` as events like
any other, so the feed does not know a kill from the server from a kill it saw
happen. Nothing is shown from a packet; everything is shown from the tick.

### The renderer: state only

The renderer never sees an event. After each tick the client captures a
`TickSnapshot` (the soldiers, corpses, bullets and things); each frame
`build_render_state` blends the last two by how far into the next tick the frame
falls, and `render_draw` draws a `RenderState` and nothing else of the game:

    game ticks  ->  TickSnapshot, twice (previous, latest)
    each frame  ->  build_render_state(previous, latest, alpha, offsets)  ->  RenderState  ->  render_draw

Positions blend; everything discrete (animation frames, weapons, health) is the latest
tick's. A soldier whose `life` differs between the two ticks is drawn where it now is,
not slid there. The renderer's own choices about where things appear between ticks
are all in that one routine, so the simulation keeps no picture-side state and the
picture keeps no game-side state. The camera follows per frame, at the frame's dt,
which is why it glides at any frame rate while the game steps at sixty.

`offsets` is the one place the wire touches the picture: what a correction from a
snapshot moved another soldier by is kept by the client stream as an offset the
renderer adds, shrinking to nothing over cl_smooth. The simulation snapped; the
picture glides; the two never disagree about where the soldier is, only about where it
is drawn.

### The wire: a subset of the events, and the halves

The wire is the third reader of the same list and the one writer of the mailbox. It
gets its own section.

## How the netcode plugs in

The simulation offers the wire exactly two hooks, both in game.h: `game_hear(g, e)`
puts an event into the mailbox for the next tick's passes, and `Game.events` is what
the tick left, to be read after it. Nothing in shared/game includes shared/network.

### Which events travel

wire.h classifies every event type, and a test (tests/wire_test.c) holds that none is
missed:

- **WIRE_LOCAL**, never sent: every consequence each machine produces for itself by
  flying the same bullet from the same seed (fire, bullet end, wall hit, ricochet,
  collider hit, bounce, split, blood, explosion, thing hit, poly effect, corpse hit,
  rope cut), and every ask between systems within a machine (Hit, knife land, thing
  knock). Hit is the simulation proposing a wound; it never travels, and only the
  server's wounds pass makes a wound of it, but its shove and bink land on every
  machine that flew the bullet (docs/netcode.md).
- **WIRE_OWNER**, a soldier's owner's decision: EVENT_SHOT, numbered so the same bullet
  comes out everywhere; EVENT_WEAPON_DROP; EVENT_FLAG_THROW. A client sends its own
  to the server, which does them as its own and relays them to everyone else.
- **WIRE_SERVER**, only the server's: damage, kill, respawn, flag grab, return, drop
  and score, kit and weapon pickup, the match's end. Sent to everyone, done by their
  passes.

Each side keeps a numbered queue (`WireQueue`) of what it sends; `wire_collect` fills
it from the tick's events after every tick (a client keeps only its own owner's
decisions, the server everything that travels, remembering from whom so an owner is
never sent its own back). A packet carries what the other side has not acknowledged,
capped; the receiver applies each once by number, into the mailbox, stamped with the
tick it happened. A shot heard is made with `advance` set to the ticks since, and the
bullets pass runs it forward that far on making it, at most half a second, so it sits
where its shooter has it now. Advancing the bullet is the lag compensation. The
bullets also carry a `lag` from the shooter's `view_lag`, and the server's history ring
can judge them against the soldiers that many ticks back (`history_targets`); today
the lag is zero everywhere and hits are judged at the present.

### The two streams

Both unreliable, every tick, delta-compressed against what the other side last
acknowledged (stream.h; docs/netcode.md has the byte-level account):

- **Client state**, client to server: the owned half of my soldier and my loadout
  choice, the life it is of, the acknowledgements, then my decisions from the queue.
- **Snapshot**, server to client: the match; for every slot a word (gone, state, or
  same: held back to fit the datagram, keep stepping it) and the soldier's two halves
  where it is state, a name when it goes whole; the things likewise; then the server's
  decisions from the queue.

What each side takes of the other's word is the decoupling in practice
(`server_stream_receive`, `client_stream_hear`):

- The **server** takes a client's owned half only for a soldier alive here and of the
  same life, after the numbers read, the position is on the map and the state is newer
  than the last. Its events go into the mailbox only if they are that owner's own
  decisions; a client's word about anyone else is dropped. The loadout is taken always,
  put right if it isn't a primary and a secondary.
- The **client** takes the served half of everyone and the owned half of everyone but
  itself; its own owned half only on a new life. Its own look, loadout and typing stay
  its own. The match is copied whole. Things are taken always for what they are and
  whose, but their points only when the client's own disagree by more than a few
  units, and never while held: a carried flag rides its holder's skeleton locally, so
  it never lags the player carrying it. The world's tick is kept to the snapshot's.
  The server's events go into the mailbox, every one.

Between words everyone steps a soldier heard of on its last keys (`stream_command`:
one-shot buttons cleared, the throw held so a wound-up grenade still goes), and after
half a second of silence the keys are let go so a quiet player falls and stops. The
client and the server both do this, through the same routine.

### The loops

**The client's tick** (client/main.c, `tick`): mark everyone but me `remote`; their
commands from `stream_command`, mine from the input; `game_tick`; `render_tick`,
`audio_tick`; `client_net_tick` (collect my decisions from the tick's events, send my
state); capture the tick snapshot; `feed_tick`. Snapshots are applied when the line is
polled, before the ticks owed this frame; the line is flushed after them so nothing
waits a tick in the queue. Frames are drawn from the two snapshots as above. Before it
joins, the client ticks the same world with authority and one soldier: the local
sandbox is the server's code path with nobody on the line.

**The server's tick** (server/host.c, `host_pump`): poll the line (states taken,
decisions into the mailbox, joins, leaves, chat and votes); then for each tick owed:
everyone's command from `stream_command`, the bots' from their minds
(`bots_commands`), `game_tick`, the bots told what the tick did to them (`bots_hear`),
`connections_snapshots` (collect the tick's travelling events into the queue, a
snapshot to every player); a round change if one is due; flush.

**A host** (server/host.c) is the world with authority, the line, the players, the bots
and the rounds in one struct, pumped by whoever owns it. The dedicated server
(server/main.c) is a console and a stdin reader around one. The client's Local Play is
that server: the `host` command starts bin/server beside the game (client/net/
local_server.c) on the install's own files, its console piped into the game's, and joins
it over the loopback once it says it is hosting, as it would any server. So a game
against bots is the netcode's ordinary case with no latency, friends can join the same
game at the player's address, and Local Play hosts exactly as a dedicated server from
that install would: its server.cfg, weapons.ini, maplist.txt, lists and script.

**The bots** (server/bots.c) are the original's AI (opensoldat's AI.pas), ported as it
stands, and they sit where a client sits: a source of commands. Each tick a bot reads
the server's world as a client would (the soldiers it can see from its head, by a ray
through the map; the things; the grenades near it) and writes its keys and aim into
its slot of the commands; the server's tick does with them what it does with a
player's, and the bot's shots travel to the clients as any owner's decisions do. A bot
is a soldier the server plays, marked `bot` in the served half so the roster knows,
not remote, with a `Connection` holding its name and no peer. Nothing in shared/game
knows a bot from a player; the one thing the bots write into the world is a thing's
`interest`, as the original's do.

**A round's end** is the original's MapChangeCounter. The match stops (a limit reached
in `match_run`, or `match_stop` from the host for `nextmap`, a map vote or a script)
and the world freezes: `world_step` runs no passes while `rules.frozen`, so bullets
hang and nobody moves or respawns, and the scores stand for ROUND_END_TICKS. The host
settles the map coming (the one asked for, else the rotation's next) and tells
everyone (MsgMapChange: the map and the count; a newcomer during the count is told
too). A client that hears it puts the scoreboard up, closes the weapons menu, prints
"Next map:", and over the board says who won (`draw_end_game_texts`), as the original's
ClientHandleMapChange and RenderEndGameTexts do. Votes (server/connections.c) run as
the original's: twenty seconds, only yeses, against the players on when the vote began,
passing at sv_votepercent; a kick passed bars the address for an hour. A player heard
from more than net_floodingpackets times in a second is warned, and past sv_warnings_flood
warnings kicked and barred for a quarter of an hour; one who chats faster than a line a
second for long is kicked for five minutes (the original's FloodWarnings and ChatWarnings). The escape
menu's map window pages the server's own list (MsgMapQuery, MsgMapReply), the
rotation or, with none, every map under data/.

**A round** (server/rounds.c): the context reloaded, the world and match made anew
with the history ring cleared, everyone placed, everyone told (MsgMap, reliable, with
the round's number). Both streams are stamped with the round and another round's are
dropped, so packets that cross the change do no harm. A client hears of its first round
the same way it hears of every other: joining and a new round are one path.

## Tests

The simulation is tested as scenes (tests/test.c): a map loaded from runtime/data/, two
soldiers placed, `game_tick` run with authority on scripted buttons, and the events
tallied. Combat, corpses and things have their scenes; the passes' mail has
events_test; the wire has its classification, round-trip and refusal tests; the join
and the streams run a real server and a real client over the loopback in one binary
(join_test, stream_test), measuring bytes per tick and that a bot's shots are made on
the client with its count in step. `xmake test` runs them all, headless.

## Adding to it

A new thing that happens in the game is a new event, and the checklist is short:

1. Add its struct and type to entities.h, and emit it where it happens, with what its
   listeners will need in it.
2. If another system must act on it, consume it in that system's pass through
   `events_pending`; decide which side of a pass boundary it falls on, and whether it
   should be gated on `authority`.
3. Classify it in `wire_side` (the switch has no default, so the compiler warns of a
   case left out) and bump the counts the wire test expects; if it travels, lay it out
   in `wire_event` and add a sample to the test's round trip.
4. Show it: a case in sparks, audio or the feed, or nothing if it has no picture.

Nothing else needs a way in. The sparks never learn of the wire, the wire never learns
of the sparks, and the passes never learn of either.
