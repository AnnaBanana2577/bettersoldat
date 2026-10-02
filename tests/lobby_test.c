// The heartbeat's pieces: the lobby's address, the JSON said to it, and what is read
// from its answers. The requests themselves need a lobby, and are tried against one by
// hand (the soldatreloaded-lobby repository's README).

#include <string.h>

#include "lobby.h"
#include "test.h"

void lobby_tests(void)
{
    char url[128];
    CHECK(lobby_url(url, sizeof url, "https://lobby.example") && strcmp(url, "https://lobby.example/v1/servers") == 0,
          "the servers' path on the lobby (%s)", url);
    CHECK(lobby_url(url, sizeof url, "https://lobby.example//") && strcmp(url, "https://lobby.example/v1/servers") == 0,
          "trailing slashes or not");
    CHECK(!lobby_url(url, sizeof url, "") && !lobby_url(url, 8, "https://lobby.example"), "no lobby, or no room, is refused");

    char body[96];
    CHECK(lobby_body(body, sizeof body, 23073, "") && strcmp(body, "{\"port\":23073}") == 0, "a heartbeat says its port (%s)",
          body);
    CHECK(lobby_body(body, sizeof body, 23073, "213.188.216.246") &&
              strcmp(body, "{\"port\":23073,\"address\":\"213.188.216.246\"}") == 0,
          "and the address it names (%s)", body);
    const char *bad[] = {"1.2.3", "1.2.3.4.5", "256.1.1.1", "1.2.3.4 ", "a.b.c.d", "1..2.3", "0001.2.3.4", "\"},{\"x\":\"1"};
    for (int i = 0; i < (int)(sizeof bad / sizeof bad[0]); i++)
        CHECK(!lobby_body(body, sizeof body, 1, bad[i]), "'%s' is not an IPv4 address", bad[i]);

    const char *answer = "{\"address\":\"203.0.113.5\",\"port\":23073,\"heartbeat_seconds\":30}\n";
    char as[64];
    CHECK(lobby_interval(answer) == 30, "the interval the lobby asks for");
    CHECK(lobby_listed_as(answer, as, sizeof as) && strcmp(as, "203.0.113.5:23073") == 0, "and what it lists (%s)", as);
    CHECK(lobby_interval("{ \"heartbeat_seconds\" : 45 }") == 45, "spaces about the colon");
    CHECK(lobby_interval("the lobby could not reach 1.2.3.4:23073") == 0 && !lobby_listed_as("", as, sizeof as) &&
              lobby_interval(NULL) == 0,
          "an answer that isn't one says nothing");
}
