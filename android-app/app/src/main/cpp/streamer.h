#ifndef MICIFY_STREAMER_H
#define MICIFY_STREAMER_H

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <oboe/Oboe.h>
#include <netinet/in.h>

typedef struct OpusEncoder OpusEncoder;

namespace micify {

/*
 * Capture pipeline: Oboe's low-latency/exclusive AAudio callback pushes raw
 * PCM into a small ring buffer and returns immediately (no blocking syscalls
 * on the realtime audio thread). A separate sender thread drains
 * Opus-frame-sized chunks, encodes them, and writes them to the socket -
 * mirroring the split between the PC daemon's network thread and its
 * PipeWire realtime callback in pc-daemon/src/backend_linux_pipewire.c.
 */
class Streamer : public oboe::AudioStreamCallback {
public:
    bool start(const std::string &host, int port, bool useTcp);
    void stop();
    bool isStreaming() const { return mRunning.load(); }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *stream, void *audioData,
                                           int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) override;

private:
    bool openSocket(const std::string &host, int port, bool useTcp);
    void closeSocket();
    bool sendFormatAnnounce();
    bool sendRaw(const uint8_t *data, size_t len);
    void senderThreadFn();

    std::shared_ptr<oboe::AudioStream> mStream;
    OpusEncoder *mEncoder = nullptr;

    static constexpr int kSampleRate = 48000;
    static constexpr int kChannels = 1;
    static constexpr int kFrameMs = 10;
    static constexpr int kFrameSize = kSampleRate / 1000 * kFrameMs; // samples/channel per Opus frame

    // Guarded by mMutex. The audio callback only ever holds the lock for a
    // short memcpy-equivalent loop (no syscalls), so contention with the
    // sender thread is brief.
    std::vector<int16_t> mRing;
    size_t mRingMask = 0;
    size_t mWritePos = 0;
    size_t mReadPos = 0;
    std::mutex mMutex;
    std::condition_variable mCv;

    std::thread mSenderThread;
    std::atomic<bool> mRunning{false};

    int mSocketFd = -1;
    bool mUseTcp = false;
    struct sockaddr_in mDestAddr {};

    uint32_t mSeq = 0;
    uint32_t mTimestamp = 0;
};

} // namespace micify

#endif // MICIFY_STREAMER_H
