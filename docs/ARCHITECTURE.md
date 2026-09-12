# Micify Architecture

A phone-as-microphone system, in the spirit of WO Mic and Micstream, designed
around one constraint: minimize end-to-end latency at every stage rather than
bolting a network transport onto whatever audio API is most convenient.

```
 [Android phone]                                   [PC]
 ┌─────────────────────────┐                ┌─────────────────────────┐
 │ Oboe (AAudio, MMAP,      │                │ udp_recv / usb (adb)    │
 │ low-latency, exclusive)  │   UDP (WiFi)   │        │                │
 │        │ raw PCM frames  │   or           │        ▼                │
 │        ▼                 │   adb-forward  │  jitter_buffer          │
 │ libopus encode            ├───TCP (USB)──▶│   (adaptive, PLC)       │
 │ (restricted_lowdelay)     │                │        │                │
 │        │                  │                │        ▼                │
 │        ▼                  │                │  libopus decode          │
 │ packetize + send           │                │        │                │
 └─────────────────────────┘                │        ▼                │
                                              │  backend (per-OS)       │
                                              │  Linux: PipeWire        │
                                              │    virtual Audio/Source │
                                              │  Windows: writes into   │
                                              │    Virtual-Audio-Driver │
                                              │    endpoint via WASAPI  │
                                              └─────────────────────────┘
```

## Why these choices, contrasted with the prior art we researched

| Concern            | WO Mic                          | Naive PCM/TCP clones      | Micify                                            |
|---------------------|----------------------------------|-----------------------------|-----------------------------------------------------|
| Capture path         | stock `AudioRecord`               | stock `AudioRecord`          | Oboe, low-latency + MMAP exclusive stream            |
| Codec                | raw PCM over UDP                  | raw PCM over TCP              | Opus 48kHz mono, `restricted_lowdelay`, 10ms frames |
| Transport            | TCP control + UDP audio           | TCP for everything (stalls)   | UDP for audio always; TCP only as an ADB-over-USB tunnel |
| Jitter handling       | none documented                    | none (fixed/no buffer)        | adaptive depth (1-6 frames) + Opus PLC for loss     |
| Linux PC sink         | `snd-aloop` ALSA kernel module     | unspecified                    | PipeWire virtual `Audio/Source` node (no kernel module) |
| Windows PC sink       | kernel virtual device (their own)  | unspecified                    | integrates with the existing MIT-licensed [Virtual-Audio-Driver](https://github.com/VirtualDrivers/Virtual-Audio-Driver) rather than shipping our own signed kernel driver |

## Repo layout

- `docs/` — protocol spec and this document.
- `pc-daemon/` — the PC-side receiver. Written in C (this dev machine has
  `gcc`/`clang` + `libpipewire-0.3` + `libopus` dev headers already installed,
  but no Rust toolchain — C lets us build and actually test the PipeWire
  virtual-source path here instead of shipping unverified scaffolding).
  - `backend_linux_pipewire.c` — implemented and tested on this machine.
  - `backend_windows_wasapi.c` — written against the `windows.h`/WASAPI +
    Virtual-Audio-Driver IOCTL surface, but **not build-tested here** (this
    machine has no MSVC/MinGW). Needs a Windows box with that driver
    installed and the Windows SDK to compile and validate.
- `android-app/` — Kotlin app (UI, permissions, connection management) with a
  C++/NDK module (`app/src/main/cpp`) using Oboe for capture and libopus for
  encoding in the same real-time audio callback (avoiding MediaCodec's
  async queueing, which adds latency). **Not build-tested here** — needs
  Android Studio / the Android SDK+NDK, neither of which is installed on
  this machine.

## USB transport note

True lowest-latency USB would mean an Android **USB Accessory / AOA gadget**
audio path bypassing the network stack entirely. That requires either root
or a custom USB gadget driver on the PC side — too large a scope for v1.
The pragmatic middle ground: `adb forward`, which tunnels a TCP byte-stream
over the existing USB connection through the ADB daemon already running on
any dev-enabled phone. Still meaningfully lower and more *stable* latency
than Wi-Fi (no radio jitter, no packet loss), at the cost of needing USB
debugging enabled and `adb` present on the PC.
