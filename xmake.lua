-- SoldatReloaded: the client, the server, the simulation they share, the launcher that
-- keeps a player's copy up to date, and the tests.
--
--   xmake                the client, the server and the launcher
--   xmake run client     in runtime/, where data/, mods/, config/ and scripts/ are
--   xmake run server
--   xmake test           the headless checks in tests/
--   xmake dist           the packages, in build/release/: one for players, one for a server,
--                        the launcher's update, and the manifest it reads (launcher/update.h)
--
-- The game finds everything beside itself, in the directory it runs from: data/, what it
-- plays by (maps, animations, skeletons, bots); mods/, what it looks and sounds like
-- (mods/default/ and a player's own beside it); config/ and scripts/. runtime/ holds them
-- as an install lays them out, so that is runtime/ under xmake run (set_rundir) and the
-- package's own directory once unpacked, and nothing is passed on the command line. What
-- the game writes there as it plays (config/client/, config/server/, demos/) and a
-- player's mods are the player's, and not the project's.

set_project("soldatreloaded")
set_version("0.7.2")
includes("@builtin/xpack")

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

-- The game's icon, runtime/data/icon.ico, built into an executable on Windows: the one
-- Explorer, the taskbar and the window show, as SDL takes a window's icon from the first
-- in its executable. The resource script that names it is written here, at build time.
rule("icon")
    on_load(function (target)
        if not target:is_plat("windows") then return end
        local ico = path.join(os.projectdir(), "runtime", "data", "icon.ico"):gsub("\\", "/")
        local rc = path.join(target:autogendir(), "icon.rc")
        local text = ("1 ICON \"%s\"\n"):format(ico)
        -- written only when it changes: a new one relinks the executable, a new one each time
        if not os.isfile(rc) or io.readfile(rc) ~= text then io.writefile(rc, text) end
        target:add("files", rc)
    end)

-- The simulation and the data it reads, shared by the client and the server. No
-- rendering, audio or networking dependencies.
target("shared")
    set_kind("static")
    add_files("packages/shared/**.c")
    add_includedirs("packages/shared", {public = true})
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
    add_files("packages/client/**.c", "packages/server/connections.c", "packages/server/lists.c", "packages/server/rounds.c",
              "packages/server/bots.c", "packages/server/host.c")
    -- the launcher's HTTPS, for the server browser's list from the lobby (client/net/browser.c)
    add_files("packages/launcher/http.c", "packages/launcher/files.c", "packages/launcher/sha256.c")
    add_includedirs("packages/client", "packages/server", "packages/launcher")
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
    set_rundir("$(projectdir)/runtime")

-- The game server, headless: the same simulation with authority, ticked on its own
-- clock. Nothing but the console and the world until the netcode is ported.
--   xmake run server [+map <name>] [+<cvar> <value>] [+<command> <args>...]
target("server")
    set_kind("binary")
    add_deps("shared")
    add_files("packages/server/**.c")
    -- the launcher's HTTPS, for the lobby's heartbeat (server/lobby.c): it finds Linux's
    -- certificates for curl's mbedTLS
    add_files("packages/launcher/http.c", "packages/launcher/files.c", "packages/launcher/sha256.c")
    add_includedirs("packages/server", "packages/launcher")
    add_packages("lua", "libcurl")
    -- the version its requests say
    on_load(function (target)
        import("core.project.project")
        target:add("defines", 'SOLDATRELOADED_VERSION="' .. project.version() .. '"')
    end)
    if not is_plat("windows") then
        add_syslinks("pthread") -- the console's reader, the script's requests and the lobby's
    end
    set_rundir("$(projectdir)/runtime")

-- The launcher, what a player starts (launcher/main.c): it brings the install up to the
-- latest release on GitHub, in a small window of its own, and starts the client. Its
-- name is the one a player looks for on Windows; on Linux one with no spaces. It works on
-- the directory it sits in, so it is tried in an unpacked package, not under xmake run.
target("launcher")
    set_kind("binary")
    add_rules("icon")
    set_basename(is_plat("windows") and "Soldat Reloaded" or "soldatreloaded-launcher")
    add_files("packages/launcher/*.c")
    add_includedirs("packages/launcher")
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
    add_files("tests/*.c", "packages/server/connections.c", "packages/server/lists.c", "packages/server/rounds.c",
              "packages/server/bots.c", "packages/server/host.c", "packages/server/script.c", "packages/server/lobby.c")
    add_files("packages/launcher/*.c|main.c")
    -- the client's line and its demos, for the demo's round trip (tests/demo_test.c)
    add_files("packages/client/net/client_net.c", "packages/client/net/demo.c")
    add_includedirs("tests", "packages/server", "packages/launcher", "packages/client")
    add_packages("lua", "libcurl", "miniz")
    if not is_plat("windows") then
        add_syslinks("pthread") -- the script's requests
    end
    set_rundir("$(projectdir)")
    add_tests("default")

-- The release packages (xpack), each laid out as an install under one directory named
-- after it, soldatreloaded-<version>-<plat>-<arch>/: the launcher drops that directory as
-- it unpacks (launcher/archive.h), so a package without it would scatter. Windows gets
-- zips; Linux tar.gzs, which keep the executable bit that a zip would lose. What an
-- install holds is runtime/'s data/, mods/default/, config/defaults/ and scripts/, flat,
-- which is how the game expects to find them (docs/git.md, Releases); the steps of the
-- release's own (version.txt, the manifest, a tar.gz the old launchers read) are
-- xmake/release.lua's.
--
--   soldatreloaded          the game, a player's: everything, the launcher and the server
--                           among it, so anyone can host; and manifest.txt, what it all is
--   soldatreloaded-patch    the client's top-level files and config/defaults/: the
--                           executables, version.txt, manifest.txt and the licence. What the
--                           launcher downloads when nothing in data/, mods/default/ or
--                           scripts/ changed
--   soldatreloaded-server   a headless server's: data/ and no mods/, no art and no sound
--
-- `xmake dist` packs them in that order, into build/release/, beside the manifest the
-- launcher reads; the formats are launcher/manifest.h's, what the launcher does with them
-- update.h's.
local function release_package(name, suffix)
    xpack(name)
        set_formats(is_plat("windows", "mingw") and "zip" or "targz") -- the mingw cross-build's exes are Windows'
        set_basename("soldatreloaded-$(version)-$(plat)-$(arch)" .. suffix)
        set_prefixdir("soldatreloaded-$(version)-$(plat)-$(arch)" .. suffix)
        set_bindir(".")
        add_installfiles("license.md")
        add_installfiles("runtime/(config/defaults/**)") -- the game's; the player's own are made by the game
        after_package(function (package)
            import("release", {rootdir = path.join(os.projectdir(), "xmake")}).repack_targz(package)
        end)
end

-- What every package's install is given last: version.txt, the executables' bit; and for
-- the full package its manifest, which the update package carries too.
local function finish_install(manifest)
    after_installcmd(function (package, batchcmds)
        local executables = {}
        for _, target in ipairs(package:targets()) do
            table.insert(executables, target:filename())
        end
        local release = import("release", {rootdir = path.join(os.projectdir(), "xmake")})
        batchcmds:call(release.finish, {package:installdir(), package:version(), executables, not package:is_plat("windows", "mingw")})
        if manifest == "write" then
            batchcmds:call(release.write_manifest, {package:installdir(), package:version()})
        elseif manifest == "copy" then
            batchcmds:call(release.copy_manifest, {package:installdir()})
        end
    end)
end

-- The icons: the .ico only builds the executables, which hold it on Windows; the .png is
-- the window's and the menu entry's elsewhere (launcher/desktop.h), and a server has neither.
release_package("soldatreloaded", "")
    add_targets("client", "server", "launcher")
    add_installfiles("runtime/(data/**)|icon.ico|icon.png")
    if not is_plat("windows") then
        add_installfiles("runtime/(data/icon.png)")
    end
    add_installfiles("runtime/(mods/default/**)") -- the game's; a player's mods beside it are theirs
    add_installfiles("runtime/(scripts/**)")      -- the server's scripts, the example among them
    finish_install("write")

release_package("soldatreloaded-patch", "-patch")
    add_targets("client", "server", "launcher")
    finish_install("copy")

release_package("soldatreloaded-server", "-server")
    add_targets("server")
    add_installfiles("runtime/(data/**)|icon.ico|icon.png")
    add_installfiles("runtime/(scripts/**)")
    finish_install()

-- xmake dist: the three packages, in build/release/, and latest-<plat>-<arch>.txt, the
-- manifest the launcher reads: the version, the two packages it can download and every
-- file of the full one, with their hashes.
task("dist")
    set_category("action")
    set_menu({usage = "xmake dist", description = "package the client and the server for this platform"})
    on_run(function ()
        import("core.project.config")
        import("core.project.project")
        import("release", {rootdir = path.join(os.projectdir(), "xmake")})

        config.load()
        local outputdir = path.join(config.builddir(), "release")
        -- built once, and packed as built: each package carries the same executables, which
        -- the manifest they share names by hash
        os.execv(os.programfile(), {"build", "-y", "client", "server", "launcher"})
        for _, name in ipairs({"soldatreloaded", "soldatreloaded-patch", "soldatreloaded-server"}) do
            os.execv(os.programfile(), {"pack", "-y", "--autobuild=n", "-o", outputdir, name})
        end

        local version, plat, arch = project.version(), config.plat(), config.arch()
        local stem = path.join(outputdir, ("soldatreloaded-%s-%s-%s"):format(version, plat, arch))
        local extension = (plat == "windows" or plat == "mingw") and ".zip" or ".tar.gz"
        local latest = release.write_latest(outputdir, version, plat, arch, stem .. extension, stem .. "-patch" .. extension)
        print("listed " .. path.absolute(latest))
    end)
