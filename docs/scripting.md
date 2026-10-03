# Scripting

The server runs a Lua script (Lua 5.4), named by `sv_script`: `scripts/main.lua` by
default, read once as the server starts, if the file is there. The game's Local Play is
that server, started beside the game, so its script runs there too. It, and every script it
`require`s, hands the server functions to call when things happen (`server.on`), and
calls the server back through the `server` table. Requests to the web go through `http`,
with `json` for their bodies.

`scripts/main.lua` is the server owner's: a game's launcher brings a newer one only while it
is as the game made it, and a server unpacked from its own package makes it on its first
start, as that package hasn't one. The game's examples are in `scripts/examples/`, kept
current by every update: a greeter, the players' figures (/stats, /top), a chat filter, admin commands among friends,
the game run from the chat as gathers run it (`!p`, `!up` with a count of 3, 2, 1, `!r`,
`!map ash`), and a report of each round to a webhook. Each returns a function that sets it
up, so
`main.lua` takes up as many as it likes, each with its settings:

```lua
require("examples.greeter")({welcome = "Welcome, %s."})
require("examples.round_webhook")({url = "https://discord.com/api/webhooks/..."})
```

Their lines are in `main.lua`, commented out. A script may `require` any file beside
`main.lua`, so scripts of your own go there too. An example changed in place is undone by
the next update: to change one, copy it beside `main.lua` under a name of your own.

The script runs on the server's thread, between ticks, so nothing it does races the
game, and anything slow it does stalls the game: a request is sent from a thread of its
own and answered later, on the server's thread, for that reason. An error in the script
is printed on the console and the call is dropped; the game goes on.

Console commands: `pause` and `unpause`; `script_reload` reads the script again from the
start, losing its state; `lua <code>` runs a line in the script's state.

## What the script hears

`server.on(event, fn)` hands the server `fn` to call on `event`, and gives `fn` back;
`server.off(event, fn)` takes it off again (`true` if it was on). Any script may hand in
as many as it likes: each event's handlers are called in the order they were handed in,
and an error in one is printed on the console and the next is called. For `chat` and
`command`, the first handler to return `true` ends it: the line is kept, or the command
answered, and the handlers after it don't hear it.

A global function named `on_<event>` (`on_join`, `on_chat`, ...), as a lone script may
still define it, is called after the handlers handed in.

| event | the handler's arguments | when | return |
|---|---|---|---|
| `chat` | `slot, text, team` | a player said `text` (`team`: to its team alone), before anyone else hears it | `true` keeps the line from everyone else |
| `command` | `slot, text` | a player said `/text` and the server has no such command (`team`, `votemap`, `votekick`, `yes` and `no` are its own) | `true` answers it; else "Unknown command" |
| `join` | `slot, name` | a player (or a bot) has joined | |
| `leave` | `slot, name` | one has left; its slot is already empty | |
| `kill` | `killer, victim, weapon` | a kill, with the weapon's name; `killer == victim` for a suicide | |
| `capture` | `slot, team` | the flag scored by `slot`, for `team` | |
| `spawn` | `slot` | a soldier placed, or placed anew | |
| `match_end` | `winner` | the round's limit reached, or `nextmap`: `winner` is `"alpha"`, `"bravo"` or `nil`; the scores then stand a few seconds | |
| `round_end` | `stats` | just before the next map loads; `stats` below | |
| `round_start` | `map` | the next round has begun on `map` | |
| `tick` | `tick` | every tick, 60 a second: keep it quick | |
| `second` | | once a second | |

`stats` holds `why` (`"limit"`, `"nextmap"` or `"vote"`), `map`, `round`, `time_left`
in seconds, `scores` (`{alpha = n, bravo = n}`), `winner` (a team's name in capture the
flag, the top scorer's slot in a deathmatch, or `nil`) and `players`, a list of player
tables.

A player table has `slot`, `name`, `team` (`"none"`, `"alpha"`, `"bravo"`,
`"charlie"`, `"delta"` or `"spectator"`), `kills`, `deaths`, `flags`, `ping`, `health`,
and `bot`, `alive` and `spectator` as booleans. Slots count from 0 and are what the
players are known by on the wire; a slot is reused once its player has left.

## What the script may do

| call | what |
|---|---|
| `server.say(text [, color])` | a line to everyone, in the script colour, or `color`: `"RRGGBB"` or `{r, g, b}` |
| `server.say_to(slot, text [, color])` | the same to one player |
| `server.print(text)` | a line on the server's console only |
| `server.command(text)` | a console command, as if typed: `"say hello"`, `"addbot1"`, `"nextmap"`, `"sv_password x"` (the password to join, read live) |
| `server.pause()`, `server.unpause()` | the game stands still, nobody moving and the clock stopped, or goes on; `true` if that changed anything |
| `server.paused()` | whether it stands |
| `server.next_map([map])` | the round ends now; on `map` if given, else the rotation's next: `true`, or `false` (and nothing changes) for a map the server hasn't got, which it couldn't load |
| `server.maps()` | the server's list of maps, the one its votes and map window pick from: the rotation (`maplist.txt`, or `sv_maps` given on the command line), or every map it has when there is none |
| `server.map()`, `server.round()`, `server.mode()` | the map, the round from 1, `"ctf"` or `"dm"` |
| `server.tick()`, `server.time_left()` | the world's tick; the seconds left in the round |
| `server.scores()` | `{alpha = n, bravo = n}` |
| `server.players()` | every player's table, in slot order |
| `server.player(slot)` | one player's, or `nil` |
| `server.kick(slot [, reason])` | the player put off, told `reason`; a bot is simply removed |
| `server.add_bot([team [, name]])` | a bot, on `"alpha"` or `"bravo"` (else the emptier side), from `data/bots` by `name` or at random: its slot, or `nil` |

## The web

```lua
http.request({url = "https://example.org/hook", method = "POST", body = "...",
              headers = {["Content-Type"] = "application/json"}, timeout = 15},
             function(response) ... end)
http.get(url, callback [, headers])
http.post(url, body [, content_type] [, callback])
```

`method` is `GET` unless given, or `POST` when there is a body; `timeout` is seconds,
15 unless given. A table given as `http.post`'s body is sent as JSON. The callback, if
there is one, gets `{status = 200, body = "..."}`, or `{status = 0, error = "..."}` when
nothing came back; it runs on the server's thread, some time after the call, so a
request never holds the game up. Redirects are followed. The server was built with its
platform's TLS (Windows and macOS) or mbedTLS, so `https` works.

`json.encode(value)` and `json.decode(text)` go between Lua and JSON: a table with keys
1..n is an array, any other an object, `json.null` stands for null (as `nil` cannot be
held in a table), and `json.array(t)` marks an empty table as an array.

## An example

Two scripts side by side: `main.lua` takes up the game's greeter and a script of its own,
`scripts/rounds.lua`, and each hears the joins.

```lua
-- scripts/main.lua
require("examples.greeter")({welcome = "Welcome, %s. Say /stats for your figures."})
require("rounds")({url = "https://discord.com/api/webhooks/..."})

server.on("command", function(slot, text)
    if text == "stats" then
        local p = server.player(slot)
        server.say_to(slot, ("%d kills, %d deaths"):format(p.kills, p.deaths), "FFD700")
        return true
    end
end)
```

```lua
-- scripts/rounds.lua
return function(options)
    local joined = 0
    server.on("join", function() joined = joined + 1 end)
    server.on("round_end", function(stats)
        local lines = {}
        for _, p in ipairs(stats.players) do
            lines[#lines + 1] = ("%s %d/%d"):format(p.name, p.kills, p.deaths)
        end
        http.post(options.url, {content = ("Round over on %s, %d joined: %s"):format(
            stats.map, joined, table.concat(lines, ", "))})
    end)
end
```
