-- The players' figures (docs/scripting.md): /stats for your own and /top for whoever leads,
-- every so many kills called out, captures and the round's winner said. Taken up in
-- scripts/main.lua:
--
--   require("examples.stats")({streak = 5})
--
-- streak: a player's kills are called out at every multiple of it; 0 for never.

local function player_line(p)
    return ("%s %d/%d"):format(p.name, p.kills, p.deaths)
end

return function(options)
    options = options or {}
    local streak = options.streak or 5

    server.on("command", function(slot, text)
        if text == "stats" then
            local p = server.player(slot)
            server.say_to(slot, ("%d kills, %d deaths, %d captures, %d ms"):format(p.kills, p.deaths, p.flags, p.ping),
                          "FFD700")
            return true
        elseif text == "top" then
            local best
            for _, q in ipairs(server.players()) do
                if not q.spectator and (not best or q.kills > best.kills) then best = q end
            end
            server.say(best and ("Top: " .. player_line(best)) or "Nobody yet")
            return true
        end
    end)

    server.on("kill", function(killer, victim, weapon)
        if killer == victim or streak <= 0 then return end
        local k = server.player(killer)
        if k and k.kills > 0 and k.kills % streak == 0 then
            server.say(("%s is on %d kills (%s)"):format(k.name, k.kills, weapon), "FF8800")
        end
    end)

    server.on("capture", function(slot, team)
        local p = server.player(slot)
        if p then server.say(("%s scores for %s!"):format(p.name, team)) end
    end)

    server.on("match_end", function(winner)
        if winner then server.say(winner .. " team wins the round", "FFD700")
        else server.say("The round is a draw", "FFD700") end
    end)
end
