-- bettersoldat: the client, the server, the simulation they share, and the tests.
--
--   xmake                the client and the server
--   xmake run client     from the project directory, where config.cfg and assets/ are
--   xmake run server
--   xmake test           the headless checks in tests/
--   xmake dist           the packages, in build/dist/: one for players, one for a server
--
-- The game finds everything beside itself: config.cfg and assets/ in the directory it
-- runs from. That is the project directory under xmake run (set_rundir) and the
-- package's own directory once unpacked, so nothing is passed on the command line.

set_project("bettersoldat")
set_version("0.1.0")

add_rules("mode.debug", "mode.release")
set_languages("c11")
set_warnings("all")

if is_plat("windows") then
    add_defines("_CRT_SECURE_NO_WARNINGS")
end

-- The libraries, built static so a package is the executables and nothing to find at
-- run time. SDL2 gives the client its window, input and GL context and stb its images,
-- as in the original (opensoldat's client/Gfx.pas); OpenGL itself is loaded at run time
-- through SDL_GL_GetProcAddress, so nothing links against a GL library. ENet is the
-- wire, for the netcode as it is ported. On Linux SDL2 builds against the system's X11,
-- Wayland and audio development headers, which have to be installed first.
add_requires("libsdl2", "enet", {configs = {shared = false}})
add_requires("stb")

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
--   xmake run client [+map <name>] [+<cvar> <value>] [+<command> <args>...]
target("client")
    set_kind("binary")
    set_basename("bettersoldat")
    add_deps("shared")
    add_files("client/**.c")
    add_includedirs("client")
    add_packages("libsdl2", "stb", "enet")
    if is_plat("windows") then
        -- SDL2main provides main; with none in our objects the linker can't infer the subsystem
        add_ldflags("/SUBSYSTEM:CONSOLE")
    end
    set_rundir("$(projectdir)")

-- The game server, headless: the same simulation with authority, ticked on its own
-- clock. Nothing but the console and the world until the netcode is ported.
--   xmake run server [+map <name>] [+<cvar> <value>] [+<command> <args>...]
target("server")
    set_kind("binary")
    set_basename("bettersoldat-server")
    add_deps("shared")
    add_files("server/**.c")
    add_includedirs("server")
    add_packages("enet")
    set_rundir("$(projectdir)")

-- The tests: headless checks of what shared/ holds. Not built by default; run them with
--   xmake test
target("tests")
    set_kind("binary")
    set_default(false)
    add_deps("shared")
    add_files("tests/*.c")
    add_includedirs("tests")
    set_rundir("$(projectdir)")
    add_tests("default")

-- xmake dist: the packages for this platform, in build/dist/. Each unpacks to one
-- directory holding the executables, config.cfg, the licence and assets/, flat, which is
-- how the game expects to find them (docs/git.md, Releases). The client's package holds
-- the server too, so anyone can host. The server's package holds only what a headless
-- server reads of the assets: no art and no sound. Windows gets a zip; Linux a tar.gz,
-- which keeps the executable bit that a zip would lose.
task("dist")
    set_category("action")
    set_menu({usage = "xmake dist", description = "package the client and the server for this platform"})
    on_run(function ()
        import("core.project.config")
        import("core.project.project")
        import("utils.archive")

        config.load()
        os.execv(os.programfile(), {"build", "-y", "client", "server"})

        local plat, arch = config.plat(), config.arch()
        local distdir = path.join(config.buildir(), "dist")
        local stem = ("bettersoldat-%s-%s-%s"):format(project.version(), plat, arch)
        local client, server = project.target("client"), project.target("server")

        -- the art and the sound: the client's alone
        local function server_needs(name)
            return not (name:endswith("-gfx") or name == "textures" or name == "custom-interfaces" or name == "sfx"
                        or name == "icon.bmp" or name == "play-regular.ttf" or name == "OFL.txt" or name == "mod.ini")
        end

        local function package(name, targets, needs)
            local dir = path.join(distdir, name)
            os.tryrm(dir)
            os.mkdir(path.join(dir, "assets"))
            for _, target in ipairs(targets) do
                os.cp(target:targetfile(), dir)
            end
            os.cp("config.cfg", dir)
            os.cp("license.md", dir)
            for _, entry in ipairs(os.filedirs("assets/*")) do
                local base = path.filename(entry)
                if needs(base) then
                    os.cp(entry, path.join(dir, "assets", base))
                end
            end

            -- absolute: the archiver runs inside distdir so the directory's name is the archive's root
            local archivefile = path.absolute(path.join(distdir, name .. (plat == "windows" and ".zip" or ".tar.gz")))
            os.tryrm(archivefile)
            archive.archive(archivefile, name, {curdir = distdir})
            print("packaged " .. archivefile)
        end

        package(stem .. "-client", {client, server}, function () return true end)
        package(stem .. "-server", {server}, server_needs)
    end)
