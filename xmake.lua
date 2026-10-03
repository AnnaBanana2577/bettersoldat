-- SoldatReloaded: the client, the server, the simulation they share, the launcher that
-- keeps a player's copy up to date, and the tests.
--
--   xmake                the client, the server and the launcher
--   xmake run client     from the project directory, where config.cfg and assets/ are
--   xmake run server
--   xmake test           the headless checks in tests/
--   xmake dist           the packages, in build/dist/: one for players, one for a server,
--                        the launcher's update, and the manifest it reads (launcher/update.h)
--
-- The game finds everything beside itself: config.cfg and assets/ in the directory it
-- runs from. That is the project directory under xmake run (set_rundir) and the
-- package's own directory once unpacked, so nothing is passed on the command line.

set_project("soldatreloaded")
set_version("0.7.2")

add_rules("mode.debug", "mode.release")
set_languages("c11")
set_warnings("all")

if is_plat("windows") then
    add_defines("_CRT_SECURE_NO_WARNINGS")
else
    -- -std=c11 hides what glibc has beyond ISO C (dirent's d_type, clock_gettime, nanosleep)
    add_defines("_DEFAULT_SOURCE")
end

-- The libraries, built static so a package is the executables and nothing to find at
-- run time. SDL2 gives the client its window, input and GL context and stb its images,
-- as in the original (opensoldat's client/Gfx.pas); OpenGL itself is loaded at run time
-- through SDL_GL_GetProcAddress, so nothing links against a GL library. ENet is the
-- wire, for the netcode as it is ported. On Linux SDL2 builds against the system's X11,
-- Wayland and audio development headers, which have to be installed first.
add_requires("libsdl2", "enet", {configs = {shared = false}})
add_requires("stb")
-- The server's script runs on Lua (server/script.c), and its requests go through
-- libcurl; curl uses the system's TLS on Windows and macOS, and mbedTLS, built in,
-- elsewhere. The client needs neither.
add_requires("lua 5.4.x", {configs = {shared = false}})
add_requires("libcurl", {configs = {shared = false, mbedtls = not is_plat("windows", "macosx")}})
-- The launcher downloads with the same curl, and unpacks the packages with miniz: a zip
-- on Windows, and the deflate inside a Linux tar.gz.
add_requires("miniz")

-- The game's icon, assets/icon.ico, built into an executable on Windows: the one
-- Explorer, the taskbar and the window show, as SDL takes a window's icon from the first
-- in its executable. The resource script that names it is written here, at build time.
rule("icon")
    on_load(function (target)
        if not target:is_plat("windows") then return end
        local ico = path.join(os.projectdir(), "assets", "icon.ico"):gsub("\\", "/")
        local rc = path.join(target:autogendir(), "icon.rc")
        io.writefile(rc, ("1 ICON \"%s\"\n"):format(ico))
        target:add("files", rc)
    end)

-- The simulation and the data it reads, shared by the client and the server. No
-- rendering, audio or networking dependencies.
target("shared")
    set_kind("static")
    add_files("shared/**.c")
    add_includedirs("shared", {public = true})
    add_packages("enet", {public = true}) -- the transport (shared/network) is ENet's
    if not is_plat("windows") then
        add_syslinks("m", {public = true})
    end

-- The game client: SDL2 for the window and input, OpenGL 2.1 for the drawing, and
-- audio. The server's host is built in (server/host.c and what it stands on), for Local
-- Play: the client hosts a game and joins it over the loopback.
--   xmake run client [+map <name>] [+<cvar> <value>] [+<command> <args>...]
target("client")
    set_kind("binary")
    add_rules("icon")
    add_deps("shared")
    add_files("client/**.c", "server/connections.c", "server/rounds.c", "server/bots.c", "server/host.c")
    -- the launcher's HTTPS, for the server browser's list from the lobby (client/net/browser.c)
    add_files("launcher/http.c", "launcher/files.c", "launcher/sha256.c")
    add_includedirs("client", "server", "launcher")
    add_packages("libsdl2", "stb", "libcurl")
    if not is_plat("windows") then
        add_syslinks("pthread") -- curl's resolver
    end
    -- the escape menu shows the version xmake.lua sets
    on_load(function (target)
        import("core.project.project")
        target:add("defines", 'SOLDATRELOADED_VERSION="' .. project.version() .. '"')
    end)
    if is_plat("windows") then
        -- SDL2main provides main and WinMain; with neither in our objects the linker can't
        -- infer the subsystem. A release is a windowed program, with no console window
        -- beside the game (the game has its own); a debug build keeps one for stderr.
        if is_mode("debug") then
            add_ldflags("/SUBSYSTEM:CONSOLE")
        else
            add_ldflags("/SUBSYSTEM:WINDOWS")
        end
    end
    set_rundir("$(projectdir)")

-- The game server, headless: the same simulation with authority, ticked on its own
-- clock. Nothing but the console and the world until the netcode is ported.
--   xmake run server [+map <name>] [+<cvar> <value>] [+<command> <args>...]
target("server")
    set_kind("binary")
    add_deps("shared")
    add_files("server/**.c")
    -- the launcher's HTTPS, for the lobby's heartbeat (server/lobby.c): it finds Linux's
    -- certificates for curl's mbedTLS
    add_files("launcher/http.c", "launcher/files.c", "launcher/sha256.c")
    add_includedirs("server", "launcher")
    add_packages("lua", "libcurl")
    -- the version its requests say
    on_load(function (target)
        import("core.project.project")
        target:add("defines", 'SOLDATRELOADED_VERSION="' .. project.version() .. '"')
    end)
    if not is_plat("windows") then
        add_syslinks("pthread") -- the console's reader, the script's requests and the lobby's
    end
    set_rundir("$(projectdir)")

-- The launcher, what a player starts (launcher/main.c): it brings the install up to the
-- latest release on GitHub, in a small window of its own, and starts the client. Its
-- name is the one a player looks for on Windows; on Linux one with no spaces. It works on
-- the directory it sits in, so it is tried in an unpacked package, not under xmake run.
target("launcher")
    set_kind("binary")
    add_rules("icon")
    set_basename(is_plat("windows") and "Soldat Reloaded" or "soldatreloaded-launcher")
    add_files("launcher/*.c")
    add_includedirs("launcher")
    add_packages("libsdl2", "stb", "libcurl", "miniz")
    add_defines('SOLDATRELOADED_RELEASES="https://github.com/soldatreloaded/soldatreloaded/releases"')
    -- the version it says, and the platform whose manifest it asks for (latest-windows-x64.txt)
    on_load(function (target)
        import("core.project.project")
        target:add("defines", 'SOLDATRELOADED_VERSION="' .. project.version() .. '"')
        target:add("defines", 'SOLDATRELOADED_PLATFORM="' .. target:plat() .. "-" .. target:arch() .. '"')
    end)
    if is_plat("windows") then
        if is_mode("debug") then
            add_ldflags("/SUBSYSTEM:CONSOLE")
        else
            add_ldflags("/SUBSYSTEM:WINDOWS")
        end
    else
        add_syslinks("pthread")
    end

-- The tests: headless checks of what shared/ holds, of the server's join, streams and
-- rounds over the loopback (the server's systems are built into them), and of the
-- launcher's manifests, archives and updates. Not built by default; run them with
--   xmake test
target("tests")
    set_kind("binary")
    set_default(false)
    add_deps("shared")
    add_files("tests/*.c", "server/connections.c", "server/rounds.c", "server/bots.c", "server/host.c", "server/script.c",
              "server/lobby.c")
    add_files("launcher/*.c|main.c")
    -- the client's line and its demos, for the demo's round trip (tests/demo_test.c),
    -- and its taunts, for the taunt editor's round trip (tests/taunts_test.c)
    add_files("client/net/client_net.c", "client/net/demo.c", "client/ui/taunts.c")
    add_includedirs("tests", "server", "launcher", "client")
    add_packages("lua", "libcurl", "miniz")
    if not is_plat("windows") then
        add_syslinks("pthread") -- the script's requests
    end
    set_rundir("$(projectdir)")
    add_tests("default")

-- xmake dist: what a release holds for this platform, in build/dist/. Each package
-- unpacks to one directory holding the executables, version.txt, config.cfg, the
-- licence and assets/, flat, which is how the game expects to find them (docs/git.md,
-- Releases). Windows gets zips; Linux tar.gzs, which keep the executable bit that a zip
-- would lose.
--
--   <stem>               the game, a player's: everything, the launcher and the server among it,
--                        so anyone can host; and manifest.txt, what it all is
--   <stem>-patch         the client's top-level files but config.cfg: the executables,
--                        version.txt, manifest.txt and the licence. What the launcher
--                        downloads when nothing in assets/ or scripts/ changed
--   <stem>-server        a headless server's: only what it reads of the assets, no art
--                        and no sound
--   latest-<plat>-<arch>.txt  the manifest the launcher reads: the version, the two
--                        packages and every file of the client's, with their hashes
--
-- The formats are launcher/manifest.h's; what the launcher does with them, update.h's.
task("dist")
    set_category("action")
    set_menu({usage = "xmake dist", description = "package the client and the server for this platform"})
    on_run(function ()
        import("core.project.config")
        import("core.project.project")
        import("utils.archive")

        config.load()
        os.execv(os.programfile(), {"build", "-y", "client", "server", "launcher"})

        local plat, arch = config.plat(), config.arch()
        local version = project.version()
        local distdir = path.join(config.buildir(), "dist")
        local stem = ("soldatreloaded-%s-%s-%s"):format(version, plat, arch)
        -- the mingw cross-build's exes are Windows', so they want a zip too
        local extension = (plat == "windows" or plat == "mingw") and ".zip" or ".tar.gz"
        local client, server, launcher = project.target("client"), project.target("server"), project.target("launcher")

        -- the art and the sound: the client's alone
        local function server_needs(name)
            return not (name:endswith("-gfx") or name == "textures" or name == "custom-interfaces" or name == "sfx"
                        or name == "icon.png" or name == "icon.ico" or name == "play-regular.ttf" or name == "play-bold.ttf"
                        or name == "russo-one.ttf" or name == "black-ops-one.ttf" or name == "OFL.txt"
                        or name == "mod.ini")
        end

        -- the package's directory, laid out as an install
        local function lay_out(name, targets, needs)
            local dir = path.join(distdir, name)
            os.tryrm(dir)
            os.mkdir(path.join(dir, "assets"))
            for _, target in ipairs(targets) do
                os.cp(target:targetfile(), dir)
            end
            os.cp("config.cfg", dir)
            os.cp("license.md", dir)
            io.writefile(path.join(dir, "version.txt"), version .. "\n")
            if os.isdir("scripts") then os.cp("scripts", dir) end -- the server's scripts, the example among them
            for _, entry in ipairs(os.filedirs("assets/*")) do
                local base = path.filename(entry)
                if needs(base) then
                    os.cp(entry, path.join(dir, "assets", base))
                end
            end
            return dir
        end

        local function pack(name)
            -- absolute: the archiver runs inside distdir so the directory's name is the archive's root
            local archivefile = path.absolute(path.join(distdir, name .. extension))
            os.tryrm(archivefile)
            if extension == ".tar.gz" then
                -- tar itself, not xmake's archiver: that gzips its own output file (empty, just
                -- made) before the tar, so the package comes out as two gzip members, an
                -- empty one first, which the launchers shipped before 0.7.2 can't read
                os.vrunv("tar", {"-czf", archivefile, name}, {curdir = distdir})
            else
                archive.archive(archivefile, name, {curdir = distdir})
            end
            print("packaged " .. archivefile)
            return archivefile
        end

        local function entry(file, name)
            return ("%s %d %s"):format(hash.sha256(file), os.filesize(file), name)
        end

        -- Every file of an install but config.cfg, which is the player's, and the
        -- manifest itself, by path.
        local function manifest(dir)
            local paths = {}
            for _, file in ipairs(os.files(path.join(dir, "**"))) do
                local name = path.relative(file, dir):gsub("\\", "/")
                if name ~= "config.cfg" and name ~= "manifest.txt" then
                    table.insert(paths, name)
                end
            end
            table.sort(paths)
            local lines = {"version " .. version}
            for _, name in ipairs(paths) do
                table.insert(lines, "file " .. entry(path.join(dir, name), name))
            end
            return lines
        end

        -- everything but the icons that aren't read: icon.ico is in the Windows
        -- executables, and icon.png is for Linux's windows and menu entry (launcher/desktop.h)
        local full = lay_out(stem, {client, server, launcher}, function (name)
            return name ~= "icon.ico" and not (plat == "windows" and name == "icon.png")
        end)
        local files = manifest(full)
        io.writefile(path.join(full, "manifest.txt"),
                     "// What this install holds, which the launcher checks it against.\n" .. table.concat(files, "\n") .. "\n")
        local full_archive = pack(stem)

        local update = path.join(distdir, stem .. "-patch")
        os.tryrm(update)
        os.mkdir(update)
        for _, file in ipairs(os.files(path.join(full, "*"))) do
            if path.filename(file) ~= "config.cfg" then os.cp(file, update) end
        end
        local update_archive = pack(stem .. "-patch")

        lay_out(stem .. "-server", {server}, server_needs)
        pack(stem .. "-server")

        local latest = path.join(distdir, ("latest-%s-%s.txt"):format(plat, arch))
        table.insert(files, 2, "package update " .. entry(update_archive, path.filename(update_archive)))
        table.insert(files, 3, "package full " .. entry(full_archive, path.filename(full_archive)))
        io.writefile(latest, "// SoldatReloaded " .. version .. " for " .. plat .. " " .. arch .. ", for the launcher (launcher/update.h).\n"
                             .. table.concat(files, "\n") .. "\n")
        print("listed " .. path.absolute(latest))
    end)
