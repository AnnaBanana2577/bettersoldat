set_project("csoldat")
set_version("0.1.0")

add_rules("mode.debug", "mode.release")
set_languages("c11")
set_warnings("all")

if is_plat("windows") then
    add_defines("_CRT_SECURE_NO_WARNINGS")
end

add_requires("raylib 5.5")

-- The simulation and the data it reads, shared by the client and the server. No
-- rendering, audio or networking dependencies.
target("shared")
    set_kind("static")
    add_files("shared/**.c")
    add_includedirs("shared", {public = true})
    if not is_plat("windows") then
        add_syslinks("m", {public = true})
    end

-- The game client: raylib for the window, input, drawing and (later) audio.
--   xmake run client -assets <opensoldat base dir> -map <name>
target("client")
    set_kind("binary")
    add_deps("shared")
    add_files("client/**.c")
    add_includedirs("client")
    add_packages("raylib")
    set_rundir("$(projectdir)")
