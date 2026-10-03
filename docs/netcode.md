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
  and score, kit and weapon pickup, the match's end, a new round; and where a shot ended
  (EVENT_SHOT_END), for the shots slow enough that a miss shows: a blast (grenades,
  rockets, clusters, flak) and an arrow or thrown knife stopped in a body. The same
  bullet flies everywhere, but not against the same soldiers: the server judges it
  against the shooter's view, a client against its own present, so a grenade that went
  off on a player at the server can roll on over that player's corpse at the player's
  own client. Hearing the word, a client puts its flight of the shot where the server's
  ended and ends it the same way; one it has already ended stays ended. Plain bullets
  are not told: too quick for a miss to show, and many enough to crowd the queue.
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
off by default; when no snapshot has the tick on show, the newest before it is applied
instead, so the server's word of a placing or a death never waits on the clock). A
client runs its own soldier at the present with no delay and no
correction, and shows everyone else where the game last heard they were, moving as
they were moving: each is taken from its newest word and stepped on to the tick on
show with its last controls through the ordinary `soldier_step` with `armed` false, so
a remote soldier moves but never fires from its keys, and is never stepped past the
tick on show, as the original's are not. A word that arrives late is stepped on the
further and moves nothing that stepping had right, the step being the same everywhere,
so the line's jitter never reaches the picture; what a word does move is what stepping
could not foresee, a key pressed since, and that blends in rather than snapping: the
picture keeps its place and its speed at the moment of the word and eases to the
corrected place as a critically damped spring does, nine tenths of the way over
cl_smooth (100 ms) (client_stream_smooth). No word for half a second releases the
keys, so a quiet player falls and stops. The server's events ride the same clock: a
client keeps them until their tick is on show. A shot heard is run forward from its
stamp to the tick on show, so it is where its shooter has it by then, half the
shooter's ping ahead of the shooter's soldier as drawn; and it gets the flash, the
smoke and the sound at that soldier's muzzle, which its unarmed step would never give.
Over the distance it skipped it is drawn as the original draws it (TBullet.Render,
PingAdd): a round's own art stretched back from it toward where it was fired, faint,
in place of the round, shrinking as the ticks it was run count down four a tick, and
drawn on after the bullet is gone until they have; so a shot at a high ping is seen
leaving the muzzle rather than starting out ahead of it.

A shot is an event from the owner, stamped with its tick, which names the frame the
shooter had. The server runs the bullet forward from that tick to its own present and,
each step of the way, judges it against the frame the shooter's screen held at that
step, out of the history ring (bullet_target, history_targets), until it has caught up
and meets the present like any other bullet. So what landed on the shooter's screen
lands on the server however far the target has moved since, and the round trip never
cheats the shooter of a hit; the price falls on the target, who can be hit a round trip
after reaching cover, as in every game that rewinds. What is left is what the shooter's
screen could not know: a key the target changed, or a frame lost, inside the last round
trip. The shooter plays its own flash, sound and blood at once, and the health on the
server's damage event.

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

The lobby (the soldatreloaded-lobby repository) is the list of servers. A dedicated
server with `sv_public 1` says it is up every half minute over HTTPS, with its port
(server/lobby.c, on a thread of its own so the ticks never wait for it). The lobby lists
the address the request came from, and asks the server the query there before listing
it, so a server nobody can reach is never on the list; a 422 on the server's console
is a port not forwarded. The request goes over IPv4, as ENet is IPv4 only. A server
behind a proxy that sends from another address than players reach it on (Fly's
fly-global-services) names the one they reach in `sv_lobby_ip`; the lobby still asks
the query there, so naming another's address can list nothing but a real server. A
server leaving says goodbye, and one that stops saying it is up drops off after a
minute and a half. A game hosted from the menu is never listed.

The browser (client/net/browser.c, the main menu's Servers page) fetches the lobby's
list as plain lines, `1.2.3.4:23073`, and asks every server on it the query from one
socket, again after a second, and gives up after three. What it shows is what each
server answered, so the players are counted now and the ping is the browser's own; the
lobby only knows where the servers are. A server on another wire version is shown
greyed and can't be joined from there, and one that asks a password sends the player
to the Join page with its address filled in.

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
   aim judged at the present misses. Then the words applied by stepping: a late word
   stepped on to the tick on show, so jitter moves nothing, and cl_interp to keep the
   view behind the newest for a line that needs it. Two things were tried on the way
   and dropped: a tracer standing in for the flight nobody saw, and the others stepped
   on past their last word by half each ping to show them at now, with the server
   stepping its frames on by the same lead to judge; the stepping ahead was a
   prediction, wrong whenever a key changed inside it, and the game shows everyone
   where it last heard they were, as the original does. The flight nobody saw is now
   drawn as the original draws it, the round's own art stretched back over the
   distance run (the Time section), not a tracer of our own; tests/rewind_test.c holds
   that a client's shot heard and run forward carries it and the server's never does.
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
