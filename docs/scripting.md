# Scripting

The server runs one Lua script (Lua 5.4), named by `sv_script`: `scripts/server.lua`
by default, read once as the server starts, if the file is there. The script defines
functions the server calls when things happen, and calls the server back through the
`server` table. Requests to the web go through `http`, with `json` for their bodies.
`scripts/example.lua` shows all of it in use; copy it to `scripts/server.lua` to start
from.

The script runs on the server's thread, between ticks, so nothing it does races the
game, and anything slow it does stalls the game: a request is sent from a thread of its
own and answered later, on the server's thread, for that reason. An error in the script
is printed on the console and the call is dropped; the game goes on.

Console commands: `pause` and `unpause`; `script_reload` reads the script again from the
start, losing its state; `lua <code>` runs a line in the script's state.

## What the script hears

Define any of these; the rest are simply never called.

| function | when | return |
|---|---|---|
| `on_chat(slot, text, team)` | a player said `text` (`team`: to its team alone), before anyone else hears it | `true` keeps the line from everyone else |
| `on_command(slot, text)` | a player said `/text` and the server has no such command (`team`, `votemap`, `votekick`, `yes` and `no` are its own) | `true` answers it; else "Unknown command" |
| `on_join(slot, name)` | a player (or a bot) has joined | |
| `on_leave(slot, name)` | one has left; its slot is already empty | |
| `on_kill(killer, victim, weapon)` | a kill, with the weapon's name; `killer == victim` for a suicide | |
| `on_capture(slot, team)` | the flag scored by `slot`, for `team` | |
| `on_spawn(slot)` | a soldier placed, or placed anew | |
| `on_match_end(winner)` | the round's limit reached, or `nextmap`: `winner` is `"alpha"`, `"bravo"` or `nil`; the scores then stand a few seconds | |
| `on_round_end(stats)` | just before the next map loads; `stats` below | |
| `on_round_start(map)` | the next round has begun on `map` | |
| `on_tick(tick)` | every tick, 60 a second: keep it quick | |
| `on_second()` | once a second | |

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
| `server.next_map([map])` | the round ends now; on `map` if given, else the rotation's next |
| `server.map()`, `server.round()`, `server.mode()` | the map, the round from 1, `"ctf"` or `"dm"` |
| `server.tick()`, `server.time_left()` | the world's tick; the seconds left in the round |
| `server.scores()` | `{alpha = n, bravo = n}` |
| `server.players()` | every player's table, in slot order |
| `server.player(slot)` | one player's, or `nil` |
| `server.kick(slot [, reason])` | the player put off, told `reason`; a bot is simply removed |
| `server.add_bot([team [, name]])` | a bot, on `"alpha"` or `"bravo"` (else the emptier side), from `assets/bots` by `name` or at random: its slot, or `nil` |

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

```lua
-- scripts/server.lua
local webhook = "https://discord.com/api/webhooks/..."

function on_join(slot, name)
    server.say_to(slot, "Welcome, " .. name .. ". Say /stats for your figures.")
end

function on_command(slot, text)
    if text == "stats" then
        local p = server.player(slot)
        server.say_to(slot, ("%d kills, %d deaths"):format(p.kills, p.deaths), "FFD700")
        return true
    end
end

function on_round_end(stats)
    local lines = {}
    for _, p in ipairs(stats.players) do
        lines[#lines + 1] = ("%s %d/%d"):format(p.name, p.kills, p.deaths)
    end
    http.post(webhook, {content = ("Round over on %s: %s"):format(stats.map, table.concat(lines, ", "))})
end
```
