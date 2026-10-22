# platform/esp32/

The companion ESP32-C3 relay firmware — the other end of the `remote/protocol` wire link `remote/server` (on `env:teensymm`/`env:simulator`) already speaks. The relay itself is `esp32::RelayApp` (`relay_app.hpp`), which takes any `Stream`: `env:esp32c3` runs it over the UART to the Teensy, and the ESP32-S3 dash (`env:dash_s3`, [dash_s3/](../dash_s3/README.md)) compiles this same directory minus `main.cpp` and runs it in-process over an in-memory pipe, since that chip has its own Wi-Fi and needs no C3. Unlike `platform/teensy`/`platform/simulator`, this is **not** another implementation of [`platform.hpp`](../platform.hpp)'s `Platform` interface — the C3 has no display, touch, or CAN, so it isn't part of that abstraction at all. It's a standalone Arduino sketch with its own `main.cpp`, built as a separate PlatformIO env (`esp32c3`) that excludes the shared `src/main.cpp` and pulls in only `remote/protocol` (zero `core::`/`gui::` deps) plus this directory.

It drives the SoftAP up/down in response to `CMD_AP_ENABLE`/`CMD_AP_DISABLE` and replies `EVT_AP_READY`/`EVT_AP_DISABLED` — enough for `gui::WirelessSetupScreen`'s QR-code flow to work end to end — and serves the phone configurator once a phone joins the AP.

## The phone configurator

One static page, [`web/index.html`](web/index.html), with a mock of the dash at the top: the real panel layout for the chosen count and orientation, each panel drawn as Digital/Bar/Gauge in the chosen accent, with sample values. Under it, two tabs: **Dash** (panel count, orientation, theme, brightness — what affects the whole screen) and **Panel** (the selected panel's signal, units, display mode, custom scale and warnings). Tapping a panel in the mock selects it and opens the Panel tab, the same flow as tapping a panel on the dash. A "Try a value" slider moves the selected signal's sample value so a warning can be seen tripping.

While the page is open the dash is in a **preview session**: the Teensy applies the page's config to the live config without saving, and shows the mock's sample values in place of live ones, so the real screen and the mock always match. Every edit (and the Try-a-value slider) re-sends the preview. **Save** writes the config and the session carries on; **Discard** goes back to the last save. The session ends — live values and the saved config come back — when the page is hidden or closed, when the dash hears nothing from it for 60 s (the page pings every 20 s), when the WiFi screen is closed, or when the AP times out. Orientation is the exception to all of this — it's boot-time-only, so the mock shows it and the dash rotates on its next restart.

The C3 decodes nothing. The page speaks `remote/protocol`'s records itself (hex over HTTP, since `WebServer` returns a POST body as an Arduino `String`, which can't hold NULs), and the Teensy validates every batch:

| Route | Relays | Answers |
| --- | --- | --- |
| `GET /` | — | the page, gzipped |
| `GET /api/state` | `REQ_GET_STATE` | `RESP_STATE` payload, hex |
| `POST /api/preview` | `REQ_PREVIEW_CONFIG` (hex body: config, then sample values) | `ApplyResult`, as a digit |
| `POST /api/save` | `REQ_APPLY_CONFIG` (hex body) | `ApplyResult` |
| `POST /api/revert` | `REQ_REVERT_PREVIEW` | `ApplyResult` |

That keeps every copy of the Teensy's tables out of the C3. The page does mirror a few (unit labels/conversions/decimals, the accent presets, `compute_layout()`, and `PanelWidget`'s geometry for the mock) — the comment at the top of `index.html` lists each one's source. Keep them in step when those change.

## Files

- **`web/index.html`** — the configurator page, self-contained (a phone on the dash's AP has no internet, so nothing can come from a CDN). [`tools/web/embed_web.py`](../../tools/web/embed_web.py) gzips it into `web_index_gz.h` in the build directory on every `env:esp32c3` build (~12 KB).
- **`inc/config_web.hpp`** / **`src/config_web.cpp`** — `ConfigWebHandler`: the routes above. Hex in, hex out, one relay round trip per request.
- **`inc/relay_app.hpp`** / **`src/relay_app.cpp`** — `RelayApp`: the relay's `setup()`/`loop()` as an object over a `Stream` — owns the `ApManager`, `RelayLink`, `WebServer` and `ConfigWebHandler`, answers `CMD_AP_ENABLE`/`CMD_AP_DISABLE`, and does the boot-time `WiFi.mode(WIFI_AP)` that avoids the lazy-init lwIP crash (see its comment). Shared by this firmware and the S3 dash.
- **`inc/relay_link.hpp`** / **`src/relay_link.cpp`** — `RelayLink`: the C3-side counterpart to `remote::server::RemoteConfigLink`. Feeds bytes from a `Stream&` through a `remote::protocol::FrameReader` and dispatches complete frames to a callback; `send_event()` is the outbound counterpart for `EVT_AP_READY`/`EVT_AP_DISABLED`. `send_request_and_wait()` is a separate *blocking* request/response helper for the `REQ_*` config messages — needed because an HTTP handler has to return its response body synchronously, unlike the fire-and-forget AP events. Never call it and `poll()` in the same call stack; it's only safe from inside a `WebServer` handler invoked from `loop()`.
- **`inc/ap_manager.hpp`** / **`src/ap_manager.cpp`** — `ApManager`: thin wrapper over `WiFi.softAP()`/`softAPdisconnect()`/`softAPIP()`.
- **`src/main.cpp`** — the transport only: `Serial1` on GPIO20/21 to the Teensy, with the enlarged RX ring `RESP_STATE` needs, handed to a `RelayApp`. Native USB-CDC is debug output.

## Working on the page (no C3, no Teensy)

[`tools/web/dev_server.py`](../../tools/web/dev_server.py) stands in for the C3 on a laptop: it serves `web/index.html` straight from disk (edit, reload) and relays `/api/*` over a pseudo-terminal to the desktop simulator, which runs the real Teensy-side code — so previews show up in the simulator window.

```sh
pio run -e simulator
python3 tools/web/dev_server.py --sim     # launches the simulator on the pty too
open http://localhost:8080/
```

It also answers the simulator's `CMD_AP_ENABLE`, so Settings → WiFi works. Note that a Save writes the simulator's `sim_config.bin`.

## Building and flashing

```sh
pio run -e esp32c3 -t upload
```

First build downloads the `espressif32` platform/toolchain (one-time, ~1GB) — expect it to be slow.

## Testing on hardware

1. Flash both: `pio run -e esp32c3 -t upload`, `pio run -e teensymm -t upload`.
2. On the dash, Settings → WiFi. Join the AP from a phone and open the address on the screen (or scan the QR code).
3. As the page loads, the dash should switch to the dashboard showing the same numbers as the mock. Change something: it should follow within a fraction of a second.
4. **Discard** — the dash returns to the saved config. Change something again, then close the WiFi screen on the dash (tap the dashboard, then X) — the dash goes back to the saved config and live values. Same again, but lock the phone instead.
5. **Save**, then power-cycle the dash: the change survives.
