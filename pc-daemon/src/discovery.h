#ifndef MICIFY_DISCOVERY_H
#define MICIFY_DISCOVERY_H

#include <pthread.h>

typedef struct {
    int sockfd;
    int running;
    pthread_t thread;
    int audio_port;
} discovery_t;

/* Starts broadcasting a beacon (see protocol.h micify_beacon_t) to
 * 255.255.255.255:MICIFY_DISCOVERY_PORT once a second so phones on the
 * same LAN can find this daemon without typing an IP. Wi-Fi/UDP only -
 * doesn't make sense for USB mode, which always talks to 127.0.0.1. */
int discovery_start(discovery_t *d, int audio_port);
void discovery_stop(discovery_t *d);

#endif /* MICIFY_DISCOVERY_H */
