// The query: its bytes both ways and what a reader refuses, then a host on the loopback
// answering one from a bare socket, saying nothing to a request too short to pad, and
// still taking a client's connection beside them.

#include <stdio.h>
#include <string.h>

#include "host.h"
#include "test.h"

#define PORT 40041
#define ROUNDS 300 // of 10 ms: three seconds at most for anything to arrive

static void bytes_both_ways(void)
{
    uint8_t request[QUERY_REQUEST_SIZE + 8];
    size_t size = query_write_request(request, sizeof request, 0xdeadbeef);
    uint32_t nonce = 0;
    CHECK(size == QUERY_REQUEST_SIZE && query_read_request(request, size, &nonce) && nonce == 0xdeadbeef,
          "a request reads back its nonce");
    CHECK(!query_read_request(request, 12, &nonce), "a request without its padding is refused");
    CHECK(query_write_request(request, QUERY_REQUEST_SIZE - 1, 1) == 0, "a request needs its room");

    ServerInfo info = {.protocol = NET_VERSION, .players = 3, .bots = 2, .max_players = MAX_PLAYERS, .mode = MATCH_CTF,
                       .password = true, .hostname = "Ye Olde Server", .map = "ctf_Ash"};
    uint8_t reply[QUERY_REPLY_MAX];
    size = query_write_reply(reply, sizeof reply, 77, &info);
    ServerInfo got;
    CHECK(size > 0 && size <= QUERY_REPLY_MAX, "a reply fits its room (%zu bytes)", size);
    CHECK(query_read_reply(reply, size, 77, &got) && memcmp(&got, &info, sizeof got) == 0, "a reply reads back the same");
    CHECK(!query_read_reply(reply, size, 78, &got), "a reply to another request is refused");
    CHECK(!query_read_reply(reply, size - 1, 77, &got), "a cut reply is refused");
    CHECK(!query_read_request(reply, size, &nonce), "a reply is not a request");

    // the lobby's test holds the same bytes (internal/query/query_test.go): the two read one layout
    static const uint8_t golden[] = {0xFF, 0xFF, 0xFF, 0xFF, 'B', 'S', 'R', 'i', 77, 0, 0, 0, NET_VERSION, 0, 3, 2, 32, 1, 1,
                                     14, 'Y', 'e', ' ', 'O', 'l', 'd', 'e', ' ', 'S', 'e', 'r', 'v', 'e', 'r',
                                     7, 'c', 't', 'f', '_', 'A', 's', 'h'};
    CHECK(size == sizeof golden && memcmp(reply, golden, size) == 0, "a reply is laid out as the lobby reads it");

    memset(info.hostname, 'x', sizeof info.hostname - 1);
    memset(info.map, 'm', sizeof info.map - 1);
    size = query_write_reply(reply, sizeof reply, 1, &info);
    CHECK(size == QUERY_REPLY_MAX, "the longest reply is QUERY_REPLY_MAX");
    reply[19] = NET_NAME_SIZE; // a name longer than the reader's room
    CHECK(!query_read_reply(reply, size, 1, &got), "a name too long is refused");

    CHECK(!query_is_query((const uint8_t *)"\xff\xff\xff", 3), "three bytes are nobody's query");
}

// Sends `size` bytes of a request to the host and pumps it until a reply comes back or
// the rounds run out: the reply's size, or 0.
static size_t ask(Host *h, ENetSocket s, const uint8_t *request, size_t size, uint8_t *reply, size_t room)
{
    ENetAddress to = {.port = PORT};
    enet_address_set_host(&to, "127.0.0.1");
    ENetBuffer out = {.data = (void *)request, .dataLength = size};
    enet_socket_send(s, &to, &out, 1);
    for (int i = 0; i < ROUNDS / 10; i++) {
        host_pump(h, 0);
        ENetAddress from;
        ENetBuffer in = {.data = reply, .dataLength = room};
        int got = enet_socket_receive(s, &from, &in, 1);
        if (got > 0) return (size_t)got;
        enet_host_service(h->link.host, NULL, 10);
    }
    return 0;
}

static void a_host_answers(void)
{
    Host host;
    HostSettings settings = {.port = PORT, .mode = MATCH_CTF, .hostname = "query test"};
    snprintf(settings.data, sizeof settings.data, "%s", TEST_DATA);
    snprintf(settings.map, sizeof settings.map, "ctf_Ash");
    if (!host_open(&host, NULL, &settings)) {
        CHECK(false, "a host on port %d to query", PORT);
        return;
    }
    host_add_bot(&host, TEAM_ALPHA, NULL);
    connections_set_password(&host.connections, "hush");

    ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    CHECK(s != ENET_SOCKET_NULL, "a bare socket");
    enet_socket_set_option(s, ENET_SOCKOPT_NONBLOCK, 1);

    uint8_t request[QUERY_REQUEST_SIZE], reply[NET_MTU];
    query_write_request(request, sizeof request, 4242);
    size_t size = ask(&host, s, request, sizeof request, reply, sizeof reply);
    ServerInfo info;
    CHECK(query_read_reply(reply, size, 4242, &info), "the host answers a query (%zu bytes)", size);
    CHECK(info.protocol == NET_VERSION && info.max_players == MAX_PLAYERS && info.mode == MATCH_CTF,
          "with its version, room and mode");
    CHECK(strcmp(info.hostname, "query test") == 0 && strcmp(info.map, "ctf_Ash") == 0, "its name and map (%s, %s)",
          info.hostname, info.map);
    CHECK(info.players == 0 && info.bots == 1 && info.password, "a bot, nobody else, and a password");

    CHECK(ask(&host, s, request, 12, reply, sizeof reply) == 0, "a request without its padding gets nothing");

    NetLink client;
    bool connected = false;
    if (net_connect(&client, "127.0.0.1", PORT)) {
        for (int i = 0; i < ROUNDS && !connected; i++) {
            host_pump(&host, 0);
            NetEvent e;
            while (net_poll(&client, &e, 0) != NET_EVENT_NONE) connected |= e.kind == NET_EVENT_CONNECT;
            enet_host_service(host.link.host, NULL, 10);
        }
        net_close(&client);
    }
    CHECK(connected, "and a client still connects beside the queries");

    enet_socket_destroy(s);
    host_close(&host);
}

// The lobby's servers.txt, as the browser reads it.
static void the_list(void)
{
    QueryAddress list[4];
    int n = query_parse_list("203.0.113.5:23073\n198.51.100.7:1\r\n", list, 4);
    CHECK(n == 2 && strcmp(list[0].ip, "203.0.113.5") == 0 && list[0].port == 23073 && strcmp(list[1].ip, "198.51.100.7") == 0 &&
              list[1].port == 1,
          "a list of two, either line ending (%d)", n);
    n = query_parse_list("\n# a comment\n1.2.3.4\n1.2.3.4:\n1.2.3.4:0\n1.2.3.4:65536\n256.1.1.1:5\n[::1]:5\nx:5\n9.9.9.9:7 \n"
                         "8.8.8.8:8",
                         list, 4);
    CHECK(n == 1 && strcmp(list[0].ip, "8.8.8.8") == 0 && list[0].port == 8, "only the good line, unterminated (%d)", n);
    CHECK(query_parse_list("1.1.1.1:1\n2.2.2.2:2\n3.3.3.3:3\n", list, 2) == 2, "no more than there is room for");
    CHECK(query_parse_list(NULL, list, 4) == 0 && query_parse_list("", list, 4) == 0, "nothing is no servers");
}

void query_tests(void)
{
    bytes_both_ways();
    the_list();
    CHECK(net_init(), "ENet starts");
    a_host_answers();
    net_shutdown();
}
