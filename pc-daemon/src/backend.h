#ifndef MICIFY_BACKEND_H
#define MICIFY_BACKEND_H

#include "pcm_ring.h"

typedef struct backend backend_t;

/* Starts the platform-specific virtual microphone sink, consuming decoded
 * PCM from `ring` as it plays out. Runs its own thread; call backend_stop
 * to tear down. */
backend_t *backend_start(pcm_ring_t *ring, int sample_rate, int channels, const char *device_name);
void backend_stop(backend_t *b);

#endif /* MICIFY_BACKEND_H */
