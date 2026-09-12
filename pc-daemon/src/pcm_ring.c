#include "pcm_ring.h"
#include <stdlib.h>
#include <string.h>

int pcm_ring_init(pcm_ring_t *r, size_t capacity_pow2) {
    memset(r, 0, sizeof(*r));
    r->samples = calloc(capacity_pow2, sizeof(int16_t));
    if (!r->samples) return -1;
    r->capacity = capacity_pow2;
    r->mask = capacity_pow2 - 1;
    pthread_mutex_init(&r->lock, NULL);
    pthread_cond_init(&r->not_empty, NULL);
    return 0;
}

void pcm_ring_destroy(pcm_ring_t *r) {
    pthread_mutex_destroy(&r->lock);
    pthread_cond_destroy(&r->not_empty);
    free(r->samples);
    r->samples = NULL;
}

void pcm_ring_write(pcm_ring_t *r, const int16_t *samples, size_t n, size_t max_depth_samples) {
    pthread_mutex_lock(&r->lock);

    if (max_depth_samples > r->capacity) max_depth_samples = r->capacity;

    /* If the producer is about to exceed the current adaptive jitter-buffer
     * target (or the hard capacity), drop the oldest samples by advancing
     * read_pos. Bounded, adaptive latency beats an ever-growing queue. */
    size_t used = r->write_pos - r->read_pos;
    if (used + n > max_depth_samples) {
        r->read_pos = r->write_pos + n - max_depth_samples;
    }

    for (size_t i = 0; i < n; i++) {
        r->samples[(r->write_pos + i) & r->mask] = samples[i];
    }
    r->write_pos += n;

    pthread_cond_signal(&r->not_empty);
    pthread_mutex_unlock(&r->lock);
}

size_t pcm_ring_read_nonblock(pcm_ring_t *r, int16_t *out, size_t n) {
    if (pthread_mutex_trylock(&r->lock) != 0) {
        memset(out, 0, n * sizeof(int16_t));
        return 0;
    }

    size_t available = r->write_pos - r->read_pos;
    size_t to_copy = available < n ? available : n;

    for (size_t i = 0; i < to_copy; i++) {
        out[i] = r->samples[(r->read_pos + i) & r->mask];
    }
    r->read_pos += to_copy;

    pthread_mutex_unlock(&r->lock);

    if (to_copy < n) {
        memset(out + to_copy, 0, (n - to_copy) * sizeof(int16_t));
    }
    return to_copy;
}

size_t pcm_ring_available(pcm_ring_t *r) {
    pthread_mutex_lock(&r->lock);
    size_t available = r->write_pos - r->read_pos;
    pthread_mutex_unlock(&r->lock);
    return available;
}
