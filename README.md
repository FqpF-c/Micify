# Micify

A phone-as-microphone system in the spirit of [WO Mic](https://wolicheng.com/womic/) and
[Micstream](https://micstream.io/), built around one goal: lower end-to-end
latency than either, by tightening every stage instead of just swapping the
transport.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the full design and how
it compares to WO Mic/Micstream/naive clones, and
[docs/PROTOCOL.md](docs/PROTOCOL.md) for the wire format.

## Downloads

Prebuilt binaries are attached to [Releases](https://github.com/FqpF-c/Micify/releases):
a `Micify-debug.apk` for Android and a `micify-linux-x86_64.tar.gz` for
Linux (GTK GUI + the daemon it drives).

## Status

- **`pc-daemon/`** (C) - builds and runs on this machine, both the headless
  `micify-daemon` and the `micify` GTK GUI that launches it. Verified
  end-to-end: a synthetic tone sent through the full network → jitter
  buffer → Opus decode → PipeWire virtual-microphone path was recorded back
  losslessly (matching peak amplitude, no clipping/corruption) using
  `pw-record` against the daemon's registered "Micify (phone mic)" source.
  Windows backend is written but unbuilt (no Windows toolchain here) - see
  `pc-daemon/README.md`.
- **`android-app/`** (Kotlin + C++/NDK) - builds cleanly: `./gradlew
  assembleDebug` produces a working, correctly-signed, installable APK
  (Oboe + Opus native libs for arm64-v8a/armeabi-v7a confirmed bundled,
  manifest/label/icon confirmed correct via `aapt dump badging`). Not yet
  tested against a real phone/microphone by a human, since this machine
  has no attached Android device - see `android-app/README.md`.

## Quick start (PC side, Linux)

Either grab the release tarball and run `./micify` (GUI), or build from
source:

```bash
cd pc-daemon
make
./micify-daemon --port 44551      # headless
./micify                          # GUI wrapper around the same daemon
```

`wpctl status` should show a new "Micify (phone mic)" source once the phone
(or `pc-daemon/tools/test_sender`) starts sending.

## Repo layout

```
docs/            architecture + wire protocol
pc-daemon/       PC-side receiver: network -> jitter buffer -> Opus decode -> virtual mic (+ GTK GUI)
android-app/     phone-side capture: Oboe -> Opus encode -> network
assets/icon/     shared Micify mic icon (SVG source + rendered PNGs)
```
