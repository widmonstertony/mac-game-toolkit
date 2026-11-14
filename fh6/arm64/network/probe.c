/* SPDX-License-Identifier: MIT
 * Local-only Windows API regression: dual-stack datagrams and upgraded connections.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static LONG failures;
static INTERNET_PORT http_port;
static volatile LONG worker_stage[6];
#define CHECK(expr, label) do { if (!(expr)) { \
    printf("FAIL %s (error %lu)\n", label, GetLastError()); \
    InterlockedIncrement(&failures); } else printf("PASS %s\n", label); } while (0)

static void udp_test(void)
{
    SOCKET dual = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    SOCKET peer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in6 bind6 = {0}, destination = {0};
    struct sockaddr_in bind4 = {0}, source = {0};
    DWORD zero = 0, one = 1, ttl = 255, wait = 2000;
    int length = sizeof(bind4), source_length = sizeof(source), value, value_size;
    char buffer[32];
    CHECK(dual != INVALID_SOCKET && peer != INVALID_SOCKET, "UDP sockets created");
    CHECK(!setsockopt(dual, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&zero, sizeof(zero)), "UDP dual-stack enabled");
    bind6.sin6_family = AF_INET6;
    CHECK(!bind(dual, (struct sockaddr *)&bind6, sizeof(bind6)), "UDP dual-stack bind");
    CHECK(!setsockopt(dual, IPPROTO_IP, IP_DONTFRAGMENT, (char *)&one, sizeof(one)), "IPv4 DONTFRAGMENT on dual-stack socket");
    CHECK(!setsockopt(dual, IPPROTO_IP, IP_TTL, (char *)&ttl, sizeof(ttl)), "IPv4 TTL on dual-stack socket");
    value = -1; value_size = sizeof(value);
    CHECK(!getsockopt(dual, IPPROTO_IPV6, IPV6_UNICAST_HOPS, (char *)&value, &value_size) && value == 255,
          "mapped unicast hops actually applied");
    ttl = 999;
    CHECK(setsockopt(dual, IPPROTO_IP, IP_TTL, (char *)&ttl, sizeof(ttl)) == SOCKET_ERROR,
          "invalid TTL still fails");
    CHECK(setsockopt(INVALID_SOCKET, IPPROTO_IP, IP_TTL, (char *)&one, sizeof(one)) == SOCKET_ERROR,
          "invalid socket still fails");
    bind4.sin_family = AF_INET;
    bind4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(!bind(peer, (struct sockaddr *)&bind4, sizeof(bind4)) &&
          !getsockname(peer, (struct sockaddr *)&bind4, &length), "IPv4 loopback peer bound");
    destination.sin6_family = AF_INET6;
    destination.sin6_port = bind4.sin_port;
    destination.sin6_addr.s6_addr[10] = 0xff;
    destination.sin6_addr.s6_addr[11] = 0xff;
    memcpy(&destination.sin6_addr.s6_addr[12], &bind4.sin_addr, 4);
    setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, (char *)&wait, sizeof(wait));
    setsockopt(dual, SOL_SOCKET, SO_RCVTIMEO, (char *)&wait, sizeof(wait));
    CHECK(sendto(dual, "fh6-net", 7, 0, (struct sockaddr *)&destination, sizeof(destination)) == 7,
          "dual-stack send to mapped IPv4 peer");
    int received = recvfrom(peer, buffer, sizeof(buffer), 0, (struct sockaddr *)&source, &source_length);
    CHECK(received == 7 && !memcmp(buffer, "fh6-net", 7), "IPv4 peer receives real datagram");
    if (received == 7) {
        CHECK(sendto(peer, buffer, received, 0, (struct sockaddr *)&source, source_length) == received,
              "IPv4 peer sends response");
        received = recv(dual, buffer, sizeof(buffer), 0);
        CHECK(received == 7 && !memcmp(buffer, "fh6-net", 7), "dual-stack receives real response");
    }
    closesocket(peer);
    closesocket(dual);
}

static DWORD WINAPI websocket_test(void *unused)
{
    HINTERNET session = NULL, connection = NULL, request = NULL, socket = NULL, health = NULL;
    DWORD size, error;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
    char buffer[128];
    int worker = (int)(UINT_PTR)unused;
    worker_stage[worker] = 1;
    session = WinHttpOpen(L"FH6 local regression", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    if (!session) goto failed;
    WinHttpSetTimeouts(session, 2000, 2000, 2000, 3000);
    connection = WinHttpConnect(session, L"127.0.0.1", http_port, 0);
    if (!connection) goto failed;
    request = WinHttpOpenRequest(connection, L"GET", L"/ws", NULL, NULL, NULL, 0);
    if (!request || !WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0) ||
        !WinHttpSendRequest(request, NULL, 0, NULL, 0, 0, 0) || !WinHttpReceiveResponse(request, NULL)) goto failed;
    socket = WinHttpWebSocketCompleteUpgrade(request, 0);
    if (!socket) goto failed;
    worker_stage[worker] = 2;
    /* Drain the HTTP response after upgrade. It must not pool the live WebSocket. */
    size = 123;
    WinHttpReadData(request, buffer, 0, &size);
    worker_stage[worker] = 3;
    health = WinHttpOpenRequest(connection, L"GET", L"/health", NULL, NULL, NULL, 0);
    if (!health || !WinHttpSendRequest(health, NULL, 0, NULL, 0, 0, 0) ||
        !WinHttpReceiveResponse(health, NULL) || !WinHttpReadData(health, buffer, sizeof(buffer), &size) ||
        size != 2 || memcmp(buffer, "ok", 2)) goto failed;
    worker_stage[worker] = 4;
    WinHttpCloseHandle(request); request = NULL;
    error = WinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, "echo", 4);
    if (error) { SetLastError(error); goto failed; }
    worker_stage[worker] = 5;
    size = 0;
    error = WinHttpWebSocketReceive(socket, buffer, sizeof(buffer), &size, &type);
    if (error || size != 4 || memcmp(buffer, "echo", 4)) { SetLastError(error); goto failed; }
    worker_stage[worker] = 6;
    CHECK(1, "WebSocket ownership + concurrent HTTP + delayed echo");
    WinHttpWebSocketClose(socket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
    worker_stage[worker] = 7;
    goto done;
failed:
    CHECK(0, "WebSocket ownership + concurrent HTTP + delayed echo");
done:
    if (health) WinHttpCloseHandle(health);
    if (socket) WinHttpCloseHandle(socket);
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    worker_stage[worker] = 8;
    return 0;
}

static void timeout_test(void)
{
    HINTERNET session = WinHttpOpen(L"FH6 timeout regression", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    CHECK(session != NULL, "timeout session created");
    if (!session) return;
    WinHttpSetTimeouts(session, 1000, 1000, 1000, 150);
    HINTERNET connection = WinHttpConnect(session, L"127.0.0.1", http_port, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", L"/slow", NULL, NULL, NULL, 0) : NULL;
    DWORD response_timeout = 150;
    CHECK(request && WinHttpSetOption(request, WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT,
                                     &response_timeout, sizeof(response_timeout)),
          "HTTP response-header timeout configured");
    ULONGLONG start = GetTickCount64();
    BOOL sent = request && WinHttpSendRequest(request, NULL, 0, NULL, 0, 0, 0);
    BOOL received = sent && WinHttpReceiveResponse(request, NULL);
    DWORD error = GetLastError();
    ULONGLONG elapsed = GetTickCount64() - start;
    /* Wine 11.18 propagates Winsock's timeout on this response-header path. */
    printf("INFO timeout error=%lu elapsed=%llu ms\n", error, elapsed);
    CHECK(sent && !received && (error == ERROR_WINHTTP_TIMEOUT || error == WSAETIMEDOUT) &&
          elapsed >= 75 && elapsed < 2500,
          "WinHTTP receive timeout preserved");
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
}

int main(int argc, char **argv)
{
    WSADATA data;
    HANDLE workers[6];
    if (argc != 2) return 2;
    setvbuf(stdout, NULL, _IONBF, 0);
    http_port = (INTERNET_PORT)atoi(argv[1]);
    CHECK(!WSAStartup(MAKEWORD(2, 2), &data), "WSAStartup");
    udp_test();
    if (!strcmp(argv[1], "--udp-only")) {
        WSACleanup();
        printf("RESULT failures=%ld\n", failures);
        return failures ? 1 : 0;
    }
    timeout_test();
    for (int i = 0; i < 6; ++i) workers[i] = CreateThread(NULL, 0, websocket_test, (void *)(UINT_PTR)i, 0, NULL);
    DWORD wait = WaitForMultipleObjects(6, workers, TRUE, 15000);
    printf("INFO thread wait result=%lu\n", wait);
    for (int i = 0; i < 6; ++i) printf("INFO worker %d stage %ld\n", i, worker_stage[i]);
    CHECK(wait == WAIT_OBJECT_0, "six WebSocket workers complete without hang");
    for (int i = 0; i < 6; ++i) CloseHandle(workers[i]);
    WSACleanup();
    printf("RESULT failures=%ld\n", failures);
    return failures ? 1 : 0;
}
