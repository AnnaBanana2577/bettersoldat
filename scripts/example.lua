-- An example of the server's script (docs/scripting.md): copy it to scripts/server.lua,
-- or point sv_script at it, and it runs as the server starts. It greets whoever comes,
-- answers /stats and /top, lets a few names pause the game and skip the map, keeps a
-- word out of the chat, says who won, and reports each round to a webhook if one is set.

local webhook = ""      -- a Discord webhook URL, or any that takes JSON; empty sends nothing
local admins = {}       -- names allowed to pause and skip: admins["Name"] = true
local kept_word = "noob" -- a line with this in it goes no further

local function player_line(p)
    return ("%s %d/%d"):format(p.name, p.kills, p.deaths)
end

function on_join(slot, name)
    server.say_to(slot, "Welcome, " .. name .. ". Say /stats or /top.", "7FD6FF")
end

function on_leave(slot, name)
    server.print(name .. " left slot " .. slot)
end

function on_chat(slot, text, team)
    if text:lower():find(kept_word, 1, true) then
        server.say_to(slot, "That word stays with you.")
        return true
    end
end

function on_command(slot, text)
    local p = server.player(slot)
    if text == "stats" then
        server.say_to(slot, ("%d kills, %d deaths, %d captures, %d ms"):format(p.kills, p.deaths, p.flags, p.ping), "FFD700")
        return true
    elseif text == "top" then
        local best
        for _, q in ipairs(server.players()) do
            if not q.spectator and (not best or q.kills > best.kills) then best = q end
        end
        server.say(best and ("Top: " .. player_line(best)) or "Nobody yet")
        return true
    elseif admins[p.name] and (text == "pause" or text == "unpause") then
        if text == "pause" then server.pause() else server.unpause() end
        return true
    elseif admins[p.name] and text == "skip" then
        server.next_map()
        return true
    end
end

function on_kill(killer, victim, weapon)
    if killer == victim then return end
    local k, v = server.player(killer), server.player(victim)
    if k and v and k.kills > 0 and k.kills % 5 == 0 then
        server.say(("%s is on %d kills (%s)"):format(k.name, k.kills, weapon), "FF8800")
    end
end

function on_capture(slot, team)
    local p = server.player(slot)
    if p then server.say(("%s scores for %s!"):format(p.name, team)) end
end

function on_match_end(winner)
    if winner then server.say(winner .. " team wins the round", "FFD700")
    else server.say("The round is a draw", "FFD700") end
end

function on_round_end(stats)
    local lines = {}
    for _, p in ipairs(stats.players) do
        if not p.spectator then lines[#lines + 1] = player_line(p) end
    end
    local report = ("Round %d on %s over (%s): alpha %d, bravo %d. %s"):format(
        stats.round, stats.map, stats.why, stats.scores.alpha, stats.scores.bravo, table.concat(lines, ", "))
    server.print(report)
    if webhook ~= "" then
        http.post(webhook, {content = report}, nil, function(r)
            if r.error then server.print("the webhook failed: " .. r.error)
            elseif r.status >= 300 then server.print("the webhook answered " .. r.status) end
        end)
    end
end

function on_round_start(map)
    server.say("Now playing " .. map)
end
