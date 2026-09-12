#include "streamer.h"
#include "protocol.h"

#include <opus.h>
#include <android/log.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <cstring>

#define LOG_TAG "MicifyStreamer"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace micify {

bool Streamer::openSocket(const std::string &host, int port, bool useTcp) {
    mUseTcp = useTcp;
    int type = useTcp ? SOCK_STREAM : SOCK_DGRAM;
    mSocketFd = socket(AF_INET, type, 0);
    if (mSocketFd < 0) {
        LOGE("socket() failed: %s", strerror(errno));
        return false;
    }

    memset(&mDestAddr, 0, sizeof(mDestAddr));
    mDestAddr.sin_family = AF_INET;
    mDestAddr.sin_port = htons((uint16_t) port);
    if (inet_pton(AF_INET, host.c_str(), &mDestAddr.sin_addr) != 1) {
        LOGE("invalid host address: %s", host.c_str());
        close(mSocketFd);
        mSocketFd = -1;
        return false;
    }

    if (useTcp) {
        int one = 1;
        setsockopt(mSocketFd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        if (connect(mSocketFd, (struct sockaddr *) &mDestAddr, sizeof(mDestAddr)) != 0) {
            LOGE("TCP connect() failed: %s", strerror(errno));
            close(mSocketFd);
            mSocketFd = -1;
            return false;
        }
    } else {
        // connect() on a UDP socket just fixes the destination so we can
        // use send() instead of sendto() per packet; no handshake occurs.
        if (connect(mSocketFd, (struct sockaddr *) &mDestAddr, sizeof(mDestAddr)) != 0) {
            LOGE("UDP connect() failed: %s", strerror(errno));
            close(mSocketFd);
            mSocketFd = -1;
            return false;
        }
    }

    return true;
}

void Streamer::closeSocket() {
    if (mSocketFd >= 0) {
        close(mSocketFd);
        mSocketFd = -1;
    }
}

bool Streamer::sendRaw(const uint8_t *data, size_t len) {
    if (mSocketFd < 0) return false;

    if (mUseTcp) {
        uint16_t len_le = (uint16_t) len; // little-endian on all our targets, per docs/PROTOCOL.md
        if (send(mSocketFd, &len_le, sizeof(len_le), 0) != (ssize_t) sizeof(len_le)) return false;
        return send(mSocketFd, data, len, 0) == (ssize_t) len;
    }

    return send(mSocketFd, data, len, 0) == (ssize_t) len;
}

bool Streamer::sendFormatAnnounce() {
    uint8_t packet[MICIFY_HEADER_SIZE + MICIFY_FORMAT_SIZE];
    micify_header_t hdr{MICIFY_MAGIC, MICIFY_VERSION, MICIFY_FLAG_FORMAT, 0, 0};
    micify_format_t fmt{(uint32_t) kSampleRate, (uint8_t) kChannels, (uint8_t) kFrameMs, 0, 0};
    memcpy(packet, &hdr, sizeof(hdr));
    memcpy(packet + sizeof(hdr), &fmt, sizeof(fmt));
    return sendRaw(packet, sizeof(packet));
}

bool Streamer::start(const std::string &host, int port, bool useTcp) {
    if (mRunning.load()) return true;

    if (!openSocket(host, port, useTcp)) return false;
    if (!sendFormatAnnounce()) {
        closeSocket();
        return false;
    }

    int err = 0;
    mEncoder = opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err);
    if (err != OPUS_OK || !mEncoder) {
        LOGE("opus_encoder_create failed: %d", err);
        closeSocket();
        return false;
    }
    opus_encoder_ctl(mEncoder, OPUS_SET_BITRATE(32000));
    opus_encoder_ctl(mEncoder, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(mEncoder, OPUS_SET_PACKET_LOSS_PERC(5));

    // ~500ms hard ceiling; the sender thread keeps real steady-state depth
    // far lower, this just bounds memory if the network stalls briefly.
    size_t capacity = 1;
    while (capacity < (size_t) kSampleRate / 2) capacity <<= 1;
    mRing.assign(capacity, 0);
    mRingMask = capacity - 1;
    mWritePos = 0;
    mReadPos = 0;
    mSeq = 0;
    mTimestamp = 0;

    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Input)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setFormat(oboe::AudioFormat::I16)
        ->setChannelCount(kChannels)
        ->setSampleRate(kSampleRate)
        ->setInputPreset(oboe::InputPreset::VoiceCommunication)
        ->setCallback(this);

    oboe::Result result = builder.openStream(mStream);
    if (result != oboe::Result::OK) {
        LOGE("openStream failed: %s", oboe::convertToText(result));
        opus_encoder_destroy(mEncoder);
        mEncoder = nullptr;
        closeSocket();
        return false;
    }

    mRunning.store(true);
    mSenderThread = std::thread(&Streamer::senderThreadFn, this);

    result = mStream->requestStart();
    if (result != oboe::Result::OK) {
        LOGE("requestStart failed: %s", oboe::convertToText(result));
        stop();
        return false;
    }

    LOGI("streaming started: %d Hz, %d ch, %d ms frames, %s", kSampleRate, kChannels, kFrameMs,
         useTcp ? "TCP (USB)" : "UDP (Wi-Fi)");
    return true;
}

void Streamer::stop() {
    if (!mRunning.exchange(false)) {
        // still tear down a partially-started session (e.g. requestStart failed)
    }

    mCv.notify_all();
    if (mSenderThread.joinable()) mSenderThread.join();

    if (mStream) {
        mStream->stop();
        mStream->close();
        mStream.reset();
    }

    if (mEncoder) {
        opus_encoder_destroy(mEncoder);
        mEncoder = nullptr;
    }

    closeSocket();
    LOGI("streaming stopped");
}

oboe::DataCallbackResult Streamer::onAudioReady(oboe::AudioStream *stream, void *audioData,
                                                 int32_t numFrames) {
    (void) stream;
    const int16_t *src = static_cast<const int16_t *>(audioData);
    size_t n = (size_t) numFrames * kChannels;

    {
        std::lock_guard<std::mutex> lock(mMutex);
        size_t capacity = mRingMask + 1;
        size_t used = mWritePos - mReadPos;
        if (used + n > capacity) {
            // Sender thread (network/encode) fell behind: drop the oldest
            // samples rather than blocking this realtime callback.
            mReadPos = mWritePos + n - capacity;
        }
        for (size_t i = 0; i < n; i++) {
            mRing[(mWritePos + i) & mRingMask] = src[i];
        }
        mWritePos += n;
    }
    mCv.notify_one();

    return mRunning.load() ? oboe::DataCallbackResult::Continue : oboe::DataCallbackResult::Stop;
}

void Streamer::onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) {
    (void) stream;
    LOGE("stream error after close: %s", oboe::convertToText(error));
    mRunning.store(false);
    mCv.notify_all();
}

void Streamer::senderThreadFn() {
    const size_t need = (size_t) kFrameSize * kChannels;
    std::vector<int16_t> chunk(need);
    std::vector<uint8_t> opusOut(512);

    while (mRunning.load()) {
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mCv.wait(lock, [&] { return !mRunning.load() || (mWritePos - mReadPos) >= need; });
            if (!mRunning.load() && (mWritePos - mReadPos) < need) break;

            for (size_t i = 0; i < need; i++) {
                chunk[i] = mRing[(mReadPos + i) & mRingMask];
            }
            mReadPos += need;
        }

        int nbytes = opus_encode(mEncoder, chunk.data(), kFrameSize, opusOut.data(), (int) opusOut.size());
        if (nbytes < 0) {
            LOGE("opus_encode failed: %d", nbytes);
            continue;
        }

        uint8_t packet[MICIFY_HEADER_SIZE + 512];
        micify_header_t hdr{MICIFY_MAGIC, MICIFY_VERSION, 0, mSeq, mTimestamp};
        memcpy(packet, &hdr, sizeof(hdr));
        memcpy(packet + sizeof(hdr), opusOut.data(), (size_t) nbytes);

        if (!sendRaw(packet, sizeof(hdr) + (size_t) nbytes)) {
            LOGE("send failed: %s", strerror(errno));
        }

        mSeq++;
        mTimestamp += (uint32_t) kFrameSize;
    }
}

} // namespace micify
