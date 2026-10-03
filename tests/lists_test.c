// The server's lists (server/lists.h): bans, mutes and admins by address, kept in their
// files and read back the same; a ban lifts at its time; the console's admin commands
// ban and unban an address.

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "connections.h"
#include "files.h"
#include "test.h"

#define DIR "build/lists_test"

void lists_tests(void)
{
    files_remove_tree(DIR);
    files_make_parents(DIR "/admins.txt");
    files_write(DIR "/admins.txt", "// the owner's\nadmin 10.0.0.7 \"Boss\"\n", 38);

    uint32_t a, b, c;
    CHECK(lists_address("1.2.3.4", &a) && lists_address("5.6.7.8", &b) && lists_address("10.0.0.7", &c) && !lists_address("1.2.3", &a) &&
              !lists_address("one.two", &a) && lists_address("1.2.3.4", &a),
          "addresses read as four numbers, nothing else");
    char text[32];
    lists_address_text(a, text, sizeof text);
    CHECK(strcmp(text, "1.2.3.4") == 0, "and write back the same (%s)", text);

    static Lists l, again, fresh;
    lists_load(&l, DIR, NULL);
    CHECK(l.admin_count == 1 && lists_admin(&l, c) && !lists_admin(&l, a), "admins.txt is read: the owner's admin, by address");
    CHECK(files_exists(DIR "/banlist.txt") && files_exists(DIR "/mutelist.txt"),
          "the lists that weren't there are made, so an owner finds them");

    // a first start: every list made, the owner's admins.txt a template that lists nobody
    files_remove_tree(DIR "/fresh");
    lists_load(&fresh, DIR "/fresh", NULL);
    CHECK(files_exists(DIR "/fresh/banlist.txt") && files_exists(DIR "/fresh/mutelist.txt") &&
              files_exists(DIR "/fresh/admins.txt"),
          "a first start makes all three lists");
    lists_load(&fresh, DIR "/fresh", NULL);
    CHECK(fresh.ban_count == 0 && fresh.mute_count == 0 && fresh.admin_count == 0, "and read back, they list nobody");
    int64_t now = (int64_t)time(NULL);
    lists_ban(&l, a, 0, "Major", "Cheating \"a lot\"");
    lists_ban(&l, b, now + 600, "Minor", "Spam");
    lists_mute(&l, b, "Minor");
    CHECK(lists_banned(&l, a, now) && lists_banned(&l, b, now) && !lists_banned(&l, c, now), "banned are the two banned");
    CHECK(lists_muted(&l, b) && !lists_muted(&l, a), "and muted the one muted");

    lists_load(&again, DIR, NULL);
    const Ban *major = lists_banned(&again, a, now);
    CHECK(again.ban_count == 2 && again.mute_count == 1 && major && major->expires == 0 && strcmp(major->name, "Major") == 0 &&
              strcmp(major->reason, "Cheating 'a lot'") == 0 && lists_muted(&again, b),
          "the files read back the same lists (%d bans, %d mutes; a quote in a reason kept as an apostrophe)", again.ban_count,
          again.mute_count);
    CHECK(!lists_banned(&again, b, now + 601) && again.ban_count == 1, "a ban lifts at its time, and is dropped");
    CHECK(lists_unban(&again, a) && !lists_banned(&again, a, now) && !lists_unban(&again, a), "and unban lifts one for ever");
    CHECK(lists_unmute(&again, b) && !lists_muted(&again, b), "unmute takes the mute off");
    lists_load(&l, DIR, NULL);
    CHECK(l.ban_count == 0 && l.mute_count == 0 && l.admin_count == 1, "and the files say so after (admins untouched)");

    // the console's admin commands, against no players
    static Connections conns;
    NetLink link = {0};
    connections_init(&conns, &link, NULL, "ctf_Ash");
    lists_load(&conns.lists, DIR, NULL);
    CHECK(connections_admin(&conns, NULL, -1, "banip 9.9.9.9 30 bad"), "banip is an admin command");
    uint32_t nine;
    lists_address("9.9.9.9", &nine);
    const Ban *ban = lists_banned(&conns.lists, nine, (int64_t)time(NULL));
    CHECK(ban && ban->expires - (int64_t)time(NULL) > 29 * 60 && strcmp(ban->reason, "bad") == 0, "for thirty minutes, with its reason");
    CHECK(connections_admin(&conns, NULL, -1, "unban 9.9.9.9") && !lists_banned(&conns.lists, nine, (int64_t)time(NULL)), "and unban lifts it");
    CHECK(!connections_admin(&conns, NULL, -1, "votemap ctf_Ash"), "a player's command isn't an admin's");
    connections_free(&conns);
    files_remove_tree(DIR);
}
