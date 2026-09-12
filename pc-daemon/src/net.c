#include "net.h"
#include "protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

static void handle_datagram(net_receiver_t *nr, const unsigned char *buf, size_t len) {
    if (len < MICIFY_HEADER_SIZE) return;

    micify_header_t hdr;
    memcpy(&hdr, buf, MICIFY_HEADER_SIZE);

    if (hdr.magic != MICIFY_MAGIC) return;
    if (hdr.version != MICIFY_VERSION) return;

    const unsigned char *payload = buf + MICIFY_HEADER_SIZE;
    size_t payload_len = len - MICIFY_HEADER_SIZE;

    if (hdr.flags & MICIFY_FLAG_FORMAT) {
        if (payload_len < MICIFY_FORMAT_SIZE) return;
        micify_format_t fmt;
        memcpy(&fmt, payload, MICIFY_FORMAT_SIZE);
        if (nr->on_format) nr->on_format(nr->user, fmt.sample_rate, fmt.channels, fmt.frame_ms);
        return;
    }

    if (nr->on_packet) nr->on_packet(nr->user, hdr.seq, hdr.timestamp, payload, payload_len);
}

static void *udp_thread_fn(void *arg) {
    net_receiver_t *nr = (net_receiver_t *)arg;
    unsigned char buf[MICIFY_MAX_PACKET];

    while (nr->running) {
        ssize_t n = recv(nr->sockfd, buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (!nr->running) break;
            continue;
        }
        handle_datagram(nr, buf, (size_t)n);
    }
    return NULL;
}

static ssize_t read_full(int fd, void *buf, size_t n) {
    size_t got = 0;
    unsigned char *p = (unsigned char *)buf;
    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r <= 0) return r;
        got += (size_t)r;
    }
    return (ssize_t)got;
}

static void *tcp_thread_fn(void *arg) {
    net_receiver_t *nr = (net_receiver_t *)arg;
    unsigned char buf[MICIFY_MAX_PACKET];

    while (nr->running) {
        int conn = accept(nr->sockfd, NULL, NULL);
        if (conn < 0) {
            if (errno == EINTR) continue;
            if (!nr->running) break;
            continue;
        }

        int one = 1;
        setsockopt(conn, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        while (nr->running) {
            uint16_t len_be;
            if (read_full(conn, &len_be, sizeof(len_be)) <= 0) break;
            size_t len = len_be; /* length prefix is little-endian, host-order on our LE targets */
            if (len == 0 || len > sizeof(buf)) break;
            if (read_full(conn, buf, len) <= 0) break;
            handle_datagram(nr, buf, len);
        }
        close(conn);
    }
    return NULL;
}

int net_receiver_start(net_receiver_t *nr, micify_transport_t transport, int port,
                        micify_packet_cb on_packet, micify_format_cb on_format, void *user) {
    memset(nr, 0, sizeof(*nr));
    nr->transport = transport;
    nr->port = port;
    nr->on_packet = on_packet;
    nr->on_format = on_format;
    nr->user = user;

    int type = (transport == MICIFY_TRANSPORT_UDP) ? SOCK_DGRAM : SOCK_STREAM;
    nr->sockfd = socket(AF_INET, type, 0);
    if (nr->sockfd < 0) {
        perror("socket");
        return -1;
    }

    /* SO_REUSEADDR is only set for TCP (USB mode), where it just lets us
     * re-bind promptly after a restart instead of waiting out TIME_WAIT.
     * Deliberately NOT set for UDP: on Linux, two UDP sockets can both
     * bind the same port when both have SO_REUSEADDR, and the kernel then
     * splits incoming datagrams between them somewhat arbitrarily - if a
     * second daemon instance is accidentally left running, the phone's
     * format-announce packet and its actual audio packets can land on
     * *different* processes, so one shows "connected" while the other
     * silently discards every audio packet it gets. Better to fail loudly
     * on the second instance than to silently black-hole audio. */
    if (transport == MICIFY_TRANSPORT_TCP) {
        int reuse = 1;
        setsockopt(nr->sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(nr->sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        fprintf(stderr, "[micify] is another micify-daemon instance already running on port %d?\n", port);
        close(nr->sockfd);
        return -1;
    }

    if (transport == MICIFY_TRANSPORT_TCP) {
        if (listen(nr->sockfd, 1) < 0) {
            perror("listen");
            close(nr->sockfd);
            return -1;
        }
    }

    nr->running = 1;
    void *(*thread_fn)(void *) = (transport == MICIFY_TRANSPORT_UDP) ? udp_thread_fn : tcp_thread_fn;
    if (pthread_create(&nr->thread, NULL, thread_fn, nr) != 0) {
        close(nr->sockfd);
        nr->running = 0;
        return -1;
    }

    return 0;
}

void net_receiver_stop(net_receiver_t *nr) {
    if (!nr->running) return;
    nr->running = 0;
    shutdown(nr->sockfd, SHUT_RDWR);
    close(nr->sockfd);
    pthread_join(nr->thread, NULL);
}
