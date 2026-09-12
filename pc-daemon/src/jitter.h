#ifndef MICIFY_JITTER_H
#define MICIFY_JITTER_H

#include <stdint.h>
#include <stddef.h>
#include "pcm_ring.h"

typedef struct OpusDecoder OpusDecoder; /* matches opus.h's typedef; avoids requiring the opus headers here */

typedef struct {
    OpusDecoder *dec;
    pcm_ring_t *ring;
    int sample_rate;
    int channels;
    int frame_size; /* samples per channel per Opus frame */

    int have_seq;
    uint32_t next_seq;

    /* RFC 3550-style inter-arrival jitter estimate, in timestamp units
     * (samples), used to adapt the jitter buffer's target depth. */
    double jitter_estimate;
    int32_t last_transit;
    uint32_t last_timestamp;
    int64_t last_arrival_ns;

    int target_depth_frames; /* adaptive, clamped to [1, 6] */
    int packets_since_recalc;

    uint64_t stat_packets;
    uint64_t stat_lost;
    uint64_t stat_late_dropped;
} jitter_state_t;

int jitter_init(jitter_state_t *j, pcm_ring_t *ring, int sample_rate, int channels, int frame_ms);
void jitter_destroy(jitter_state_t *j);

/* Feed one received Opus payload (opus_payload/len) with its protocol
 * seq/timestamp. Decodes in order, concealing any gap with Opus PLC, and
 * pushes PCM into the ring. Safe to call from a single network thread only. */
void jitter_on_packet(jitter_state_t *j, uint32_t seq, uint32_t timestamp,
                       const unsigned char *opus_payload, size_t len);

/* current adaptive target depth, in samples (interleaved) */
size_t jitter_target_depth_samples(const jitter_state_t *j);

#endif /* MICIFY_JITTER_H */
