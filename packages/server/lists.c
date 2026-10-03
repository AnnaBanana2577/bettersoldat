#include "lists.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"             // the launcher's, for the directory
#include "network/transport.h" // ENet, for the addresses

#define BANLIST "banlist.txt"
#define MUTELIST "mutelist.txt"
#define ADMINS "admins.txt"
#define LIST_WORDS 6

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

static void take_ban(Lists *l, char **w, int n)
{
    uint32_t host;
    if (n < 3 || l->ban_count == MAX_BANS || !lists_address(w[1], &host)) return;
    Ban *b = &l->bans[l->ban_count++];
    *b = (Ban){.host = host, .expires = strtoll(w[2], NULL, 10)};
    copy(b->name, sizeof b->name, n > 3 ? w[3] : "");
    copy(b->reason, sizeof b->reason, n > 4 ? w[4] : "");
}

static void take_entry(ListEntry *list, int *count, int max, char **w, int n)
{
    uint32_t host;
    if (*count == max || !lists_address(w[1], &host)) return;
    ListEntry *e = &list[(*count)++];
    *e = (ListEntry){.host = host};
    copy(e->name, sizeof e->name, n > 2 ? w[2] : "");
}

static void take_mute(Lists *l, char **w, int n) { take_entry(l->mutes, &l->mute_count, MAX_MUTES, w, n); }
static void take_admin(Lists *l, char **w, int n) { take_entry(l->admins, &l->admin_count, MAX_ADMINS, w, n); }

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
                         "// The bans: the address, the Unix time the ban lifts (0 never), the name it was given, why.\n"
                         "// The server writes this as players are banned and unbanned; edit it with the server stopped.\n\n");
    if (!f) return;
    for (int i = 0; i < l->ban_count; i++) {
        const Ban *b = &l->bans[i];
        char ip[32], name[NET_NAME_SIZE], reason[LIST_REASON_SIZE];
        lists_address_text(b->host, ip, sizeof ip);
        quoted(name, sizeof name, b->name);
        quoted(reason, sizeof reason, b->reason);
        fprintf(f, "ban %s %lld \"%s\" \"%s\"\n", ip, (long long)b->expires, name, reason);
    }
    fclose(f);
}

static void save_mutes(const Lists *l)
{
    if (!l->dir[0]) return;
    FILE *f = begin_file(l, MUTELIST,
                         "// The mutes: the address, and the name it was given. Their chat reaches nobody until they are\n"
                         "// unmuted. The server writes this as players are muted and unmuted; edit it with the server stopped.\n\n");
    if (!f) return;
    for (int i = 0; i < l->mute_count; i++) {
        char ip[32], name[NET_NAME_SIZE];
        lists_address_text(l->mutes[i].host, ip, sizeof ip);
        quoted(name, sizeof name, l->mutes[i].name);
        fprintf(f, "mute %s \"%s\"\n", ip, name);
    }
    fclose(f);
}

// Each list's file that isn't there, made with its header, so an owner finds them all in
// config/server/ from the first start: the bans and mutes as the server writes them,
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

const Ban *lists_banned(Lists *l, uint32_t host, int64_t now)
{
    for (int i = 0; i < l->ban_count; i++) {
        Ban *b = &l->bans[i];
        if (b->host != host) continue;
        if (b->expires == 0 || b->expires > now) return b;
        l->bans[i] = l->bans[--l->ban_count]; // lifted
        save_bans(l);
        return NULL;
    }
    return NULL;
}

void lists_ban(Lists *l, uint32_t host, int64_t expires, const char *name, const char *reason)
{
    Ban *b = NULL;
    for (int i = 0; i < l->ban_count && !b; i++)
        if (l->bans[i].host == host) b = &l->bans[i];
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
    copy(b->name, sizeof b->name, name);
    copy(b->reason, sizeof b->reason, reason);
    save_bans(l);
}

bool lists_unban(Lists *l, uint32_t host)
{
    for (int i = 0; i < l->ban_count; i++) {
        if (l->bans[i].host != host) continue;
        l->bans[i] = l->bans[--l->ban_count];
        save_bans(l);
        return true;
    }
    return false;
}

static int find(const ListEntry *list, int count, uint32_t host)
{
    for (int i = 0; i < count; i++)
        if (list[i].host == host) return i;
    return -1;
}

bool lists_muted(const Lists *l, uint32_t host) { return find(l->mutes, l->mute_count, host) >= 0; }

void lists_mute(Lists *l, uint32_t host, const char *name)
{
    int i = find(l->mutes, l->mute_count, host);
    if (i < 0) {
        if (l->mute_count == MAX_MUTES) return;
        i = l->mute_count++;
    }
    l->mutes[i] = (ListEntry){.host = host};
    copy(l->mutes[i].name, sizeof l->mutes[i].name, name);
    save_mutes(l);
}

bool lists_unmute(Lists *l, uint32_t host)
{
    int i = find(l->mutes, l->mute_count, host);
    if (i < 0) return false;
    l->mutes[i] = l->mutes[--l->mute_count];
    save_mutes(l);
    return true;
}

bool lists_admin(const Lists *l, uint32_t host) { return find(l->admins, l->admin_count, host) >= 0; }
