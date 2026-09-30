# Netcode

The plan, as agreed before it was built. It becomes "as built" as the pieces land, and
each piece's commit says what it measured.

## The model, in one sentence

Every soldier is simulated everywhere from the last word about it. The owner's word is
the truth for where it is and what it fires; the server's word is the truth for what
happens to it; nobody waits for anybody.

This is the original Soldat's model, kept because it is why Soldat feels the way it
does: a player's own soldier is never corrected, and everyone else is where the game
last heard they were, moving as they were moving. What is not kept is the original's
wire: fifty-two message types, snapshots on three periods, deltas beside them, and a
heartbeat carrying the scoreboard. Here there are two streams and a handful of
reliable messages, and the vocabulary of everything that happens is the simulation's
own event list.

## The simulation's contract

shared/game is Context, World and Commands in, World and Events out (game.h). Inside
it, the systems run as passes in a fixed order and talk only through events: a pass
consumes every event since it last ran and writes its own entities alone (see
systems.h). So a decision made elsewhere needs no way in of its own: the wire puts what
it heard into the tick's events before the passes run, and the owner's pass does it as
it would its own. A shot from another machine is a shot like any other. Nothing in the
simulation knows about sparks, sounds, jet flames or packets. Three consumers sit
outside it:

- **Effects** (client/) reads the tick's events and the state: blood, sparks, wall
  dust, the muzzle flash from a fire event, the jet flame from `jetting`. A sink.
- **The renderer** reads state only.
- **The wire** (shared/network) carries the subset of events that are decisions, and
  puts the far side's into the tick's events.

Events split by who may decide them, and a table in shared/network says which is which;
a test holds that every event type is classified.

- **Local consequences, never sent:** fire, bullet end, wall hit, ricochet, collider
  hit, grenade bounce, cluster split, blood, explosion, and Hit itself. Every machine
  flies the same bullet from the same seed and produces these for itself. Hit is the
  simulation proposing a wound; only the server turns it into damage.
- **The owner's decisions, in its client state:** the shot (EVENT_SHOT, numbered so the
  same bullet comes out everywhere), the weapon throw (EVENT_WEAPON_DROP) and the flag
  throw (EVENT_FLAG_THROW).
- **The server's decisions, in its snapshot:** damage, kill, respawn, flag grab, return
  and score, kit and weapon pickup, the match's end, a new round.
- **Neither, and never sent:** what one system asks of another within a machine, such
  as a bullet's knock on a flag (EVENT_THING_KNOCK) or a landed knife (EVENT_KNIFE_LAND);
  every machine produces these for itself.

## The two streams

Both unreliable, both every tick, both delta-compressed against the newest packet the
other side acknowledged; the acknowledgement rides in the packet going the other way.
A baseline too old to keep, or a side that has acknowledged nothing yet, gets the
whole thing.

**Client state**, client to server. The owned half of my soldier (`soldier_copy_owned`:
position, velocity, controls, aim, stance, animation, weapons and ammo, grenades), the
choices that are mine (team, primary, secondary, camera while dead), my wire events
since the server's last acknowledgement, and the number of the newest snapshot I
have. The server bounds-checks the numbers and takes it as written, as the original
takes the movement snapshot.

**Snapshot**, server to client. The server tick. For each soldier: the owned half as
last heard, the served half (`soldier_copy_served`: health, life, how it died, the
tally, the thing it holds, its choices, its look). For each thing: active, style,
holder, the particles, the timeout. The match: scores, time, state. The roster: names
and looks. The map's name and a round number; a client loads the map when the number
changes. The server's wire events since the client's last acknowledgement.

Events are not delta-compressed; they are new by nature. Each side numbers the events
it sends, the receiver keeps the last number applied per sender and applies each once,
so a lost packet is covered by the next and nothing needs a reliable channel. After
heavy loss the backlog is capped, oldest held back, kills and pickups first.

**Reliable, rarely:** Hello, Welcome, Denied, and Chat, which carries commands and
votes as text as well. Nothing else.

## Time

One clock, the server's tick, carried on every snapshot; a client adopts it. Now is
now: a client runs its own soldier at the present with no delay and no correction,
and shows everyone else where the game last heard they were, stepped forward with
their last controls through the ordinary `soldier_step` with `armed` false, so a
remote soldier moves but never fires from its keys. Its bullets come as events. New
word about a soldier blends in over a few ticks rather than snapping. No word for half
a second releases the keys, so a quiet player falls and stops.

A shot is an event from the owner. The server applies it at the shooter's tick and
runs the bullet forward to its own present; every other client runs it forward by
the shooter's ping plus its own, as the original does. Hits are judged on the server
where its bullets meet its soldiers, all at the present. Advancing the bullet is the
lag compensation; nothing is rewound. The shooter plays the flash, the sound and the
blood at once, and the health on the server's damage event.

## Things

Flags, kits and dropped guns run the same physics everywhere. A thing's state in the
snapshot is applied only when it is more than a few units from where the client has
it, and never while it is held: a carried flag rides its holder's skeleton point
locally, so it never lags the player carrying it. Pickups are the server's: the event
arrives one round trip later with the thing's state behind it. A client-side guess at
a pickup can be added later without touching the wire.

## What the server checks

Numbers in range. A life that is still being lived (`life`: word from before a
placing is never taken for word from after it). A shot the weapon in hand could make,
with the ammo it has, from within a few units of where the shooter has stood over the
last quarter second: the history ring (history.c) keeps where everyone claimed to be,
for this, not for rewinding. Duplicate events by number. A movement claim that fails
is dropped and the server's version of the soldier goes out in the snapshot as usual;
there is no correction message, because the owned half in the snapshot is that.

## The wire's shape

shared/network holds: a bounds-checked reader and writer that refuse floats that are
not numbers, enums out of range, counts too large and bytes left over; netfield tables
in Quake 3's style, one per wire struct (the soldier's halves, the thing, the match, a
roster entry, each wire event), one entry per field with its offset, kind and width,
driving one routine that writes a struct whole or as a delta against a baseline and
reads it back; and the message table, reliability marked there and nowhere else.
Transport is ENet, one unreliable channel and one reliable.

A snapshot fits one datagram, about 1100 bytes. In the steady state deltas are a few
bytes per soldier and this never binds; for a join and after loss, soldiers out of view
and old events are held back to the next snapshot by priority.

## Measured, not believed

A fake link in-process, with latency, jitter and loss, runs a server and clients in
one test binary, so the claims here become numbers in the log: how far a soldier sits
from where its owner put it under 30% loss; how many of the shots a client saw itself
land were ruled hits; bytes per second each way with six soldiers in view. Every
commit that changes the netcode says what it measured, on what line.

## The order of work

1. The simulation from the port-simulation branch onto main: bullets and their
   collisions, explosions, things, flags, kits, dropped guns, the parachute and the
   stationary gun, the corpses, with their tests. One system per commit. Netcode
   without them carries nothing.
2. The systems as passes that talk only through events, so the wire has a way in that
   is the simulation's own. Built: bullets are made from shots, wounds land in a pass
   of their own, the things lay down what left a hand, knock what was struck, hold
   what is held, and the soldiers take what the things gave. The passes' mail has its
   test; the scenes held throughout.
3. shared/network: the buffer, the netfields, the message table, the round-trip and
   refusal tests.
4. The join: Hello through the first snapshot, the headless server hosting, a headless
   client joining, over the fake link.
5. Movement: the two streams, extrapolation, blending, the checks. Built (stream.h):
   the client's state every tick and the snapshot every tick, each a delta against
   what the other side acknowledged, the server against the history ring for what it
   sent and the client against what it received; a slot the baseline did not carry
   goes whole, and the farthest soldiers are held back until a snapshot fits the
   datagram. Everyone steps a soldier heard of on its last keys, one-shots cleared,
   and lets them go after half a second's silence; a client takes its own soldier's
   word from the server only on a new life. New word snaps, as the original's does;
   blending it in is still to do. The checks so far: old states, unreadable ones and
   positions off the map are dropped. Measured on the loopback: a tick's client state
   under 60 bytes, a snapshot of two soldiers under 160.
6. Shots as events, the advance by ping, hits, deaths, respawns. Built (wire.h): a
   table classifies every event type and a test holds it; each travelling type has one
   routine both ways. Each side queues what it sends, numbered; a client keeps only
   its own decisions, the server everything that travels and remembers from whom, so an
   owner is never sent its own back. A packet carries the events past the other side's
   acknowledgement, capped, each with its seq and the tick it happened; the receiver
   applies each once into the game's mailbox, and a shot heard is run forward from its
   tick to now, at most half a second. A client's word about anyone but itself is
   dropped. The client's tick keeps to the server's from the snapshots. Measured on
   the loopback: a client's shots made on the server and numbered as its own, a bot's
   shots made on the client with its count in step, the server's wounds heard by the
   wounded.
7. Things, flags, kits, the match, rounds, chat. Built in part: the things ride the
   snapshot as the soldiers do, a word per slot and a delta against what the client
   received, out of the history ring which keeps them too; a client takes what a thing
   is and whose always, and where its points are only when its own disagree by more
   than ten units, never while held, as the original does. The match rides whole, small
   as it is, and a client's match is the server's alone: match_run is the authority's.
   A soldier that goes whole brings its player's name, so the roster needs no message.
   Held-back things and soldiers go farthest first. Chat was built with the join.
   Rounds (server/rounds.c): the match ends at its limits or on `nextmap`, the scores
   stand, and the next round begins on the next map of sv_maps, or the same again; the
   world is made anew with the history ring cleared, everyone joined is placed, and
   everyone hears the Map, a reliable message with the round's number, which is also
   how a joining client hears of its first round: joining and a new round are one path.
   Both streams are stamped with the round, and another round's are dropped, so packets
   that cross the change do no harm. Still to do: votes, and blending new word in.
