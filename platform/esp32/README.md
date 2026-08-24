# platform/esp32/

The companion ESP32-C3 relay firmware — the other end of the `remote/protocol` wire link `remote/server` (on `env:teensymm`/`env:simulator`) already speaks. Unlike `platform/teensy`/`platform/simulator`, this is **not** another implementation of [`platform.hpp`](../platform.hpp)'s `Platform` interface — the C3 has no display, touch, or CAN, so it isn't part of that abstraction at all. It's a standalone Arduino sketch with its own `main.cpp`, built as a separate PlatformIO env (`esp32c3`) that excludes the shared `src/main.cpp` and pulls in only `remote/protocol` (zero `core::`/`gui::` deps) plus this directory.

It drives the SoftAP up/down in response to `CMD_AP_ENABLE`/`CMD_AP_DISABLE` and replies `EVT_AP_READY`/`EVT_AP_DISABLED` — enough for `gui::WirelessSetupScreen`'s QR-code flow to work end to end — and serves the phone-facing wireless config form once a phone joins the AP: `GET /` relays a `REQ_GET_STATE` and renders a plain HTML `<form>` (day/night brightness, panel count/orientation, accent color, per-panel signal/units/display-mode, and per-signal warning thresholds + custom display-range — including a metric/imperial toggle); `POST /save` relays the edits back as `REQ_APPLY_CONFIG` and redirects to `/` with a success/error banner. No JSON, no client-side JS — see the Phase 5 plan (`.claude/plans/immutable-jingling-balloon.md` if still present) for why.

## Files

- **`inc/relay_link.hpp`** / **`src/relay_link.cpp`** — `RelayLink`: the C3-side counterpart to `remote::server::RemoteConfigLink`. Feeds bytes from a `Stream&` through a `remote::protocol::FrameReader` and dispatches complete frames to a callback; `send_event()` is the outbound counterpart for `EVT_AP_READY`/`EVT_AP_DISABLED`. `send_request_and_wait()` is a separate *blocking* request/response helper for `REQ_GET_STATE`/`REQ_APPLY_CONFIG` — needed because an HTTP handler has to return its response body synchronously, unlike the fire-and-forget AP events. Never call it and `poll()` in the same call stack; it's only safe from inside a `WebServer` handler invoked from `loop()`.
- **`inc/ap_manager.hpp`** / **`src/ap_manager.cpp`** — `ApManager`: thin wrapper over `WiFi.softAP()`/`softAPdisconnect()`/`softAPIP()`.
- **`inc/unit_labels.hpp`** — Small presentation tables the config form needs (unit labels, metric↔imperial conversion, the accent-color preset list) duplicated here rather than linking `core::vehicle`/`gui::theme` into this env — see the file's own doc comment for why, and keep it in sync if those tables ever change on the Teensy side.
- **`inc/config_web.hpp`** / **`src/config_web.cpp`** — `ConfigWebHandler`: registers `GET /` and `POST /save` on the shared `WebServer` and does the relay/parse work described above; delegates the actual markup to `render_config_page()`.
- **`inc/config_page_render.hpp`** / **`src/config_page_render.cpp`** — `render_config_page()`: the config form's HTML, as a pure function of already-decoded wire state — takes plain data in, writes HTML chunks out via a callback, zero Arduino/WebServer dependency. Extracted out of `ConfigWebHandler` specifically so there's exactly one implementation shared by the real firmware and the host-native layout preview below, rather than two that could drift apart.
- **`tools/preview_main.cpp`** — host-native `main()` for `env:web_preview` (see below): fabricates sample wire state from the real `core::SIGNAL_CATALOG` and calls `render_config_page()` to produce a static `.html` file — a way to eyeball the form's layout without any hardware.
- **`src/main.cpp`** — `setup()`/`loop()`. Serial is native USB-CDC (this board has no separate UART bridge chip) — the same cable used to flash it also carries relay traffic for now. Once wired to a real Teensy, swap `Serial` for `Serial1` on two GPIOs so native USB is free for flashing/debug logs only.

## Previewing the form's layout (no hardware, no server)

`env:web_preview` is a small host-native build that runs the *actual* `render_config_page()` code against fabricated sample data (real signal names/units/ranges from `core::SIGNAL_CATALOG`, varied threshold/custom-range states so checked/unchecked and filled/empty fields both show up) and writes a static `.html` file — good for a quick layout/CSS check, but it's not interactive: there's no server behind it, so `Save` doesn't do anything. For that, use the full standalone test recipe below instead.

```sh
pio run -e web_preview
.pio/build/web_preview/program /path/to/output.html
open /path/to/output.html   # macOS
```

## Building and flashing

```sh
pio run -e esp32c3 -t upload
```

First build downloads the `espressif32` platform/toolchain (one-time, ~1GB) — expect it to be slow.

## Testing standalone (no Teensy, no display wiring)

Since the relay link rides the same USB-CDC connection used to flash the board, the desktop `simulator` build can drive a real, physically-flashed C3 exactly like it would a real Teensy — no extra hardware needed:

1. Flash it: `pio run -e esp32c3 -t upload`.
2. Find its device path: `ls /dev/tty.usbmodem*` (macOS).
3. Point the simulator's serial link at it and run: `MQB_REMOTE_SERIAL_PORT=/dev/tty.usbmodemXXXX pio run -e simulator -t exec` (see [`platform/simulator/src/simulator_serial_link.cpp`](../simulator/src/simulator_serial_link.cpp) — unset, this env var defaults to a no-op).
4. In the simulator, navigate Settings → WiFi. This sends a real `CMD_AP_ENABLE` frame to the C3; the C3 actually stands up a SoftAP; the simulator only shows the QR code once a real `EVT_AP_READY` comes back over the wire.
5. Join the AP from a phone (or a laptop — anything that can join an open Wi-Fi network) and hit `http://192.168.4.1/`. The form should show whatever's currently saved in the simulator's config file. Change something on-device first (e.g. a warning threshold) so you're editing real values, not just defaults.

This is a full round trip through real firmware — not just a build check.

## Testing the config form specifically

With the setup above running:

1. Confirm the form pre-fills with the simulator's actual current config.
2. Submit an edit and confirm the `/save` → redirect shows the change reflected, then check the simulator's persisted config file on disk to confirm the write actually landed (not just round-tripped through the page).
3. Toggle Metric/Imperial in the Signal Thresholds section and confirm a value entered in one system reads back correctly converted after reloading in the other (e.g. a Temp threshold set to 200°F should read back as ~93.3°C).
4. Try the custom display-range fields — there's no touchscreen equivalent to cross-check against (`core::SignalConfig::use_custom_range` has no on-device UI), so this is the first place to confirm it actually persists and round-trips.
5. Force a rejected save (e.g. edit a `<select>`'s value via browser devtools to something out of range) and confirm it surfaces as an error banner rather than a blank or crashed page.
