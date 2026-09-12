# Micify Android app

Kotlin UI + a C++/NDK capture pipeline (Oboe for low-latency AAudio capture,
libopus for encoding). Builds cleanly with the command-line SDK/NDK + Gradle
(`./gradlew assembleDebug` - confirmed producing a correctly-signed,
installable APK with both ABIs' native libs bundled). Not yet tested
against a real phone/mic by a human - this machine has no Android device
attached, only the ability to compile.

## One-time setup

libopus isn't distributed by the NDK, so it's vendored as source and built
from source by the app's CMake (see `app/src/main/cpp/CMakeLists.txt`):

```bash
git clone --depth 1 --branch v1.5.2 https://github.com/xiph/opus \
    app/src/main/cpp/third_party/opus
```

Then open the project in Android Studio (or `./gradlew assembleDebug` with
the SDK/NDK on `PATH`). `ndkVersion` in `app/build.gradle.kts` pins a
specific NDK release - adjust if Android Studio prompts to install a
different one.

The launcher icon (`res/mipmap-*/ic_launcher*.png`) is generated from
`assets/icon/micify_icon.svg` at the repo root - re-render it with
`rsvg-convert` if you change the source SVG (see that file's history for
the exact sizes used).

## Wi-Fi mode

Enter the PC's LAN IP and the daemon's port (`44551` by default), leave
"USB mode" off, and start streaming.

## USB mode

1. Enable USB debugging on the phone and plug it in.
2. On the PC: `adb forward tcp:44551 tcp:44551`
3. Run `micify-daemon --port 44551 --usb` on the PC.
4. In the app, flip "USB mode" on (the app then connects to its own
   `127.0.0.1:44551`, which `adb forward` tunnels over USB to the PC) and
   start streaming.

## Where the latency work actually happens

- `streamer.cpp` opens the Oboe stream with `PerformanceMode::LowLatency` +
  `SharingMode::Exclusive` (the AAudio MMAP path) - this is the single
  biggest capture-side latency lever, well below the default `AudioRecord`
  path most naive mic apps use.
- The audio callback (`onAudioReady`) only copies samples into a ring buffer
  and returns - no socket I/O or Opus encoding happens on the realtime
  audio thread. That work happens on a separate sender thread, matching the
  split on the PC daemon side between its PipeWire callback and its
  network/decode thread.
- Opus runs in `OPUS_APPLICATION_RESTRICTED_LOWDELAY` mode with in-band FEC
  enabled, so brief Wi-Fi packet loss is concealed instead of audible as a
  glitch (see `docs/PROTOCOL.md`).
