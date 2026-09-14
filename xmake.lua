set_project("csoldat")
set_version("0.1.0")

add_rules("mode.debug", "mode.release")
set_languages("c11")
set_warnings("all")

if is_plat("windows") then
    add_defines("_CRT_SECURE_NO_WARNINGS")
end

-- The client's window, input and GL context come from SDL2 and its images from stb,
-- as the original's do (opensoldat's client/Gfx.pas). OpenGL itself is loaded at run
-- time through SDL_GL_GetProcAddress, so nothing links against a GL library.
add_requires("libsdl2", "stb")

-- The simulation and the data it reads, shared by the client and the server. No
-- rendering, audio or networking dependencies.
target("shared")
    set_kind("static")
    add_files("shared/**.c")
    add_includedirs("shared", {public = true})
    if not is_plat("windows") then
        add_syslinks("m", {public = true})
    end

-- The game client: SDL2 for the window and input, OpenGL 2.1 for the drawing, and
-- (later) audio.
--   xmake run client -assets <opensoldat base dir> -map <name>
target("client")
    set_kind("binary")
    add_deps("shared")
    add_files("client/**.c")
    add_includedirs("client")
    add_packages("libsdl2", "stb")
    if is_plat("windows") then
        -- SDL2main provides main; with none in our objects the linker can't infer the subsystem
        add_ldflags("/SUBSYSTEM:CONSOLE")
    end
    set_rundir("$(projectdir)")
