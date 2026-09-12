# Micify Wire Protocol (v1)

Goal: lower end-to-end latency than WO Mic and Micstream's Wi-Fi mode by keeping
the transport dumb, the codec delay-optimized, and the jitter buffer adaptive
instead of fixed.

## Transport

Two carriers, same packet format:

- **Wi-Fi**: raw UDP. Phone sends datagrams straight to the PC daemon's UDP
  port. No handshake round trip required before audio can flow — the first
  audio packet doubles as the format announcement (see header below), so the
  daemon can configure its virtual audio node from the very first datagram.
- **USB**: `adb forward tcp:<port> tcp:<port>` tunnels a local TCP port to the
  same port on the phone over the USB link. Because ADB only forwards TCP
  byte-streams (not datagrams), each packet is prefixed with a `u16` length
  before the same header+payload used on the UDP path. This avoids
  maintaining two divergent protocols — only the framing differs.

UDP was chosen over TCP for Wi-Fi deliberately: TCP's retransmission and
head-of-line blocking means one lost packet stalls every packet behind it,
which is exactly the stutter WO Mic and naive PCM-over-TCP clones exhibit.
Losing a UDP packet just costs one Opus frame, concealed by PLC (below).

Endianness: all multi-byte fields are little-endian, fixed (not "host order").
Every realistic target (ARM64 phones, x86_64 PCs) is little-endian, so this
avoids a byteswap per packet for zero portability cost today. If a big-endian
target ever matters, bump `version`.

## Packet header (16 bytes, precedes the Opus payload)

| Offset | Size | Field       | Notes                                          |
|-------:|-----:|-------------|-------------------------------------------------|
| 0      | 4    | `magic`     | `0x4649434D` ("MICF")                            |
| 4      | 2    | `version`   | protocol version, currently `1`                  |
| 6      | 2    | `flags`     | bit0 = keyframe/format-announce, bits1-15 reserved |
| 8      | 4    | `seq`       | monotonically increasing packet sequence number  |
| 12     | 4    | `timestamp` | sample count at capture time (48000 Hz clock)    |

Payload: one Opus frame, `restricted_lowdelay` application mode, 48 kHz mono.

The **first packet** of a session sets `flags` bit0 and its payload is a
12-byte format record instead of audio:

| Offset | Size | Field         |
|-------:|-----:|---------------|
| 0      | 4    | `sample_rate` |
| 4      | 1    | `channels`    |
| 5      | 1    | `frame_ms`    | Opus frame size in ms (2/5/10/20) |
| 6      | 2    | reserved      |
| 8      | 4    | reserved      |

The daemon uses this to create/reconfigure its virtual audio node, then every
subsequent packet with bit0 clear is an Opus-encoded audio frame.

## Jitter buffer

Fixed-size buffers is what gives WO Mic and naive clones their either-too-laggy
or too-glitchy behavior. Instead:

- Track inter-arrival jitter with an EWMA (RFC 3550-style estimator).
- Target depth starts at 2 frames (20ms @ 10ms frames) and adapts within
  [1, 6] frames based on the jitter estimate, recomputed every ~200 packets.
- A missing `seq` at playout time triggers Opus PLC (decode with a NULL
  payload) instead of underrunning or inserting silence.
- Packets arriving more than one buffer-depth late are dropped, not queued —
  stale audio is worse than a concealed gap.

## Session lifecycle

- No explicit teardown packet. If no packet arrives for 3 seconds, the daemon
  tears down the virtual node's audio (goes silent) but keeps the node itself
  registered so reconnecting doesn't flap other apps' device selection.
- Reconnection just resumes sending; a new format-announce packet (bit0 set)
  is sent by the phone whenever it (re)starts a stream, and the daemon resets
  its jitter buffer and `seq` tracking on that event.
