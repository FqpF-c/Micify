/*
 * Standalone test tool (not part of the shipped daemon): generates a sine
 * wave, encodes it with Opus, and sends it over UDP using the wire protocol
 * from docs/PROTOCOL.md - stands in for the Android app so the PC daemon's
 * network -> jitter buffer -> PipeWire path can be verified without a phone.
 *
 * Build: gcc test_sender.c -o test_sender $(pkg-config --cflags --libs opus) -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <opus.h>

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t seq;
    uint32_t timestamp;
} header_t;

typedef struct {
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t frame_ms;
    uint16_t reserved0;
    uint32_t reserved1;
} format_t;
#pragma pack(pop)

#define MAGIC 0x4649434Du
#define FLAG_FORMAT 0x1u

int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? atoi(argv[2]) : 44551;
    double freq = argc > 3 ? atof(argv[3]) : 440.0;
    int seconds = argc > 4 ? atoi(argv[4]) : 10;

    int sample_rate = 48000;
    int channels = 1;
    int frame_ms = 10;
    int frame_size = sample_rate / 1000 * frame_ms;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    /* format-announce packet */
    {
        unsigned char buf[sizeof(header_t) + sizeof(format_t)];
        header_t hdr = {MAGIC, 1, FLAG_FORMAT, 0, 0};
        format_t fmt = {(uint32_t)sample_rate, (uint8_t)channels, (uint8_t)frame_ms, 0, 0};
        memcpy(buf, &hdr, sizeof(hdr));
        memcpy(buf + sizeof(hdr), &fmt, sizeof(fmt));
        sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&addr, sizeof(addr));
        printf("sent format-announce: %d Hz, %d ch, %d ms frames\n", sample_rate, channels, frame_ms);
    }

    usleep(50 * 1000);

    int err;
    OpusEncoder *enc = opus_encoder_create(sample_rate, channels, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err);
    if (err != OPUS_OK) {
        fprintf(stderr, "opus_encoder_create failed: %d\n", err);
        return 1;
    }
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(32000));

    int16_t pcm[960];
    unsigned char opus_out[512];
    uint32_t seq = 0, ts = 0;
    double phase = 0.0;
    double phase_inc = 2.0 * M_PI * freq / sample_rate;

    int total_frames = (seconds * 1000) / frame_ms;
    for (int f = 0; f < total_frames; f++) {
        for (int i = 0; i < frame_size; i++) {
            pcm[i] = (int16_t)(8000.0 * sin(phase));
            phase += phase_inc;
        }
        int nbytes = opus_encode(enc, pcm, frame_size, opus_out, sizeof(opus_out));
        if (nbytes < 0) {
            fprintf(stderr, "opus_encode failed: %d\n", nbytes);
            break;
        }

        unsigned char packet[sizeof(header_t) + 512];
        header_t hdr = {MAGIC, 1, 0, seq, ts};
        memcpy(packet, &hdr, sizeof(hdr));
        memcpy(packet + sizeof(hdr), opus_out, (size_t)nbytes);
        sendto(sock, packet, sizeof(hdr) + (size_t)nbytes, 0, (struct sockaddr *)&addr, sizeof(addr));

        seq++;
        ts += (uint32_t)frame_size;
        usleep((useconds_t)(frame_ms * 1000));
    }

    printf("sent %u frames\n", seq);
    opus_encoder_destroy(enc);
    close(sock);
    return 0;
}
