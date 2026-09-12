#ifndef MICIFY_PCM_RING_H
#define MICIFY_PCM_RING_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

/*
 * Single-producer (network/decode thread), single-consumer (realtime audio
 * callback) ring buffer of interleaved int16 samples.
 *
 * The consumer uses pthread_mutex_trylock: a realtime audio callback must
 * never block on a lock held by a non-realtime thread, so on contention it
 * just emits silence for that callback instead of stalling the graph.
 */
typedef struct {
    int16_t *samples;
    size_t capacity;   /* in samples (interleaved), power of two */
    size_t mask;
    size_t write_pos;
    size_t read_pos;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
} pcm_ring_t;

int pcm_ring_init(pcm_ring_t *r, size_t capacity_pow2);
void pcm_ring_destroy(pcm_ring_t *r);

/* producer side: pushes samples. If the buffered depth would exceed
 * max_depth_samples, drops the oldest data instead of blocking or growing
 * unbounded. max_depth_samples is the *adaptive* jitter-buffer target (in
 * samples) recomputed by the jitter estimator, capped by `capacity`. */
void pcm_ring_write(pcm_ring_t *r, const int16_t *samples, size_t n, size_t max_depth_samples);

/* consumer side: fills `out` with n samples; pads with silence and returns
 * the number of real samples actually available if fewer than n are ready,
 * or if the trylock fails. */
size_t pcm_ring_read_nonblock(pcm_ring_t *r, int16_t *out, size_t n);

size_t pcm_ring_available(pcm_ring_t *r);

#endif /* MICIFY_PCM_RING_H */
