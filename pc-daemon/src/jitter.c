#include "jitter.h"
#include <opus.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdio.h>

#define MAX_CONCEALED_IN_A_ROW 25 /* ~250ms at 10ms frames: give up, resync instead of concealing forever */

static int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int jitter_init(jitter_state_t *j, pcm_ring_t *ring, int sample_rate, int channels, int frame_ms) {
    memset(j, 0, sizeof(*j));
    int err = 0;
    j->dec = opus_decoder_create(sample_rate, channels, &err);
    if (err != OPUS_OK || !j->dec) return -1;

    j->ring = ring;
    j->sample_rate = sample_rate;
    j->channels = channels;
    j->frame_size = sample_rate / 1000 * frame_ms;
    j->target_depth_frames = 2; /* start at 20ms @ 10ms frames */
    return 0;
}

void jitter_destroy(jitter_state_t *j) {
    if (j->dec) opus_decoder_destroy(j->dec);
    j->dec = NULL;
}

size_t jitter_target_depth_samples(const jitter_state_t *j) {
    return (size_t)j->target_depth_frames * (size_t)j->frame_size * (size_t)j->channels;
}

static void update_jitter_estimate(jitter_state_t *j, uint32_t timestamp) {
    int64_t t_now = now_ns();

    if (j->last_arrival_ns == 0) {
        j->last_arrival_ns = t_now;
        j->last_timestamp = timestamp;
        return;
    }

    /* Convert elapsed wall-clock time to the same units as `timestamp`
     * (samples), then compare against elapsed timestamp: their difference
     * is how far this packet's spacing deviated from ideal (RFC 3550 J). */
    double elapsed_wall_samples = (double)(t_now - j->last_arrival_ns) * j->sample_rate / 1e9;
    double elapsed_ts = (double)(int32_t)(timestamp - j->last_timestamp);
    double d = elapsed_wall_samples - elapsed_ts;
    if (d < 0) d = -d;

    j->jitter_estimate += (d - j->jitter_estimate) / 16.0;

    j->last_arrival_ns = t_now;
    j->last_timestamp = timestamp;

    j->packets_since_recalc++;
    if (j->packets_since_recalc >= 100) {
        j->packets_since_recalc = 0;
        int frames = (int)ceil((j->jitter_estimate * 3.0) / j->frame_size);
        if (frames < 1) frames = 1;
        if (frames > 6) frames = 6;
        j->target_depth_frames = frames;
    }
}

static void decode_and_push(jitter_state_t *j, const unsigned char *payload, size_t len) {
    int16_t pcm[960 * 2]; /* generous: up to 20ms stereo @ 48kHz */
    int n = opus_decode(j->dec, payload, (int32_t)len, pcm, j->frame_size, 0);
    if (n < 0) {
        j->stat_lost++;
        return;
    }
    pcm_ring_write(j->ring, pcm, (size_t)n * j->channels, jitter_target_depth_samples(j));
}

static void conceal_one_frame(jitter_state_t *j) {
    int16_t pcm[960 * 2];
    int n = opus_decode(j->dec, NULL, 0, pcm, j->frame_size, 0);
    if (n < 0) return;
    j->stat_lost++;
    pcm_ring_write(j->ring, pcm, (size_t)n * j->channels, jitter_target_depth_samples(j));
}

void jitter_on_packet(jitter_state_t *j, uint32_t seq, uint32_t timestamp,
                       const unsigned char *opus_payload, size_t len) {
    j->stat_packets++;
    update_jitter_estimate(j, timestamp);

    if (!j->have_seq) {
        j->have_seq = 1;
        j->next_seq = seq;
    }

    int32_t gap = (int32_t)(seq - j->next_seq);

    if (gap < 0) {
        /* Arrived too late relative to what we already played out. */
        j->stat_late_dropped++;
        return;
    }

    if (gap > MAX_CONCEALED_IN_A_ROW) {
        /* Huge jump (reconnect, long stall) - resync instead of concealing
         * hundreds of frames of fake audio. */
        j->next_seq = seq;
        gap = 0;
    }

    while (gap > 0) {
        conceal_one_frame(j);
        j->next_seq++;
        gap--;
    }

    decode_and_push(j, opus_payload, len);
    j->next_seq = seq + 1;
}
