-- The release's own steps around xpack's (xmake.lua: the packages and `xmake dist`): what
-- an install holds beside its files, the manifest the launcher checks it against, and the
-- tar.gz a launcher can read. The formats are launcher/manifest.h's.
--
-- Its steps run as xpack lays an install out (batchcmds:call), each in a sandbox of its
-- own, so what they share is local to this file rather than its globals.

-- Where the full package leaves its manifest for the update package, which carries the
-- same: it is packed first (`xmake dist`).
local function _manifest_stash()
    return path.join(import("core.project.config").builddir(), ".xpack", "manifest.txt")
end

-- "<sha256> <bytes> <name>"
local function _entry(file, name)
    return ("%s %d %s"):format(hash.sha256(file), os.filesize(file), name)
end

-- What every package holds beside its files: version.txt, and on Linux the executables'
-- bit, which nothing else is sure to keep. xpack's debug symbols go: a player has no use
-- for them.
function finish(installdir, version, executables, linux)
    io.writefile(path.join(installdir, "version.txt"), version .. "\n")
    for _, file in ipairs(os.files(path.join(installdir, "*.pdb"))) do
        os.rm(file)
    end
    if linux then
        for _, name in ipairs(executables) do
            os.vrunv("chmod", {"+x", path.join(installdir, name)})
        end
    end
end

-- Every file of the install but the manifest itself, by path, written into it as
-- manifest.txt and left for the update package. The player's own files (config/client/,
-- config/server/, their mods) are in no package, so in no manifest either.
function write_manifest(installdir, version)
    local names = {}
    for _, file in ipairs(os.files(path.join(installdir, "**"))) do
        local name = path.relative(file, installdir):gsub("\\", "/")
        if name ~= "manifest.txt" then
            table.insert(names, name)
        end
    end
    table.sort(names)
    local lines = {"// What this install holds, which the launcher checks it against.", "version " .. version}
    for _, name in ipairs(names) do
        table.insert(lines, "file " .. _entry(path.join(installdir, name), name))
    end
    local text = table.concat(lines, "\n") .. "\n"
    io.writefile(path.join(installdir, "manifest.txt"), text)
    io.writefile(_manifest_stash(), text)
end

-- The full package's manifest, into the update package's install.
function copy_manifest(installdir)
    assert(os.isfile(_manifest_stash()), "the full package is packed first: run `xmake dist`")
    os.cp(_manifest_stash(), path.join(installdir, "manifest.txt"))
end

-- A tar.gz packed again by tar itself. xmake's archiver gzips its own output file (empty,
-- just made) before the tar, so its tar.gz is two gzip members, an empty one first, and
-- the launchers shipped before 0.7.2 read only the first. The install's root is the
-- package's directory (set_prefixdir), so the archive's is too.
function repack_targz(package)
    if package:format() ~= "targz" then
        return
    end
    local archivefile = path.absolute(package:outputfile())
    os.tryrm(archivefile)
    os.vrunv("tar", {"-czf", archivefile, package:prefixdir()}, {curdir = package:install_rootdir()})
end

-- latest-<plat>-<arch>.txt, the manifest the launcher reads: the full package's, with the
-- two packages it can download named beside it.
function write_latest(outputdir, version, plat, arch, full_archive, update_archive)
    local lines = io.readfile(_manifest_stash()):split("\n")
    table.remove(lines, 1) -- its comment, for one of the release's own
    table.insert(lines, 2, "package update " .. _entry(update_archive, path.filename(update_archive)))
    table.insert(lines, 3, "package full " .. _entry(full_archive, path.filename(full_archive)))
    local latest = path.join(outputdir, ("latest-%s-%s.txt"):format(plat, arch))
    io.writefile(latest, ("// SoldatReloaded %s for %s %s, for the launcher (launcher/update.h).\n"):format(version, plat, arch)
                         .. table.concat(lines, "\n") .. "\n")
    return latest
end
