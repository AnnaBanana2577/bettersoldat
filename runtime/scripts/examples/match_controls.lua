-- The game run from the chat, as gathers and clan wars want it (docs/scripting.md):
--
--   !p, !pause         the game stands still
--   !up, !unpause      a count of 3, 2, 1, then it goes on, so nobody is caught moving
--   !r, !restart       the round on this map starts again
--   !map <name>        the round ends and the next is on that map: `ash` finds ctf_Ash, as
--                      does any start of a map's name no other map shares
--
-- Each line goes to the chat like any other, and its command runs on the next tick, so the
-- answer follows the line rather than coming before it. Taken up in scripts/main.lua:
--
--   require("examples.match_controls")({spectators = false, countdown = 3})
--
-- spectators: whether a spectator may run the game too; not unless set.
-- countdown:  the seconds !up counts down from.
-- maps:       the maps !map may load; the server's list (server.maps(): the rotation, or
--             every map it has) unless given.
-- color:      the colour of what it says.

local TICKS_PER_SECOND = 60

return function(options)
    options = options or {}
    local color = options.color or "7FD6FF"
    local seconds = options.countdown or 3
    local pending = {}    -- commands said, to run on the next tick
    local countdown = nil -- ticks left until the game goes on, while !up counts

    local function who(slot)
        local p = server.player(slot)
        return p and p.name or "someone"
    end

    -- A map's name without the mode's prefix, lowercased: ctf_Ash, ash.
    local function bare(name)
        return (name:lower():gsub("^%a+_", ""))
    end

    -- The map `name` means, from the list: as given in any case, with or without its
    -- prefix, or as the start of one map's name alone. Nil and the maps it could be when
    -- it is the start of several.
    local function resolve(name)
        local want = name:lower():gsub("^%s+", ""):gsub("%s+$", "")
        if want == "" then return nil, {} end
        local candidates = {}
        for _, map in ipairs(options.maps or server.maps()) do
            if map:lower() == want or bare(map) == bare(want) then return map end
            if bare(map):sub(1, #bare(want)) == bare(want) then candidates[#candidates + 1] = map end
        end
        if #candidates == 1 then return candidates[1] end
        return nil, candidates
    end

    local function pause(slot)
        countdown = nil
        if server.pause() then server.say(("Game paused by %s. !up to go on."):format(who(slot)), color)
        else server.say_to(slot, "The game is already paused.", color) end
    end

    local function unpause(slot)
        if not server.paused() then
            server.say_to(slot, "The game is not paused.", color)
        elseif not countdown then
            countdown = seconds * TICKS_PER_SECOND
            server.say(tostring(seconds), color)
        end
    end

    local function restart(slot)
        local map = server.map()
        server.say(("%s restarts the round on %s"):format(who(slot), map), color)
        server.next_map(map)
    end

    local function change_map(slot, name)
        local map, candidates = resolve(name)
        if not map then
            if #candidates > 1 then
                server.say_to(slot, ("'%s' could be %s."):format(name, table.concat(candidates, ", ", 1, math.min(#candidates, 6))), color)
            else
                server.say_to(slot, ("No map called '%s'."):format(name), color)
            end
        elseif server.next_map(map) then
            server.say(("%s changes the map to %s"):format(who(slot), map), color)
        else
            server.say_to(slot, ("The server hasn't got %s to load."):format(map), color)
        end
    end

    local COMMANDS = {p = pause, pause = pause, up = unpause, unpause = unpause, r = restart, restart = restart,
                      map = change_map}

    server.on("chat", function(slot, text, team)
        local name, argument = text:match("^!(%a+)%s*(.-)%s*$")
        local command = name and COMMANDS[name:lower()]
        if not command then return end
        local p = server.player(slot)
        pending[#pending + 1] = function()
            if p and p.spectator and not options.spectators then
                server.say_to(slot, "Spectators don't run the game.", color)
            else
                command(slot, argument)
            end
        end
    end)

    -- The commands said, and the !up count: by the server's ticks, which run on while the
    -- world's clock stands paused.
    server.on("tick", function()
        if #pending > 0 then
            local now = pending
            pending = {}
            for _, run in ipairs(now) do run() end
        end
        if not countdown then return end
        countdown = countdown - 1
        if countdown <= 0 then
            countdown = nil
            server.unpause()
            server.say("Go!", color)
        elseif countdown % TICKS_PER_SECOND == 0 then
            server.say(tostring(countdown // TICKS_PER_SECOND), color)
        end
    end)

    -- a new round starts unpaused, with no count running
    server.on("round_start", function()
        countdown = nil
    end)
end
