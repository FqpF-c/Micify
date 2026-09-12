/*
 * Windows backend — NOT BUILD-TESTED on this machine (no MSVC/MinGW or
 * Windows SDK available in this dev environment; this file is excluded from
 * the Linux Makefile's object list).
 *
 * Rather than shipping and signing our own kernel-mode audio class driver
 * (a large, separate undertaking - see docs/ARCHITECTURE.md), this backend
 * targets the virtual microphone endpoint exposed by the existing
 * MIT-licensed driver:
 *   https://github.com/VirtualDrivers/Virtual-Audio-Driver
 *
 * Expected integration shape once ported to a Windows build:
 *   1. User installs Virtual-Audio-Driver once (adds a "Micify" capture
 *      endpoint to Windows' audio device list).
 *   2. This backend opens that endpoint via WASAPI in shared or exclusive
 *      mode (IAudioClient::Initialize), matching the same S16/48kHz/mono
 *      format negotiated over the wire.
 *   3. A render thread pulls from the same pcm_ring_t used by the Linux
 *      backend and writes into the endpoint's IAudioRenderClient buffer on
 *      each wakeup, mirroring on_process() in backend_linux_pipewire.c.
 *
 * To port: build on a Windows machine with the Windows SDK, link against
 * ole32/winmm/avrt, and implement backend_start/backend_stop against the
 * backend.h interface used by the Linux backend.
 */

#ifdef _WIN32

#include "backend.h"
#include <windows.h>

/* TODO: port - see comment block above. Left unimplemented deliberately
 * rather than shipping unverified WASAPI code that has never been compiled. */

backend_t *backend_start(pcm_ring_t *ring, int sample_rate, int channels, const char *device_name) {
    (void)ring; (void)sample_rate; (void)channels; (void)device_name;
    return NULL;
}

void backend_stop(backend_t *b) {
    (void)b;
}

#endif /* _WIN32 */
