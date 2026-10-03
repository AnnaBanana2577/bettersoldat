-- Keeps lines with the words given out of the chat, and tells whoever said one
-- (docs/scripting.md). A kept line goes no further: not to the players, and not to the
-- handlers taken up after this one. Taken up in scripts/main.lua:
--
--   require("examples.chat_filter")({words = {"noob"}})
--
-- words: what a line may not hold, in any case.

return function(options)
    options = options or {}
    local words = {}
    for _, word in ipairs(options.words or {}) do words[#words + 1] = word:lower() end

    server.on("chat", function(slot, text, team)
        local lower = text:lower()
        for _, word in ipairs(words) do
            if lower:find(word, 1, true) then
                server.say_to(slot, "That word stays with you.")
                return true
            end
        end
    end)
end
