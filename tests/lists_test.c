// The server's lists (server/lists.h): bans and mutes by address and hardware ID, admins
// by address, kept in their
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
    lists_ban(&l, a, NULL, 0, "Major", "Cheating \"a lot\"");
    lists_ban(&l, b, NULL, now + 600, "Minor", "Spam");
    lists_mute(&l, b, NULL, "Minor");
    CHECK(lists_banned(&l, a, NULL, now) && lists_banned(&l, b, NULL, now) && !lists_banned(&l, c, NULL, now), "banned are the two banned");
    CHECK(lists_muted(&l, b, NULL) && !lists_muted(&l, a, NULL), "and muted the one muted");

    lists_load(&again, DIR, NULL);
    const Ban *major = lists_banned(&again, a, NULL, now);
    CHECK(again.ban_count == 2 && again.mute_count == 1 && major && major->expires == 0 && strcmp(major->name, "Major") == 0 &&
              strcmp(major->reason, "Cheating 'a lot'") == 0 && lists_muted(&again, b, NULL),
          "the files read back the same lists (%d bans, %d mutes; a quote in a reason kept as an apostrophe)", again.ban_count,
          again.mute_count);
    CHECK(!lists_banned(&again, b, NULL, now + 601) && again.ban_count == 1, "a ban lifts at its time, and is dropped");
    CHECK(lists_unban(&again, a, NULL) && !lists_banned(&again, a, NULL, now) && !lists_unban(&again, a, NULL), "and unban lifts one for ever");
    CHECK(lists_unmute(&again, b, NULL) && !lists_muted(&again, b, NULL), "unmute takes the mute off");
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
    const Ban *ban = lists_banned(&conns.lists, nine, NULL, (int64_t)time(NULL));
    CHECK(ban && ban->expires - (int64_t)time(NULL) > 29 * 60 && strcmp(ban->reason, "bad") == 0, "for thirty minutes, with its reason");
    CHECK(connections_admin(&conns, NULL, -1, "unban 9.9.9.9") && !lists_banned(&conns.lists, nine, NULL, (int64_t)time(NULL)), "and unban lifts it");
    CHECK(!connections_admin(&conns, NULL, -1, "votemap ctf_Ash"), "a player's command isn't an admin's");

    // by the machine: a ban or mute on a hardware ID holds at any address
    char id[NET_HWID_SIZE];
    CHECK(lists_hwid("0a1b2c3d4e5", id) && strcmp(id, "0A1B2C3D4E5") == 0 && !lists_hwid("0A1B2C3D4E", id) &&
              !lists_hwid("0A1B2C3D4EZ", id),
          "a hardware ID is eleven hex digits, kept in capitals");
    CHECK(connections_admin(&conns, NULL, -1, "banhw 0a1b2c3d4e5 cheating") &&
              lists_banned(&conns.lists, b, "0A1B2C3D4E5", (int64_t)time(NULL)) &&
              !lists_banned(&conns.lists, b, "FFFFFFFFFFF", (int64_t)time(NULL)) && !lists_banned(&conns.lists, b, NULL, (int64_t)time(NULL)),
          "banhw bars the machine from any address, and no other");
    CHECK(connections_admin(&conns, NULL, -1, "banhw 1.2.3.4") && conns.lists.ban_count == 1, "and refuses what isn't a hardware ID");
    lists_ban(&conns.lists, a, "BBBBBBBBBBB", 0, "Both", "both");
    CHECK(lists_banned(&conns.lists, a, NULL, now) && lists_banned(&conns.lists, c, "BBBBBBBBBBB", now) && conns.lists.ban_count == 2,
          "a player's ban holds by their address and by their machine");
    lists_mute(&conns.lists, 0, "CCCCCCCCCCC", "Quiet");
    CHECK(lists_muted(&conns.lists, c, "CCCCCCCCCCC") && !lists_muted(&conns.lists, c, NULL), "a mute by machine alone holds by it");

    static Lists read;
    lists_load(&read, DIR, NULL);
    CHECK(read.ban_count == 2 && lists_banned(&read, 0, "0A1B2C3D4E5", now) && lists_banned(&read, a, NULL, now) &&
              lists_banned(&read, 0, "BBBBBBBBBBB", now) && lists_muted(&read, 0, "CCCCCCCCCCC") && !lists_muted(&read, c, NULL),
          "the files keep the hardware IDs, - for an address a ban doesn't name");
    CHECK(connections_admin(&conns, NULL, -1, "unban Both") && !lists_banned(&conns.lists, a, NULL, now) &&
              !lists_banned(&conns.lists, 0, "BBBBBBBBBBB", now),
          "unban by the name lifts it by address and machine both");
    CHECK(connections_admin(&conns, NULL, -1, "unban 0A1B2C3D4E5") && conns.lists.ban_count == 0, "and by the hardware ID");
    CHECK(connections_admin(&conns, NULL, -1, "unmute ccccccccccc") && conns.lists.mute_count == 0, "unmute by the hardware ID");
    connections_free(&conns);

    // lines from before hardware IDs read as by address
    files_make_parents(DIR "/old/banlist.txt");
    files_write(DIR "/old/banlist.txt", "ban 1.2.3.4 0 \"Major\" \"Cheating\"\nban 5.6.7.8 9999999999 \"Minor\"\n", 64);
    files_write(DIR "/old/mutelist.txt", "mute 1.2.3.4 \"Major\"\n", 21);
    static Lists old;
    lists_load(&old, DIR "/old", NULL);
    const Ban *was = lists_banned(&old, a, NULL, now);
    CHECK(old.ban_count == 2 && was && was->expires == 0 && strcmp(was->name, "Major") == 0 && strcmp(was->reason, "Cheating") == 0 &&
              !was->hwid[0] && lists_banned(&old, b, NULL, now) && lists_muted(&old, a, NULL) && strcmp(old.mutes[0].name, "Major") == 0,
          "an old banlist.txt and mutelist.txt read the same, by address (%d bans)", old.ban_count);
    files_remove_tree(DIR);
}
