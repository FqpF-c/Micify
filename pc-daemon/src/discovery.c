#include "discovery.h"
#include "protocol.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static void *discovery_thread_fn(void *arg) {
    discovery_t *d = (discovery_t *) arg;

    char hostname[MICIFY_DISCOVERY_NAME_LEN] = {0};
    gethostname(hostname, sizeof(hostname) - 1);

    struct sockaddr_in broadcast_addr;
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(MICIFY_DISCOVERY_PORT);
    broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;

    micify_beacon_t beacon;
    memset(&beacon, 0, sizeof(beacon));
    beacon.magic = MICIFY_DISCOVERY_MAGIC;
    beacon.version = MICIFY_DISCOVERY_VERSION;
    beacon.audio_port = (uint16_t) d->audio_port;
    size_t name_len = strnlen(hostname, sizeof(beacon.name) - 1);
    memcpy(beacon.name, hostname, name_len);

    while (d->running) {
        sendto(d->sockfd, &beacon, sizeof(beacon), 0,
               (struct sockaddr *) &broadcast_addr, sizeof(broadcast_addr));
        usleep(MICIFY_DISCOVERY_INTERVAL_MS * 1000);
    }

    return NULL;
}

int discovery_start(discovery_t *d, int audio_port) {
    memset(d, 0, sizeof(*d));
    d->audio_port = audio_port;

    d->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (d->sockfd < 0) {
        perror("discovery: socket");
        return -1;
    }

    int broadcast_enable = 1;
    if (setsockopt(d->sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        perror("discovery: setsockopt(SO_BROADCAST)");
        close(d->sockfd);
        return -1;
    }

    d->running = 1;
    if (pthread_create(&d->thread, NULL, discovery_thread_fn, d) != 0) {
        close(d->sockfd);
        d->running = 0;
        return -1;
    }

    return 0;
}

void discovery_stop(discovery_t *d) {
    if (!d->running) return;
    d->running = 0;
    pthread_join(d->thread, NULL);
    close(d->sockfd);
}
