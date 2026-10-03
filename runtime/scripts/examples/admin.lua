-- Lets the names given pause the game and skip the map from the chat: /pause, /unpause and
-- /skip (docs/scripting.md). A name is anyone's to take, so for real admins see
-- config/admins.txt; this is for a game among friends. Taken up in scripts/main.lua:
--
--   require("examples.admin")({names = {"Major", "Kruger"}})
--
-- names: who may.

return function(options)
    options = options or {}
    local allowed = {}
    for _, name in ipairs(options.names or {}) do allowed[name] = true end

    server.on("command", function(slot, text)
        local p = server.player(slot)
        if not (p and allowed[p.name]) then return end
        if text == "pause" then
            server.pause()
            return true
        elseif text == "unpause" then
            server.unpause()
            return true
        elseif text == "skip" then
            server.next_map()
            return true
        end
    end)
end
