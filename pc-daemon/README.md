# micify-daemon (PC side)

Receives the phone's Opus audio stream and exposes it as a virtual
microphone. Built and verified on this machine (PipeWire 1.6.8):

```bash
make
./micify-daemon --port 44551          # Wi-Fi (UDP)
./micify-daemon --port 44551 --usb    # USB (TCP, behind `adb forward`)
```

The virtual mic ("Micify (phone mic)") only appears once the phone sends its
first packet - the daemon lazily creates it from the format-announce packet.
Check with:

```bash
wpctl status
```

## USB mode

```bash
adb forward tcp:44551 tcp:44551
./micify-daemon --port 44551 --usb
```

Then point the phone app at USB mode on port 44551 (it connects to its own
`127.0.0.1:44551`, which `adb forward` tunnels to this port on the PC).

## Windows

Not build-tested here (no MSVC/MinGW on this dev machine). See
`src/backend_windows_wasapi.c` and `docs/ARCHITECTURE.md` for the intended
approach: writing into the endpoint exposed by the existing MIT-licensed
[Virtual-Audio-Driver](https://github.com/VirtualDrivers/Virtual-Audio-Driver)
rather than shipping a separate signed kernel driver.

## Testing without a phone

`tools/test_sender.c` stands in for the Android app: it generates a sine
wave, encodes it with Opus, and sends it using the same wire protocol.

```bash
gcc tools/test_sender.c -o tools/test_sender $(pkg-config --cflags --libs opus) -lm
./tools/test_sender 127.0.0.1 44551 440 10   # host port freq_hz seconds
```
