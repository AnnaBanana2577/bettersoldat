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
  simulation proposing a wound; only the server turns it into damage, but its knockback
  and bink land wherever it is produced, as the original writes a victim's NextPush
  wherever the bullet is simulated: the owner's word about its soldier stands, so the
  owner must feel the knock itself.
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
heavy loss the backlog is capped, oldest held back, kills and pickups first. A client
holds the server's events until the tick of their frame is on show (WirePending), and
acknowledges them as it receives them: what is held need not come again.

**Reliable, rarely:** Hello, Welcome, Denied, and Chat, which carries commands and
votes as text as well. Nothing else.

## Time

One clock, the server's tick, carried on every snapshot. A client's tick is the tick
of the frame it shows, kept against the newest snapshot it holds: the clock runs free
and is nudged a tick at a time when the frames in hand run consistently over or under
(client_stream_begin_tick; `cl_interp` keeps it some ticks behind the newest, which is
off by default). A client runs its own soldier at the present with no delay and no
correction. Everyone else it takes from their newest word and steps on to the tick on
show, and past it by a lead, with their last controls through the ordinary
`soldier_step` with `armed` false, so a remote soldier moves but never fires from its
keys. The lead (`cl_extrapolate`, on by default) is half my ping and half theirs in
ticks (ping_lead), which is how far their last word stands behind where they are by
now, so the picture is at now and not where everyone last was. A word that arrives
late is stepped on the further and moves nothing that stepping had right, so the
line's jitter never reaches the picture; what the word does move is what stepping
could not foresee, a key pressed since, and that blends in over cl_smooth rather than
snapping. No word for half a second releases the keys, so a quiet player falls and
stops. The server's events ride the same clock: a client keeps them until their tick
is on show. A shot heard has flown half my ping less half the shooter's longer than
its stamp says from here (ping_shift), and is run forward by as much more, so it sits
where the shooter's soldier, shown at now, has it; and it gets the flash, the smoke and
the sound at that soldier's muzzle, which its unarmed step would never give.

A shot is an event from the owner, stamped with its tick, which names the frame the
shooter had. The server runs the bullet forward from that tick to its own present and,
each step of the way, judges it against what the shooter drew at that step: the frame
the shooter's screen held, out of the history ring, and the target in it stepped on by
the shooter's lead, which the server can do too, the step being the same everywhere and
the lead a function of the two pings it measures itself (bullet_target,
history_future). Caught up, the bullet meets the present stepped on by the lead like
any other. So what landed on the shooter's screen lands on the server, however far the
target has moved since, and whatever the target did since, and the round trip never
cheats the shooter of a hit. The price falls on the target, who can be hit where it
was drawn, which is where its last word said it was going: a target that turned inside
the last half ping and half ping is hit where it would have been. That is the trade of
showing everyone at now, and it is the original's trade too; `cl_extrapolate 0` shows
everyone where they were last heard of instead, and the server, told so in the client's
state, judges that player's shots on the frames alone, so a target is only ever hit
where it was. The shooter plays its own flash, sound and blood at once, and the health
on the server's damage event.

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

## The query, and the lobby

Beside the game, on the same port, a server answers a query: one datagram in, one out,
outside ENet (shared/network/query.h has the bytes). A query asks what the server is
playing, and the answer is its name, map, mode, its people and its bots, its room,
whether it asks a password, and the wire's version. The transport catches a query in
ENet's intercept before ENet reads it, so nothing on the line changes for a peer. A
request is padded to at least the length of any answer, so a forged source address
gains its victim nothing, and it carries a nonce the answer echoes.

The lobby (the soldatreloaded-lobby repository) is the list of servers. A server that
wants listing says so over HTTP with its port. The lobby takes the address from the
connection, never from what the server says, and asks the server the query before it
lists it, so a server nobody can reach is never on the list. It drops a server that
stops saying so. A browser fetches the list and asks each server the query itself:
that gives the ping, and players counted now, not at the last heartbeat.

## Measured, not believed

The line is made bad outside the game: a network impairment tool on the loopback puts
latency, jitter and loss between a client and a server on one machine, and the game's
own counters say what came of it. The client's stream counts the snapshots that came
late, the ticks it had no snapshot for, the nudges and jumps of its view clock, where
`interp` stands, and how far the snapshots moved the others from where stepping had
them, which is the jitter the picture would show unsmoothed (ClientStream); `cl_netstats
1` prints them on the console once a second, with the ping and the frames in hand. The
server's counts the states it dropped (ServerStream). The headless tests hold the rest on the loopback alone: the rewind
(tests/rewind_test.c), the streams and their sizes (tests/stream_test.c). Every commit
that changes the netcode says what it measured, on what line.

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
   word from the server only on a new life, and the server takes a client's word only for
   the life it is of (the state says which): the states a client sends before it hears
   of a placing would drag the new life back to where the old one stood. New word
   snaps the simulation, as the
   original's does, but not the picture: what a correction moved another player by is
   kept as an offset the renderer adds, shrinking away over cl_smooth (100 ms); a
   placing, or a jump past STREAM_SNAP_DISTANCE, shows at once. The checks so far:
   old states, unreadable ones and positions off the map are dropped. Measured on the
   loopback: a tick's client state under 60 bytes, a snapshot of two soldiers under 160.
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
   wounded. The judging came later: as first built, an advanced bullet met the
   soldiers at the server's present, which is the round trip past where the shooter
   saw them, so a moving target was missed by more than a hitbox at any ordinary
   ping, and the field meant to carry the shooter's view lag was never set. It now
   meets the shooter's frames out of the history ring, as the Time section says, and
   tests/rewind_test.c holds that a shot landed on the shooter's screen lands on the
   server a sixth of a second later with the target run on past it, and that the same
   aim judged at the present misses. Then the others at now: each stepped on from its
   last word by the lead the two pings give, a late word stepped on the further so
   jitter moves nothing, and the server stepping its frames on by the same lead to
   meet what the shooter drew (cl_extrapolate; cl_interp keeps the view behind the
   newest instead, for the line that needs it). A tracer that stood in for a flight
   nobody saw was tried on the way and dropped: with the shot placed where the
   shooter as drawn has it, there is no gap to draw.
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
   that cross the change do no harm. A player's look and the weapons of its next spawn
   are the served half's but its own to choose: the look rides the Hello once, a look
   being for a game as in the original, and the loadout the Hello and then the client
   state, as the menu changes it; the server takes them as said, and a client keeps its
   own over the server's word of them. The team's shirt goes on
   where the gostek is drawn. The match's mode (deathmatch, or CTF on a map with a flag's
   spawn) is decided by the map once, on the server and alone alike, and rides the
   snapshot in the match, so the client's HUD, team box, spawn and shirts follow it
   rather than the map's name. Votes ride the chat, as planned: a line beginning with
   '/' is a command the server reads and never relays (/team, /votemap, /votekick,
   /yes, /no); /team is the team menu's choice, on which the server places the soldier
   anew, and in a game with teams a newcomer watches as a spectator until it chooses.
   A spectator is a soldier present and dead that the simulation passes by;
   one vote runs at a time for a minute and passes on 51% of the players; a map vote
   passed is handed to the server's loop, a kick is a Denied and the line cut. The one
   message added is Vote, the server's word of a vote begun or over, for the HUD. The
   server's own chat lines carry a kind, the original's colour class (who came to which
   team, who was kicked, a vote's word), so the client colours them as the original. The
   server's console reads its standard input on a thread, so nextmap and the rest can
   be typed at it. For the HUD alone the served half also relays two things of the
   player's the simulation never reads: whether it is typing (a bit in its client
   state) and its round trip as the server measures it; a kick vote carries the reason
   typed, and the Map the server's name.
