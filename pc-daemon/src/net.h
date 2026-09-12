#ifndef MICIFY_NET_H
#define MICIFY_NET_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

typedef enum {
    MICIFY_TRANSPORT_UDP = 0, /* Wi-Fi: raw datagrams */
    MICIFY_TRANSPORT_TCP = 1  /* USB: adb-forwarded TCP, u16-length-prefixed */
} micify_transport_t;

typedef void (*micify_packet_cb)(void *user, uint32_t seq, uint32_t timestamp,
                                  const unsigned char *payload, size_t len);
typedef void (*micify_format_cb)(void *user, uint32_t sample_rate, uint8_t channels, uint8_t frame_ms);

typedef struct {
    micify_transport_t transport;
    int port;
    int sockfd;
    int running;
    pthread_t thread;

    micify_packet_cb on_packet;
    micify_format_cb on_format;
    void *user;
} net_receiver_t;

int net_receiver_start(net_receiver_t *nr, micify_transport_t transport, int port,
                        micify_packet_cb on_packet, micify_format_cb on_format, void *user);
void net_receiver_stop(net_receiver_t *nr);

#endif /* MICIFY_NET_H */
