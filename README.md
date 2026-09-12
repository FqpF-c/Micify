# Micify

A phone-as-microphone system in the spirit of [WO Mic](https://wolicheng.com/womic/) and
[Micstream](https://micstream.io/), built around one goal: lower end-to-end
latency than either, by tightening every stage instead of just swapping the
transport.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the full design and how
it compares to WO Mic/Micstream/naive clones, and
[docs/PROTOCOL.md](docs/PROTOCOL.md) for the wire format.

## Status

- **`pc-daemon/`** (C) - builds and runs on this machine. Verified
  end-to-end: a synthetic tone sent through the full network → jitter
  buffer → Opus decode → PipeWire virtual-microphone path was recorded back
  losslessly (matching peak amplitude, no clipping/corruption) using
  `pw-record` against the daemon's registered "Micify (phone mic)" source.
  Windows backend is written but unbuilt (no Windows toolchain here) - see
  `pc-daemon/README.md`.
- **`android-app/`** (Kotlin + C++/NDK) - complete source scaffold (Oboe
  capture, Opus encode, JNI bridge, UI), **not build-tested** - this
  machine has no Android SDK/NDK. Needs Android Studio; see
  `android-app/README.md`.

## Quick start (PC side, Linux)

```bash
cd pc-daemon
make
./micify-daemon --port 44551
```

`wpctl status` should show a new "Micify (phone mic)" source once the phone
(or `pc-daemon/tools/test_sender`) starts sending.

## Repo layout

```
docs/            architecture + wire protocol
pc-daemon/       PC-side receiver: network -> jitter buffer -> Opus decode -> virtual mic
android-app/     phone-side capture: Oboe -> Opus encode -> network
```
