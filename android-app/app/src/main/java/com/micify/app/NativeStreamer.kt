package com.micify.app

/**
 * Thin JNI wrapper over the native capture/encode/send pipeline
 * (see app/src/main/cpp/streamer.cpp). Everything performance-sensitive -
 * Oboe capture callback, Opus encode, socket send - lives in native code so
 * the real-time audio callback never has to cross the JNI boundary per
 * sample.
 */
object NativeStreamer {
    init {
        System.loadLibrary("micify_native")
    }

    /** Starts capturing the mic and streaming to [host]:[port]. */
    external fun start(host: String, port: Int, useTcp: Boolean): Boolean

    /** Stops capture/streaming and releases the audio stream. */
    external fun stop()

    /** True while actively capturing and sending. */
    external fun isStreaming(): Boolean
}
