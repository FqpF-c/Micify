#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "net.h"
#include "jitter.h"
#include "backend.h"
#include "pcm_ring.h"

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) { (void)sig; g_stop = 1; }

typedef struct {
    pcm_ring_t ring;
    jitter_state_t jitter;
    backend_t *backend;
    int initialized;
    const char *device_name;
} app_state_t;

static size_t next_pow2(size_t v) {
    size_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

static void on_format(void *user, uint32_t sample_rate, uint8_t channels, uint8_t frame_ms) {
    app_state_t *app = (app_state_t *)user;

    if (app->initialized) {
        fprintf(stderr, "[micify] format re-announced (sr=%u ch=%u frame=%ums), resetting session\n",
                sample_rate, channels, frame_ms);
        backend_stop(app->backend);
        jitter_destroy(&app->jitter);
    } else {
        size_t capacity = next_pow2((size_t)sample_rate * channels); /* ~1s hard ceiling */
        if (pcm_ring_init(&app->ring, capacity) != 0) {
            fprintf(stderr, "[micify] failed to allocate PCM ring buffer\n");
            exit(1);
        }
    }

    if (jitter_init(&app->jitter, &app->ring, (int)sample_rate, (int)channels, (int)frame_ms) != 0) {
        fprintf(stderr, "[micify] failed to create Opus decoder for sr=%u ch=%u\n", sample_rate, channels);
        exit(1);
    }

    app->backend = backend_start(&app->ring, (int)sample_rate, (int)channels, app->device_name);
    if (!app->backend) {
        fprintf(stderr, "[micify] failed to start audio backend\n");
        exit(1);
    }

    app->initialized = 1;
    fprintf(stderr, "[micify] session started: %u Hz, %u ch, %ums frames\n",
            sample_rate, channels, frame_ms);
}

static void on_packet(void *user, uint32_t seq, uint32_t timestamp,
                       const unsigned char *payload, size_t len) {
    app_state_t *app = (app_state_t *)user;
    if (!app->initialized) return; /* wait for the format-announce packet first */
    jitter_on_packet(&app->jitter, seq, timestamp, payload, len);
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "Usage: %s [--port PORT] [--usb] [--name DEVICE_NAME]\n"
            "  --port PORT   UDP (or TCP, with --usb) port to listen on. Default 44551.\n"
            "  --usb         Listen over TCP instead of UDP, for use behind\n"
            "                `adb forward tcp:PORT tcp:PORT`.\n"
            "  --name NAME   Name shown for the virtual microphone device.\n",
            argv0);
}

int main(int argc, char **argv) {
    setvbuf(stderr, NULL, _IONBF, 0);

    int port = 44551;
    micify_transport_t transport = MICIFY_TRANSPORT_UDP;
    const char *device_name = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--usb") == 0) {
            transport = MICIFY_TRANSPORT_TCP;
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            device_name = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    app_state_t app;
    memset(&app, 0, sizeof(app));
    app.device_name = device_name;

    net_receiver_t nr;
    if (net_receiver_start(&nr, transport, port, on_packet, on_format, &app) != 0) {
        fprintf(stderr, "[micify] failed to start on port %d\n", port);
        return 1;
    }

    fprintf(stderr, "[micify] listening on %s port %d, waiting for phone...\n",
            transport == MICIFY_TRANSPORT_UDP ? "UDP" : "TCP", port);

    while (!g_stop) {
        usleep(200 * 1000);
    }

    fprintf(stderr, "\n[micify] shutting down\n");
    net_receiver_stop(&nr);
    if (app.initialized) {
        backend_stop(app.backend);
        jitter_destroy(&app.jitter);
        pcm_ring_destroy(&app.ring);
    }
    return 0;
}
