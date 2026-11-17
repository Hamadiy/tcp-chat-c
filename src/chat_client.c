/*
 * chat_client.c - Terminal client for the TCP chat server.
 *
 * poll() watches both the keyboard (stdin) and the socket, so incoming
 * messages are displayed while the user is typing.
 */
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int connect_to(const char *host, const char *port)
{
    struct addrinfo hints, *res, *ai;
    int fd = -1, rc;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;
    rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "%s: %s\n", host, gai_strerror(rc));
        return -1;
    }
    for (ai = res; ai != NULL; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0)
        fprintf(stderr, "Cannot connect to %s:%s\n", host, port);
    return fd;
}

static int send_all(int fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = send(fd, buf, len, MSG_NOSIGNAL);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *port = "5000";
    struct pollfd pfds[2];
    char buf[1024];
    int opt, sock, stdin_open = 1;

    while ((opt = getopt(argc, argv, "p:h")) != -1) {
        if (opt == 'p') {
            port = optarg;
        } else {
            fprintf(stderr, "Usage: %s [-p port] <host>\n", argv[0]);
            return opt == 'h' ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }
    if (optind != argc - 1) {
        fprintf(stderr, "Usage: %s [-p port] <host>\n", argv[0]);
        return EXIT_FAILURE;
    }

    sock = connect_to(argv[optind], port);
    if (sock < 0)
        return EXIT_FAILURE;

    pfds[0].fd = sock;
    pfds[0].events = POLLIN;
    pfds[1].fd = STDIN_FILENO;
    pfds[1].events = POLLIN;

    for (;;) {
        if (poll(pfds, stdin_open ? 2 : 1, -1) < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            break;
        }
        if (pfds[0].revents != 0) {
            ssize_t n = recv(sock, buf, sizeof buf, 0);

            if (n <= 0) {
                fprintf(stderr, "Connection closed by the server.\n");
                break;
            }
            fwrite(buf, 1, (size_t)n, stdout);
            fflush(stdout);
        }
        if (stdin_open && pfds[1].revents != 0) {
            ssize_t n = read(STDIN_FILENO, buf, sizeof buf);

            if (n <= 0) { /* end of input: leave politely */
                send_all(sock, "/quit\n", 6);
                stdin_open = 0;
            } else if (send_all(sock, buf, (size_t)n) < 0) {
                perror("send");
                break;
            }
        }
    }
    close(sock);
    return EXIT_SUCCESS;
}
