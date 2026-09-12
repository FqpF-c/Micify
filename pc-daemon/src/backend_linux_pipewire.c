/*
 * Linux backend: registers a PipeWire "Audio/Source" stream, which appears
 * to every other app as a normal microphone/capture device to pick from -
 * no ALSA loopback kernel module needed (that's what WO Mic relies on).
 *
 * Built and exercised on this dev machine (PipeWire 1.6.8, pkg-config
 * confirms libpipewire-0.3 + spa-0.2 headers are installed).
 */
#include "backend.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/defs.h>

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

struct backend {
    struct pw_main_loop *loop;
    struct pw_stream *stream;
    struct pw_thread_loop *thread_loop; /* runs the PipeWire loop off the caller's thread */
    pcm_ring_t *ring;
    int channels;
    int sample_rate;
};

static void on_process(void *userdata) {
    backend_t *d = userdata;
    struct pw_buffer *b = pw_stream_dequeue_buffer(d->stream);
    if (!b) return;

    struct spa_buffer *buf = b->buffer;
    int16_t *dst = buf->datas[0].data;
    if (!dst) {
        pw_stream_queue_buffer(d->stream, b);
        return;
    }

    int stride = (int)sizeof(int16_t) * d->channels;
    int n_frames = (int)(buf->datas[0].maxsize / stride);
    if (b->requested > 0 && (int)b->requested < n_frames) {
        n_frames = (int)b->requested;
    }

    pcm_ring_read_nonblock(d->ring, dst, (size_t)n_frames * (size_t)d->channels);

    buf->datas[0].chunk->offset = 0;
    buf->datas[0].chunk->stride = stride;
    buf->datas[0].chunk->size = (uint32_t)(n_frames * stride);

    pw_stream_queue_buffer(d->stream, b);
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process,
};

backend_t *backend_start(pcm_ring_t *ring, int sample_rate, int channels, const char *device_name) {
    pw_init(NULL, NULL);

    backend_t *d = calloc(1, sizeof(*d));
    d->ring = ring;
    d->channels = channels;
    d->sample_rate = sample_rate;

    d->thread_loop = pw_thread_loop_new("micify-pipewire", NULL);
    if (!d->thread_loop) {
        free(d);
        return NULL;
    }

    pw_thread_loop_lock(d->thread_loop);

    const char *name = device_name ? device_name : "Micify (phone mic)";

    d->stream = pw_stream_new_simple(
        pw_thread_loop_get_loop(d->thread_loop),
        "Micify Virtual Microphone",
        pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_MEDIA_CLASS, "Audio/Source",
            PW_KEY_MEDIA_ROLE, "Communication",
            PW_KEY_NODE_NAME, "micify_mic",
            PW_KEY_NODE_DESCRIPTION, name,
            PW_KEY_NODE_AUTOCONNECT, "true",
            NULL),
        &stream_events,
        d);

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    struct spa_audio_info_raw info = {
        .format = SPA_AUDIO_FORMAT_S16,
        .channels = (uint32_t)channels,
        .rate = (uint32_t)sample_rate,
    };
    const struct spa_pod *params[1];
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

    pw_stream_connect(d->stream,
                       PW_DIRECTION_OUTPUT,
                       PW_ID_ANY,
                       PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS,
                       params, 1);

    pw_thread_loop_unlock(d->thread_loop);
    pw_thread_loop_start(d->thread_loop);

    return d;
}

void backend_stop(backend_t *d) {
    if (!d) return;
    if (d->thread_loop) {
        pw_thread_loop_lock(d->thread_loop);
        if (d->stream) {
            pw_stream_destroy(d->stream);
        }
        pw_thread_loop_unlock(d->thread_loop);
        pw_thread_loop_stop(d->thread_loop);
        pw_thread_loop_destroy(d->thread_loop);
    }
    free(d);
}
