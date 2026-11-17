/*
 * chat_server.c - Multi-client TCP chat server built on poll().
 *
 * A single process and a single thread serve every client: poll() tells
 * which sockets are readable, and each client has its own line buffer to
 * cope with partial reads.
 *
 * Protocol (text, one message per line):
 *   first line   -> nickname
 *   /who         -> list connected users
 *   /quit        -> leave
 *   anything else is broadcast as "[nick] message"
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_CLIENTS  64
#define LINE_MAX_LEN 512
#define NICK_MAX_LEN 20

typedef struct {
    int    fd;                  /* -1 when the slot is free */
    char   nick[NICK_MAX_LEN + 1];
    char   buf[LINE_MAX_LEN];
    size_t len;
    char   addr[INET_ADDRSTRLEN + 6];
} client_t;

static client_t clients[MAX_CLIENTS];

static void send_text(int fd, const char *text)
{
    size_t len = strlen(text);

    while (len > 0) {
        ssize_t n = send(fd, text, len, MSG_NOSIGNAL);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return; /* the disconnection will be detected by poll() */
        }
        text += n;
        len -= (size_t)n;
    }
}

static void sendf(int fd, const char *fmt, ...)
{
    char msg[LINE_MAX_LEN + 64];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    send_text(fd, msg);
}

/* Sends a message to every named client except `except`. */
static void broadcast(const client_t *except, const char *fmt, ...)
{
    char msg[LINE_MAX_LEN + 64];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].fd >= 0 && clients[i].nick[0] != '\0' && &clients[i] != except)
            send_text(clients[i].fd, msg);
}

static void disconnect(client_t *c)
{
    printf("%s disconnected%s%s\n", c->addr, c->nick[0] ? " : " : "", c->nick);
    if (c->nick[0] != '\0')
        broadcast(c, "* %s left the chat\n", c->nick);
    close(c->fd);
    c->fd = -1;
    c->nick[0] = '\0';
    c->len = 0;
}

static int nick_is_valid(const char *nick)
{
    size_t len = strlen(nick);

    if (len == 0 || len > NICK_MAX_LEN)
        return 0;
    for (const char *p = nick; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-')
            return 0;
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].fd >= 0 && strcmp(clients[i].nick, nick) == 0)
            return 0;
    return 1;
}

static void list_users(const client_t *c)
{
    char msg[LINE_MAX_LEN + 64] = "Online:";
    size_t len = strlen(msg);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd < 0 || clients[i].nick[0] == '\0')
            continue;
        if (len + strlen(clients[i].nick) + 3 >= sizeof msg)
            break;
        len += (size_t)snprintf(msg + len, sizeof msg - len, " %s", clients[i].nick);
    }
    snprintf(msg + len, sizeof msg - len, "\n");
    send_text(c->fd, msg);
}

/* Returns 0 to keep the client, -1 to disconnect it. */
static int handle_line(client_t *c, const char *line)
{
    if (c->nick[0] == '\0') {
        if (!nick_is_valid(line)) {
            send_text(c->fd, "Invalid or already used nickname (letters, digits, _ and -, max 20). Try again:\n");
            return 0;
        }
        strcpy(c->nick, line);
        printf("%s is now known as %s\n", c->addr, c->nick);
        sendf(c->fd, "Hello %s! Commands: /who, /quit\n", c->nick);
        broadcast(c, "* %s joined the chat\n", c->nick);
        return 0;
    }
    if (line[0] == '\0')
        return 0;
    if (strcmp(line, "/quit") == 0) {
        send_text(c->fd, "Bye!\n");
        return -1;
    }
    if (strcmp(line, "/who") == 0) {
        list_users(c);
        return 0;
    }
    if (line[0] == '/') {
        send_text(c->fd, "Unknown command. Commands: /who, /quit\n");
        return 0;
    }
    broadcast(c, "[%s] %s\n", c->nick, line);
    return 0;
}

/* Reads available data and processes every complete line. */
static void handle_readable(client_t *c)
{
    ssize_t n = recv(c->fd, c->buf + c->len, sizeof c->buf - c->len, 0);
    char *start, *nl;

    if (n <= 0) {
        if (n < 0 && errno == EINTR)
            return;
        disconnect(c);
        return;
    }
    c->len += (size_t)n;

    start = c->buf;
    while ((nl = memchr(start, '\n', c->len - (size_t)(start - c->buf))) != NULL) {
        *nl = '\0';
        if (nl > start && nl[-1] == '\r')
            nl[-1] = '\0';
        if (handle_line(c, start) < 0) {
            disconnect(c);
            return;
        }
        start = nl + 1;
    }
    c->len -= (size_t)(start - c->buf);
    memmove(c->buf, start, c->len);

    if (c->len == sizeof c->buf) { /* line too long: process it as is */
        c->buf[c->len - 1] = '\0';
        c->len = 0;
        if (handle_line(c, c->buf) < 0)
            disconnect(c);
    }
}

static void accept_client(int listener)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof addr;
    char ip[INET_ADDRSTRLEN];
    int fd = accept(listener, (struct sockaddr *)&addr, &len);

    if (fd < 0) {
        perror("accept");
        return;
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd < 0) {
            client_t *c = &clients[i];

            c->fd = fd;
            c->nick[0] = '\0';
            c->len = 0;
            inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof ip);
            snprintf(c->addr, sizeof c->addr, "%s:%u", ip, (unsigned)ntohs(addr.sin_port));
            printf("%s connected\n", c->addr);
            send_text(fd, "Welcome to the chat! Choose a nickname:\n");
            return;
        }
    }
    send_text(fd, "Server full, try again later.\n");
    close(fd);
}

int main(int argc, char **argv)
{
    unsigned long port = 5000;
    struct sockaddr_in addr;
    struct pollfd pfds[MAX_CLIENTS + 1];
    int listener, opt, one = 1;

    while ((opt = getopt(argc, argv, "p:h")) != -1) {
        char *end;

        switch (opt) {
        case 'p':
            port = strtoul(optarg, &end, 10);
            if (*optarg == '\0' || *end != '\0' || port == 0 || port > 65535) {
                fprintf(stderr, "Invalid port: %s\n", optarg);
                return EXIT_FAILURE;
            }
            break;
        default:
            fprintf(stderr, "Usage: %s [-p port]\n", argv[0]);
            return opt == 'h' ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }

    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (int i = 0; i < MAX_CLIENTS; i++)
        clients[i].fd = -1;

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(listener, (struct sockaddr *)&addr, sizeof addr) < 0 || listen(listener, 16) < 0) {
        perror("bind/listen");
        return EXIT_FAILURE;
    }
    printf("Chat server listening on TCP port %lu\n", port);

    for (;;) {
        int map[MAX_CLIENTS + 1]; /* pollfd index -> client index */
        nfds_t n = 0;

        pfds[n].fd = listener;
        pfds[n].events = POLLIN;
        map[n++] = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd >= 0) {
                pfds[n].fd = clients[i].fd;
                pfds[n].events = POLLIN;
                map[n++] = i;
            }
        }

        if (poll(pfds, n, -1) < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            return EXIT_FAILURE;
        }
        for (nfds_t k = 0; k < n; k++) {
            if (pfds[k].revents == 0)
                continue;
            if (map[k] < 0)
                accept_client(listener);
            else if (clients[map[k]].fd == pfds[k].fd)
                handle_readable(&clients[map[k]]);
        }
    }
}
