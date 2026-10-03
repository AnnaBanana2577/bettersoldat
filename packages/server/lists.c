#include "lists.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"             // the launcher's, for the directory
#include "network/transport.h" // ENet, for the addresses

#define BANLIST "banlist.txt"
#define MUTELIST "mutelist.txt"
#define ADMINS "admins.txt"
#define LIST_WORDS 7
#define HWID_DIGITS (NET_HWID_SIZE - 1)

static void copy(char *out, size_t size, const char *s) { snprintf(out, size, "%s", s ? s : ""); }

bool lists_address(const char *text, uint32_t *host)
{
    ENetAddress a = {0};
    int parts = 0, dots = 0;
    for (const char *p = text; *p; p++) {
        if (*p == '.') dots++;
        else if (*p < '0' || *p > '9') return false;
        else parts++;
    }
    if (dots != 3 || parts == 0 || enet_address_set_host_ip(&a, text) != 0) return false;
    *host = a.host;
    return true;
}

void lists_address_text(uint32_t host, char *out, size_t size)
{
    ENetAddress a = {.host = host};
    if (enet_address_get_host_ip(&a, out, size) != 0) copy(out, size, "?");
}

bool lists_hwid(const char *text, char out[NET_HWID_SIZE])
{
    if (!text || strlen(text) != HWID_DIGITS) return false;
    for (int i = 0; i < HWID_DIGITS; i++)
        if (!isxdigit((unsigned char)text[i])) return false;
    for (int i = 0; i < HWID_DIGITS; i++) out[i] = (char)toupper((unsigned char)text[i]);
    out[HWID_DIGITS] = '\0';
    return true;
}

// Whether an entry's address and hardware ID name the player with these: either, where
// both sides have it.
static bool names(uint32_t entry_host, const char *entry_hwid, uint32_t host, const char *hwid)
{
    if (entry_host && host && entry_host == host) return true;
    return entry_hwid[0] && hwid && hwid[0] && strcmp(entry_hwid, hwid) == 0;
}

// A hardware ID as the lists keep it: in capitals, empty if it isn't one.
static void hwid_of(char out[NET_HWID_SIZE], const char *hwid)
{
    if (!lists_hwid(hwid, out)) out[0] = '\0';
}

// A line's words: spaces between them, "quotes" round one that has spaces, // ending it.
static int words(char *line, char *word[LIST_WORDS])
{
    int n = 0;
    char *p = line;
    while (n < LIST_WORDS) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '\r' || *p == '\n' || (p[0] == '/' && p[1] == '/')) break;
        if (*p == '"') {
            word[n++] = ++p;
            while (*p && *p != '"') p++;
        } else {
            word[n++] = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
        }
        if (!*p) break;
        *p++ = '\0';
    }
    return n;
}

static void path_of(const Lists *l, const char *file, char *out, size_t size) { snprintf(out, size, "%s/%s", l->dir, file); }

static void make_missing(const Lists *l);

// Each line of a list's file that begins with `verb`, to `take`.
static void read_list(Lists *l, const char *file, const char *verb, void (*take)(Lists *, char **, int))
{
    char path[600];
    path_of(l, file, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *w[LIST_WORDS];
        int n = words(line, w);
        if (n >= 2 && strcmp(w[0], verb) == 0) take(l, w, n);
    }
    fclose(f);
}

// An entry's address and hardware ID from its words at `w`: the two of them ("-" for
// either it doesn't name), or the address alone in a line from before hardware IDs. How
// many words they took; 0 if they name nobody.
static int who_of(char **w, int n, uint32_t *host, char hwid[NET_HWID_SIZE])
{
    *host = 0;
    hwid[0] = '\0';
    if (n < 1) return 0;
    bool address = strcmp(w[0], "-") != 0 && lists_address(w[0], host);
    bool paired = n > 1 && (strcmp(w[1], "-") == 0 || lists_hwid(w[1], hwid));
    if (!paired) hwid[0] = '\0';
    if (!address && !hwid[0]) return 0;
    return paired ? 2 : 1;
}

static void take_ban(Lists *l, char **w, int n)
{
    uint32_t host;
    char hwid[NET_HWID_SIZE];
    int took = who_of(w + 1, n - 1, &host, hwid);
    if (!took || l->ban_count == MAX_BANS) return;
    int at = 1 + took; // the time, the name, why
    Ban *b = &l->bans[l->ban_count++];
    *b = (Ban){.host = host, .expires = n > at ? strtoll(w[at], NULL, 10) : 0};
    copy(b->hwid, sizeof b->hwid, hwid);
    copy(b->name, sizeof b->name, n > at + 1 ? w[at + 1] : "");
    copy(b->reason, sizeof b->reason, n > at + 2 ? w[at + 2] : "");
}

static void take_mute(Lists *l, char **w, int n)
{
    uint32_t host;
    char hwid[NET_HWID_SIZE];
    int took = who_of(w + 1, n - 1, &host, hwid);
    if (!took || l->mute_count == MAX_MUTES) return;
    ListEntry *e = &l->mutes[l->mute_count++];
    *e = (ListEntry){.host = host};
    copy(e->hwid, sizeof e->hwid, hwid);
    copy(e->name, sizeof e->name, n > 1 + took ? w[1 + took] : "");
}

static void take_admin(Lists *l, char **w, int n)
{
    uint32_t host;
    if (l->admin_count == MAX_ADMINS || !lists_address(w[1], &host)) return;
    ListEntry *e = &l->admins[l->admin_count++];
    *e = (ListEntry){.host = host};
    copy(e->name, sizeof e->name, n > 2 ? w[2] : "");
}

void lists_load(Lists *l, const char *dir, Console *console)
{
    *l = (Lists){.console = console};
    copy(l->dir, sizeof l->dir, dir);
    if (!l->dir[0]) return;
    read_list(l, BANLIST, "ban", take_ban);
    read_list(l, MUTELIST, "mute", take_mute);
    read_list(l, ADMINS, "admin", take_admin);
    make_missing(l);
    if (console && (l->ban_count || l->mute_count || l->admin_count))
        console_print(console, "%d bans, %d mutes and %d admins from %s\n", l->ban_count, l->mute_count, l->admin_count, l->dir);
}

// A name or reason kept as a word in "quotes": a quote in it would end it early.
static void quoted(char *out, size_t size, const char *s)
{
    size_t n = 0;
    for (; *s && n + 1 < size; s++) out[n++] = *s == '"' ? '\'' : *s;
    out[n] = '\0';
}

// An entry's address and hardware ID as its line has them, "-" for either it lacks.
static void who_text(uint32_t host, const char *hwid, char *out, size_t size)
{
    char ip[32] = "-";
    if (host) lists_address_text(host, ip, sizeof ip);
    snprintf(out, size, "%s %s", ip, hwid[0] ? hwid : "-");
}

static FILE *begin_file(const Lists *l, const char *file, const char *header)
{
    char path[600];
    path_of(l, file, path, sizeof path);
    files_make_parents(path);
    FILE *f = fopen(path, "wb");
    if (!f) {
        if (l->console) console_print(l->console, "could not write %s\n", path);
        return NULL;
    }
    fputs(header, f);
    return f;
}

static void save_bans(const Lists *l)
{
    if (!l->dir[0]) return;
    FILE *f = begin_file(l, BANLIST,
                         "// The bans: the address, the hardware ID (- for either the ban doesn't name), the Unix\n"
                         "// time it lifts (0 never), the name it was given, why. A player is kept out by either.\n"
                         "// The server writes this as players are banned and unbanned; edit it with the server stopped.\n\n");
    if (!f) return;
    for (int i = 0; i < l->ban_count; i++) {
        const Ban *b = &l->bans[i];
        char who[64], name[NET_NAME_SIZE], reason[LIST_REASON_SIZE];
        who_text(b->host, b->hwid, who, sizeof who);
        quoted(name, sizeof name, b->name);
        quoted(reason, sizeof reason, b->reason);
        fprintf(f, "ban %s %lld \"%s\" \"%s\"\n", who, (long long)b->expires, name, reason);
    }
    fclose(f);
}

static void save_mutes(const Lists *l)
{
    if (!l->dir[0]) return;
    FILE *f = begin_file(l, MUTELIST,
                         "// The mutes: the address, the hardware ID (- for either the mute doesn't name), and the\n"
                         "// name it was given. Their chat reaches nobody until they are unmuted. The server writes\n"
                         "// this as players are muted and unmuted; edit it with the server stopped.\n\n");
    if (!f) return;
    for (int i = 0; i < l->mute_count; i++) {
        char who[64], name[NET_NAME_SIZE];
        who_text(l->mutes[i].host, l->mutes[i].hwid, who, sizeof who);
        quoted(name, sizeof name, l->mutes[i].name);
        fprintf(f, "mute %s \"%s\"\n", who, name);
    }
    fclose(f);
}

// Each list's file that isn't there, made with its header, so an owner finds them all in
// config/ from the first start: the bans and mutes as the server writes them,
// empty, and admins.txt for the owner to fill.
static void make_missing(const Lists *l)
{
    char path[600];
    path_of(l, BANLIST, path, sizeof path);
    if (!files_exists(path)) save_bans(l);
    path_of(l, MUTELIST, path, sizeof path);
    if (!files_exists(path)) save_mutes(l);
    path_of(l, ADMINS, path, sizeof path);
    if (files_exists(path)) return;
    FILE *f = begin_file(l, ADMINS,
                         "// The admins, by address: they may /kick, /ban, /mute and /map from the chat (and /admins,\n"
                         "// /bans, /mutes to list them). The server only reads this file, as it starts. A player\n"
                         "// may also be an admin until they leave by saying /login with sv_adminpassword.\n"
                         "//\n"
                         "// admin 1.2.3.4 \"Major\"\n");
    if (f) fclose(f);
}

const Ban *lists_banned(Lists *l, uint32_t host, const char *hwid, int64_t now)
{
    for (int i = 0; i < l->ban_count; i++) {
        Ban *b = &l->bans[i];
        if (!names(b->host, b->hwid, host, hwid)) continue;
        if (b->expires == 0 || b->expires > now) return b;
        l->bans[i--] = l->bans[--l->ban_count]; // lifted; another may still name them
        save_bans(l);
    }
    return NULL;
}

void lists_ban(Lists *l, uint32_t host, const char *hwid, int64_t expires, const char *name, const char *reason)
{
    char id[NET_HWID_SIZE];
    hwid_of(id, hwid);
    if (!host && !id[0]) return; // names nobody
    Ban *b = NULL;
    for (int i = 0; i < l->ban_count && !b; i++)
        if (names(l->bans[i].host, l->bans[i].hwid, host, id)) b = &l->bans[i];
    if (!b) {
        if (l->ban_count == MAX_BANS) { // full: the one lifting soonest makes room
            int soonest = 0;
            for (int i = 1; i < l->ban_count; i++)
                if (l->bans[i].expires != 0 && (l->bans[soonest].expires == 0 || l->bans[i].expires < l->bans[soonest].expires)) soonest = i;
            b = &l->bans[soonest];
        } else {
            b = &l->bans[l->ban_count++];
        }
    }
    *b = (Ban){.host = host, .expires = expires};
    copy(b->hwid, sizeof b->hwid, id);
    copy(b->name, sizeof b->name, name);
    copy(b->reason, sizeof b->reason, reason);
    save_bans(l);
}

bool lists_unban(Lists *l, uint32_t host, const char *hwid)
{
    bool any = false;
    for (int i = 0; i < l->ban_count; i++) {
        if (!names(l->bans[i].host, l->bans[i].hwid, host, hwid)) continue;
        l->bans[i--] = l->bans[--l->ban_count];
        any = true;
    }
    if (any) save_bans(l);
    return any;
}

static int find(const ListEntry *list, int count, uint32_t host, const char *hwid)
{
    for (int i = 0; i < count; i++)
        if (names(list[i].host, list[i].hwid, host, hwid)) return i;
    return -1;
}

bool lists_muted(const Lists *l, uint32_t host, const char *hwid) { return find(l->mutes, l->mute_count, host, hwid) >= 0; }

void lists_mute(Lists *l, uint32_t host, const char *hwid, const char *name)
{
    char id[NET_HWID_SIZE];
    hwid_of(id, hwid);
    if (!host && !id[0]) return;
    int i = find(l->mutes, l->mute_count, host, id);
    if (i < 0) {
        if (l->mute_count == MAX_MUTES) return;
        i = l->mute_count++;
    }
    l->mutes[i] = (ListEntry){.host = host};
    copy(l->mutes[i].hwid, sizeof l->mutes[i].hwid, id);
    copy(l->mutes[i].name, sizeof l->mutes[i].name, name);
    save_mutes(l);
}

bool lists_unmute(Lists *l, uint32_t host, const char *hwid)
{
    bool any = false;
    for (int i; (i = find(l->mutes, l->mute_count, host, hwid)) >= 0; any = true) l->mutes[i] = l->mutes[--l->mute_count];
    if (any) save_mutes(l);
    return any;
}

bool lists_admin(const Lists *l, uint32_t host) { return host && find(l->admins, l->admin_count, host, NULL) >= 0; }
