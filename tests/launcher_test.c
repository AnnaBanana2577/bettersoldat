// The launcher: its hashes, its manifests, its archives, and an update from one version
// to the next, end to end. The update reads its release from file:// URLs, which curl
// treats as it does GitHub's, out of a directory under build/.

#include <miniz.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "archive.h"
#include "files.h"
#include "http.h"
#include "manifest.h"
#include "sha256.h"
#include "test.h"
#include "update.h"

#ifdef _WIN32
#include <direct.h>
#define change_directory _chdir
#define current_directory _getcwd
#else
#include <unistd.h>
#define change_directory chdir
#define current_directory getcwd
#endif

#define SCRATCH "build/launcher-test"

static bool hex_of(const char *text, size_t size, const char *expected)
{
    Sha256 s;
    uint8_t digest[32];
    char hex[65];
    sha256_init(&s);
    // in uneven pieces, across the blocks' edges
    for (size_t at = 0; at < size;) {
        size_t piece = (at % 7) + 1;
        if (piece > size - at) piece = size - at;
        sha256_feed(&s, text + at, piece);
        at += piece;
    }
    sha256_finish(&s, digest);
    sha256_to_hex(digest, hex);
    return !strcmp(hex, expected);
}

static void hash_tests(void)
{
    CHECK(hex_of("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "the empty string's SHA-256");
    CHECK(hex_of("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "abc's SHA-256");
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(hex_of(two, strlen(two), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"),
          "a message that pads into a second block");
    char *million = malloc(1000000);
    memset(million, 'a', 1000000);
    CHECK(hex_of(million, 1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"),
          "a million a's, fed in pieces");
    free(million);
    uint8_t digest[32];
    CHECK(!sha256_from_hex("e3b0", digest) && !sha256_from_hex(
              "g3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", digest),
          "a short or non-hex digest is refused");
}

static void manifest_tests(void)
{
    const char *text =
        "// a comment\r\n"
        "version 1.2.3\n"
        "\n"
        "package update e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 10 pkg-update.zip\n"
        "file e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 Soldat Reloaded.exe\n"
        "file ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad 3 assets/maps/ctf_Ash.pms";
    Manifest m;
    char error[128];
    bool ok = manifest_parse(&m, text, strlen(text), error, sizeof error);
    CHECK(ok, "a manifest parses: %s", ok ? "" : error);
    CHECK(ok && !strcmp(m.version, "1.2.3") && m.count == 2, "its version and its two files");
    CHECK(ok && !strcmp(m.update.path, "pkg-update.zip") && m.update.size == 10 && !m.full.path[0],
          "the update package, and no full one");
    const ManifestFile *f = ok ? manifest_find(&m, "Soldat Reloaded.exe") : NULL;
    CHECK(f && f->size == 0 && f->sha256[0] == 0xe3, "a path with a space is the rest of the line");
    CHECK(ok && manifest_find(&m, "assets/maps/ctf_Ash.pms") && !manifest_find(&m, "ctf_Ash.pms"),
          "files are found by their whole path");
    manifest_free(&m);

    const char *unsafe[] = {"../x", "a/../b", "/etc/passwd", "C:/x", "a\\b", "a//b", "./a", "a/", ""};
    for (size_t i = 0; i < sizeof unsafe / sizeof unsafe[0]; i++)
        CHECK(!manifest_safe_path(unsafe[i]), "'%s' would reach outside the install", unsafe[i]);
    CHECK(manifest_safe_path("assets/maps/ctf_Ash.pms") && manifest_safe_path("a..b/c"), "ordinary paths are fine");

    const char *bad[] = {
        "file e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 x\n", // no version
        "version 1\nfile e3b0 0 x\n",
        "version 1\nfile e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 ../x\n",
        "version 1\npackage full e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 dir/x.zip\n",
        "version 1\nsomething else\n",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK(!manifest_parse(&m, bad[i], strlen(bad[i]), error, sizeof error), "bad manifest %zu is refused", i);
        manifest_free(&m);
    }
}

// --- archives --------------------------------------------------------------------------

typedef struct Bytes {
    uint8_t *data;
    size_t size;
} Bytes;

static void append(Bytes *b, const void *data, size_t size)
{
    b->data = realloc(b->data, b->size + size);
    memcpy(b->data + b->size, data, size);
    b->size += size;
}

// A tar entry: its header (with `name` in the header's field and, if given, `prefix`),
// then `size` bytes of `data`, padded to the block.
static void tar_entry(Bytes *b, const char *name, const char *prefix, char type, const char *data, size_t size)
{
    uint8_t h[512] = {0};
    memcpy(h, name, strlen(name) < 100 ? strlen(name) : 100);
    snprintf((char *)h + 100, 8, "%07o", type == '0' ? 0755 : 0644);
    snprintf((char *)h + 124, 12, "%011o", (unsigned)size);
    h[156] = (uint8_t)type;
    memcpy(h + 257, "ustar", 6);
    memcpy(h + 263, "00", 2);
    if (prefix) memcpy(h + 345, prefix, strlen(prefix));
    memset(h + 148, ' ', 8);
    unsigned sum = 0;
    for (int i = 0; i < 512; i++) sum += h[i];
    snprintf((char *)h + 148, 8, "%06o", sum);
    append(b, h, 512);
    append(b, data, size);
    uint8_t zeros[512] = {0};
    if (size % 512) append(b, zeros, 512 - size % 512);
}

static bool write_tar_gz(const char *path, const Bytes *tar)
{
    size_t packed_size = 0;
    void *packed = tdefl_compress_mem_to_heap(tar->data, tar->size, &packed_size, TDEFL_DEFAULT_MAX_PROBES);
    if (!packed) return false;
    uint8_t header[10] = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 0xff};
    uint32_t crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, tar->data, tar->size), isize = (uint32_t)tar->size;
    uint8_t trailer[8] = {(uint8_t)crc, (uint8_t)(crc >> 8), (uint8_t)(crc >> 16), (uint8_t)(crc >> 24),
                          (uint8_t)isize, (uint8_t)(isize >> 8), (uint8_t)(isize >> 16), (uint8_t)(isize >> 24)};
    Bytes gz = {0};
    append(&gz, header, sizeof header);
    append(&gz, packed, packed_size);
    append(&gz, trailer, sizeof trailer);
    mz_free(packed);
    bool ok = files_write(path, gz.data, gz.size);
    free(gz.data);
    return ok;
}

static bool holds(const char *path, const char *expected)
{
    char *text = files_read(path, NULL);
    bool same = text && !strcmp(text, expected);
    free(text);
    return same;
}

static void archive_tests(void)
{
    char error[256];
    files_remove_tree(SCRATCH);

    // Names of every kind tar writes: a short one, one split over the prefix, one in a
    // GNU long-name entry and one in a pax header, longer than a header holds.
    char long_name[300], pax_name[300], pax_record[400];
    snprintf(long_name, sizeof long_name, "pkg/assets/%0180d/gnu.txt", 0);
    snprintf(pax_name, sizeof pax_name, "pkg/assets/%0170d/pax.txt", 1);
    int record = (int)strlen(" path=\n") + (int)strlen(pax_name);
    record += snprintf(NULL, 0, "%d", record + 3); // the length counts its own digits
    snprintf(pax_record, sizeof pax_record, "%d path=%s\n", record, pax_name);

    Bytes tar = {0};
    tar_entry(&tar, "pkg/", NULL, '5', "", 0);
    tar_entry(&tar, "pkg/game", NULL, '0', "the game", 8);
    tar_entry(&tar, "maps/ctf_Ash.pms", "pkg/assets", '0', "a map", 5);
    tar_entry(&tar, "././@LongLink", NULL, 'L', long_name, strlen(long_name) + 1);
    tar_entry(&tar, "truncated", NULL, '0', "long", 4);
    tar_entry(&tar, "PaxHeaders/x", NULL, 'x', pax_record, strlen(pax_record));
    tar_entry(&tar, "truncated too", NULL, '0', "pax", 3);
    tar_entry(&tar, "pkg/empty", NULL, '0', "", 0);
    uint8_t end[1024] = {0};
    append(&tar, end, sizeof end);
    CHECK(write_tar_gz(SCRATCH "/pkg.tar.gz", &tar), "the test's tar.gz is written");
    bool ok = archive_extract(SCRATCH "/pkg.tar.gz", SCRATCH "/tar", NULL, NULL, error, sizeof error);
    CHECK(ok, "a tar.gz unpacks: %s", ok ? "" : error);
    CHECK(holds(SCRATCH "/tar/game", "the game"), "a file at the top, without the package's directory");
    CHECK(holds(SCRATCH "/tar/assets/maps/ctf_Ash.pms", "a map"), "a name split over the ustar prefix");
    char path[512];
    snprintf(path, sizeof path, SCRATCH "/tar/%s", long_name + 4);
    CHECK(holds(path, "long"), "a GNU long name");
    snprintf(path, sizeof path, SCRATCH "/tar/%s", pax_name + 4);
    CHECK(holds(path, "pax"), "a pax path");
    CHECK(holds(SCRATCH "/tar/empty", ""), "an empty file");
    free(tar.data);

    Bytes evil = {0};
    tar_entry(&evil, "pkg/../../escaped", NULL, '0', "x", 1);
    append(&evil, end, sizeof end);
    write_tar_gz(SCRATCH "/evil.tar.gz", &evil);
    CHECK(!archive_extract(SCRATCH "/evil.tar.gz", SCRATCH "/evil", NULL, NULL, error, sizeof error) &&
              !files_exists("build/escaped"),
          "an entry reaching outside is refused");
    free(evil.data);

    remove(SCRATCH "/pkg.zip");
    mz_zip_add_mem_to_archive_file_in_place(SCRATCH "/pkg.zip", "pkg/game.exe", "zipped", 6, NULL, 0, MZ_DEFAULT_LEVEL);
    mz_zip_add_mem_to_archive_file_in_place(SCRATCH "/pkg.zip", "pkg/assets/a.txt", "deep", 4, NULL, 0, MZ_DEFAULT_LEVEL);
    ok = archive_extract(SCRATCH "/pkg.zip", SCRATCH "/zip", NULL, NULL, error, sizeof error);
    CHECK(ok, "a zip unpacks: %s", ok ? "" : error);
    CHECK(holds(SCRATCH "/zip/game.exe", "zipped") && holds(SCRATCH "/zip/assets/a.txt", "deep"),
          "its files, without the package's directory");
}

// --- an update, end to end -------------------------------------------------------------

typedef struct Source {
    const char *path, *text;
} Source;

static void entry_line(char *line, size_t size, const char *kind, const char *file, const char *name)
{
    uint8_t digest[32];
    char hex[65];
    uint64_t bytes = 0;
    files_sha256(file, digest, NULL, NULL);
    files_size(file, &bytes);
    sha256_to_hex(digest, hex);
    snprintf(line, size, "%s %s %llu %s\n", kind, hex, (unsigned long long)bytes, name);
}

// A release of `files` as `xmake dist` makes one: the two zips under download/v<version>/
// and the manifest under latest/download/.
static void release(const char *root, const char *version, const Source *files, int count)
{
    char path[512], line[512];
    files_remove_tree(root);
    char full[512], update[512];
    snprintf(full, sizeof full, "%s/download/v%s/pkg-full.zip", root, version);
    snprintf(update, sizeof update, "%s/download/v%s/pkg-update.zip", root, version);
    files_make_parents(full);
    for (int i = 0; i < count; i++) {
        snprintf(path, sizeof path, "pkg/%s", files[i].path);
        size_t n = strlen(files[i].text);
        mz_zip_add_mem_to_archive_file_in_place(full, path, files[i].text, n, NULL, 0, MZ_DEFAULT_LEVEL);
        if (manifest_top_level(files[i].path) && strcmp(files[i].path, "config.cfg"))
            mz_zip_add_mem_to_archive_file_in_place(update, path, files[i].text, n, NULL, 0, MZ_DEFAULT_LEVEL);
    }

    char text[4096];
    size_t at = (size_t)snprintf(text, sizeof text, "version %s\n", version);
    entry_line(line, sizeof line, "package full", full, "pkg-full.zip");
    at += (size_t)snprintf(text + at, sizeof text - at, "%s", line);
    entry_line(line, sizeof line, "package update", update, "pkg-update.zip");
    at += (size_t)snprintf(text + at, sizeof text - at, "%s", line);
    for (int i = 0; i < count; i++) {
        if (!strcmp(files[i].path, "config.cfg")) continue;
        snprintf(path, sizeof path, "%s/source/%s", root, files[i].path);
        files_write(path, files[i].text, strlen(files[i].text));
        entry_line(line, sizeof line, "file", path, files[i].path);
        at += (size_t)snprintf(text + at, sizeof text - at, "%s", line);
    }
    snprintf(path, sizeof path, "%s/latest/download/latest-test.txt", root);
    files_write(path, text, at);
}

static void enter(const char *path) { CHECK(change_directory(path) == 0, "the test can enter %s", path); }

static void update_tests(void)
{
    char here[1024], releases[1200], version[32], error[512];
    if (!current_directory(here, sizeof here)) return;
    for (char *c = here; *c; c++)
        if (*c == '\\') *c = '/';
    snprintf(releases, sizeof releases, "file://%s%s/" SCRATCH "/releases", here[0] == '/' ? "" : "/", here);
    UpdateOptions options = {.releases = releases, .platform = "test"};

    // version 1, installed by hand with the player's own config
    const Source v1[] = {{"version.txt", "1\n"}, {"game.exe", "old game"}, {"assets/a.txt", "art"},
                         {"assets/b.txt", "more art"}, {"config.cfg", "theirs"}};
    release(SCRATCH "/releases", "1", v1, 5);
    files_remove_tree(SCRATCH "/install");
    files_make_directory(SCRATCH "/install");
    for (int i = 0; i < 4; i++) {
        char path[256];
        snprintf(path, sizeof path, SCRATCH "/install/%s", v1[i].path);
        files_write(path, v1[i].text, strlen(v1[i].text));
    }
    files_write(SCRATCH "/install/config.cfg", "mine", 4);
    files_write(SCRATCH "/install/scripts/server.lua", "a player's script", 17);

    if (change_directory(SCRATCH "/install") != 0) {
        CHECK(false, "the scratch install can be entered");
        return;
    }
    UpdateOutcome outcome = update_run(&options, NULL, version, sizeof version, error, sizeof error);
    CHECK(outcome == UPDATE_CURRENT && !strcmp(version, "1"),
          "an install with no manifest.txt that matches the release is current (%d: %s)", outcome, error);
    CHECK(files_exists(UPDATE_MANIFEST), "and is given its manifest.txt");

    // version 2 changes the game only: the update package
    const Source v2[] = {{"version.txt", "2\n"}, {"game.exe", "new game"}, {"assets/a.txt", "art"},
                         {"assets/b.txt", "more art"}, {"config.cfg", "theirs"}};
    enter(here);
    release(SCRATCH "/releases", "2", v2, 5);
    remove(SCRATCH "/releases/download/v2/pkg-full.zip"); // so only the update package can serve
    enter(SCRATCH "/install");
    outcome = update_run(&options, NULL, version, sizeof version, error, sizeof error);
    CHECK(outcome == UPDATE_UPDATED && !strcmp(version, "2"), "a new game comes in the update package (%d: %s)",
          outcome, error);
    CHECK(holds("game.exe", "new game") && holds("version.txt", "2\n"), "the new game, and its version");
    CHECK(holds("config.cfg", "mine") && holds("scripts/server.lua", "a player's script"),
          "the player's config and script are left alone");
    CHECK(!files_exists(UPDATE_STAGING), "nothing is left in .update");
    outcome = update_run(&options, NULL, version, sizeof version, error, sizeof error);
    CHECK(outcome == UPDATE_CURRENT, "then it is current (%d: %s)", outcome, error);

    // version 3 changes an asset: the full package
    const Source v3[] = {{"version.txt", "3\n"}, {"game.exe", "new game"}, {"assets/a.txt", "new art"},
                         {"assets/b.txt", "more art"}, {"assets/c.txt", "a new map"}, {"config.cfg", "theirs"}};
    enter(here);
    release(SCRATCH "/releases", "3", v3, 6);
    enter(SCRATCH "/install");
    outcome = update_run(&options, NULL, version, sizeof version, error, sizeof error);
    CHECK(outcome == UPDATE_UPDATED && holds("assets/a.txt", "new art") && holds("assets/c.txt", "a new map") &&
              holds("version.txt", "3\n"),
          "changed and new assets come in the full package (%d: %s)", outcome, error);
    CHECK(holds("config.cfg", "mine"), "whose config doesn't replace the player's");

    // damage: an asset cut short is found by its size, one changed in place only by --verify
    files_write("assets/b.txt", "more", 4);
    outcome = update_run(&options, NULL, version, sizeof version, error, sizeof error);
    CHECK(outcome == UPDATE_REPAIRED && holds("assets/b.txt", "more art"), "a damaged asset is repaired (%d: %s)",
          outcome, error);
    files_write("assets/b.txt", "MORE ART", 8);
    outcome = update_run(&options, NULL, version, sizeof version, error, sizeof error);
    CHECK(outcome == UPDATE_CURRENT, "an asset of the right size is trusted to manifest.txt");
    options.thorough = true;
    outcome = update_run(&options, NULL, version, sizeof version, error, sizeof error);
    CHECK(outcome == UPDATE_REPAIRED && holds("assets/b.txt", "more art"), "and hashed with --verify");
    options.thorough = false;
    files_write("game.exe", "NEW GAME", 8);
    outcome = update_run(&options, NULL, version, sizeof version, error, sizeof error);
    CHECK(outcome == UPDATE_REPAIRED && holds("game.exe", "new game"), "the executables are always hashed");

    // a download that isn't what the release lists is refused, and nothing is moved
    enter(here);
    files_write(SCRATCH "/releases/download/v3/pkg-update.zip", "not a zip", 9);
    enter(SCRATCH "/install");
    files_write("game.exe", "NEW GAME", 8);
    outcome = update_run(&options, NULL, version, sizeof version, error, sizeof error);
    CHECK(outcome == UPDATE_FAILED && holds("game.exe", "NEW GAME"), "a damaged download changes nothing");

    // no release to ask: the install as it is
    options.releases = "file:///nowhere/at/all";
    outcome = update_run(&options, NULL, version, sizeof version, error, sizeof error);
    CHECK(outcome == UPDATE_FAILED, "offline, damaged executables are reported (%d)", outcome);
    files_write("game.exe", "new game", 8);
    outcome = update_run(&options, NULL, version, sizeof version, error, sizeof error);
    CHECK(outcome == UPDATE_UNCHECKED && !strcmp(version, "3"), "offline, an intact install plays as it is (%d: %s)",
          outcome, error);

    // version 4 renames the game: the old name goes, and so does an old release's
    options.releases = releases;
    files_write("soldatreloaded.exe", "older game", 10);
    const Source v4[] = {{"version.txt", "4\n"}, {"client.exe", "new game"}, {"assets/a.txt", "new art"},
                         {"assets/b.txt", "more art"}, {"assets/c.txt", "a new map"}, {"config.cfg", "theirs"}};
    enter(here);
    release(SCRATCH "/releases", "4", v4, 6);
    enter(SCRATCH "/install");
    outcome = update_run(&options, NULL, version, sizeof version, error, sizeof error);
    CHECK(outcome == UPDATE_UPDATED && holds("client.exe", "new game"), "a renamed game comes in (%d: %s)", outcome,
          error);
    CHECK(!files_exists("game.exe") && !files_exists("soldatreloaded.exe"), "and the names it had are gone");
    CHECK(holds("config.cfg", "mine") && holds("scripts/server.lua", "a player's script"),
          "but not the player's own files");

    enter(here);
}

void launcher_tests(void)
{
    http_init();
    hash_tests();
    manifest_tests();
    archive_tests();
    update_tests();
    files_remove_tree(SCRATCH);
}
