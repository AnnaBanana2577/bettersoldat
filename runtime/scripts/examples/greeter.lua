-- Greets whoever joins, and says each map as it begins (docs/scripting.md). Taken up in
-- scripts/main.lua:
--
--   require("examples.greeter")({welcome = "Welcome, %s. Say /stats or /top."})
--
-- welcome: what a player is told as they join, %s their name.

return function(options)
    options = options or {}
    local welcome = options.welcome or "Welcome, %s."

    server.on("join", function(slot, name)
        server.say_to(slot, welcome:format(name), "7FD6FF")
    end)

    server.on("leave", function(slot, name)
        server.print(name .. " left slot " .. slot)
    end)

    server.on("round_start", function(map)
        server.say("Now playing " .. map)
    end)
end
