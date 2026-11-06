# platform/esp32/

The companion ESP32-C3 relay firmware — the other end of the `remote/protocol` wire link `remote/server` (on `env:teensymm`/`env:simulator`) already speaks. The relay itself is `esp32::RelayApp` (`relay_app.hpp`), which takes any `Stream`: `env:esp32c3` runs it over the UART to the Teensy, and the ESP32-S3 dash (`env:dash_s3`, [dash_s3/](../dash_s3/README.md)) compiles this same directory minus `main.cpp` and runs it in-process over an in-memory pipe, since that chip has its own Wi-Fi and needs no C3. Unlike `platform/teensy`/`platform/simulator`, this is **not** another implementation of [`platform.hpp`](../platform.hpp)'s `Platform` interface — the C3 has no display, touch, or CAN, so it isn't part of that abstraction at all. It's a standalone Arduino sketch with its own `main.cpp`, built as a separate PlatformIO env (`esp32c3`) that excludes the shared `src/main.cpp` and pulls in only `remote/protocol` (zero `core::`/`gui::` deps) plus this directory.

It drives the SoftAP up/down in response to `CMD_AP_ENABLE`/`CMD_AP_DISABLE` and replies `EVT_AP_READY`/`EVT_AP_DISABLED` — enough for `gui::WirelessSetupScreen`'s QR-code flow to work end to end — and serves the phone configurator once a phone joins the AP.

It also reports how many phones are joined (`EVT_AP_STATIONS`, a u8, read every 500 ms and sent when it changes). The dash greys its Wi-Fi glyph while that's 0. When it drops to 0 with the AP still up, the dash ends any preview and goes back to the QR code. A phone that turns its Wi-Fi off or joins another network leaves at once. One that walks out of range leaves when the AP drops it for silence, which `ApManager` shortens from ESP-IDF's 300 s to 30 s.

## The phone configurator

One static page, [`web/index.html`](web/index.html), with a mock of the dash at the top: the real panel layout for the chosen count and orientation, each panel drawn as Digital/Bar/Gauge in the chosen accent, with sample values. Under it, three tabs: **Dash** (panel count, orientation, theme, brightness — what affects the whole screen), **Panel** (the selected panel's signal, units, display mode, custom scale and warnings) and **Log** (the log profile: up to 16 signals the dash's Logging screen records when started with its **Profile** button — see [core/logging](../../core/logging/README.md)). The Log tab is one list of switches grouped by module (`MODULE_NAMES`, keyed by request id), each showing its listed poll rate, with search, an All / In profile filter, **Add on-screen signals** and **Clear**. Switching a signal only flips its switch — nothing above it changes height — and under In profile a signal switched off stays listed until the filter changes. A red "Busy bus" line, in a slot reserved whether or not it shows, appears when the panels plus the profile ask for more than `BUS_BUDGET_RPS` requests per second (a placeholder until the S3 is characterized; see TODO.md). The profile previews, saves and discards with the rest of the config; which button starts a capture is chosen on the dash, never here. Tapping a panel in the mock selects it and opens the Panel tab, the same flow as tapping a panel on the dash. A "Try a value" slider moves the selected signal's sample value so a warning can be seen tripping.

While the page is open the dash is in a **preview session**: the Teensy applies the page's config to the live config without saving, and shows the mock's sample values in place of live ones, so the real screen and the mock always match. Every edit (and the Try-a-value slider) re-sends the preview. **Save** writes the config and the session carries on; **Discard** goes back to the last save. The session ends — live values and the saved config come back — when the page is hidden or closed, when the dash hears nothing from it for 60 s (the page pings every 20 s), when the WiFi screen is closed, or when the AP times out. If it ended because the phone went (the page hidden or closed, the 60 s of silence, or the last phone leaving the AP), the panel goes back to the QR code, not the dashboard. The page tells its closing apart from Discard by sending `/api/revert` a one-byte body (`REVERT_PAGE_LEFT`); Discard's body is empty, and lands on the dashboard as before. Orientation is the exception to all of this — it's boot-time-only, so the mock shows it and the dash rotates on its next restart.

**Backup** (Dash tab) downloads the config as a readable JSON file (`mqb-monitor-config-<date>.json`) and loads one back. Loading only edits the page, like any tap, so the dash previews it and **Save** keeps it. Signals are matched by id, so a file still loads after the catalog changes, and ones the dash doesn't know are skipped. The values are metric whatever the panels show. The file also carries the per-layout 180° flips, which the page has no control for (they're set on the dash), so restoring a backup doesn't turn the panel upside down. A preview, save or discard that changes the active layout's flip turns the panel over live, the same as the dash's own Rotate button. The log profile is in it too (`log`, optional, so older files load as before). Logging on/off, which button started it, and the one-shot `open_orientation_on_boot` never reach the page, so they aren't in the file.

The C3 decodes nothing. The page speaks `remote/protocol`'s records itself (hex over HTTP, since `WebServer` returns a POST body as an Arduino `String`, which can't hold NULs), and the Teensy validates every batch:

| Route | Relays | Answers |
| --- | --- | --- |
| `GET /` | — | the page, gzipped |
| `GET /api/state` | `REQ_GET_STATE` | `RESP_STATE` payload, hex |
| `POST /api/preview` | `REQ_PREVIEW_CONFIG` (hex body: config, then sample values) | `ApplyResult`, as a digit |
| `POST /api/save` | `REQ_APPLY_CONFIG` (hex body) | `ApplyResult` |
| `POST /api/revert` | `REQ_REVERT_PREVIEW` (hex body: empty for Discard, `01` when the page is leaving) | `ApplyResult` |

The settings record grew a flips byte (bit 0 the landscape layout, bit 1 portrait), then a brightness-mode byte (0 Day, 1 Night, 2 Auto — the page's Brightness → Mode control) and Auto's slider (0–100, 50 = no shift; the page shows it as the shift, −25% to +25%). There is no protocol version, so a C3 (which carries the page) and a Teensy built before that change won't understand each other: flash both from the same tree.

That keeps every copy of the Teensy's tables out of the C3. The page does mirror a few (unit labels/conversions/decimals, the accent presets, `compute_layout()`, and `PanelWidget`'s geometry for the mock) — the comment at the top of `index.html` lists each one's source. Keep them in step when those change. Signal descriptions and Enum states aren't mirrored by hand: [`tools/gen_catalog.py`](../../tools/gen_catalog.py) writes them into the page from `signals.json`, keyed by id (the state frame has no room for them). That's what lets the page preview a state as one of its own labels.

## Files

- **`web/index.html`** — the configurator page, self-contained (a phone on the dash's AP has no internet, so nothing can come from a CDN). [`tools/web/embed_web.py`](../../tools/web/embed_web.py) gzips it into `web_index_gz.h` in the build directory on every `env:esp32c3`/`env:dash_s3` build (~12 KB). Its footer links to the S3 dash's firmware update page, but only once `/api/version` answers, which the C3 never does.
- **`inc/config_web.hpp`** / **`src/config_web.cpp`** — `ConfigWebHandler`: the routes above. Hex in, hex out, one relay round trip per request.
- **`inc/relay_app.hpp`** / **`src/relay_app.cpp`** — `RelayApp`: the relay's `setup()`/`loop()` as an object over a `Stream` — owns the `ApManager`, `RelayLink`, `WebServer` and `ConfigWebHandler`, answers `CMD_AP_ENABLE`/`CMD_AP_DISABLE`, and does the boot-time `WiFi.mode(WIFI_AP)` that avoids the lazy-init lwIP crash (see its comment). Shared by this firmware and the S3 dash. An optional `RelayExtension` adds board-specific routes and frame types; the S3's firmware updater (`DashS3Updater`) is one, and the C3 passes none.
- **`inc/relay_link.hpp`** / **`src/relay_link.cpp`** — `RelayLink`: the C3-side counterpart to `remote::server::RemoteConfigLink`. Feeds bytes from a `Stream&` through a `remote::protocol::FrameReader` and dispatches complete frames to a callback; `send_event()` is the outbound counterpart for `EVT_AP_READY`/`EVT_AP_DISABLED`. `send_request_and_wait()` is a separate *blocking* request/response helper for the `REQ_*` config messages — needed because an HTTP handler has to return its response body synchronously, unlike the fire-and-forget AP events. Never call it and `poll()` in the same call stack; it's only safe from inside a `WebServer` handler invoked from `loop()`.
- **`inc/ap_manager.hpp`** / **`src/ap_manager.cpp`** — `ApManager`: thin wrapper over `WiFi.softAP()`/`softAPdisconnect()`/`softAPIP()`/`softAPgetStationNum()`, plus the 30 s station inactive time.
- **`src/main.cpp`** — the transport only: `Serial1` on GPIO20/21 to the Teensy, with the enlarged RX ring `RESP_STATE` needs, handed to a `RelayApp`. Native USB-CDC is debug output.

## Working on the page

There's no local stand-in for the relay's web side: edit `web/index.html`, build, and load it from a real dash. The desktop simulator fakes the relay's Wi-Fi and update events for the panel's own screens (see [platform/simulator](../simulator/README.md#wi-fi-and-firmware-update-screens)), not the phone's page.

## Building and flashing

```sh
pio run -e esp32c3 -t upload
```

First build downloads the `espressif32` platform/toolchain (one-time, ~1GB) — expect it to be slow.

Releases come from `tools/release.py`, which puts this firmware in `releases/<version>/esp32c3/` with a `flash-usb.sh`, next to the Teensy `.hex` built from the same tree (see [platform/dash_s3](../dash_s3/README.md#releases)).

## Testing on hardware

1. Flash both: `pio run -e esp32c3 -t upload`, `pio run -e teensymm -t upload`.
2. On the dash, Settings → WiFi. Join the AP from a phone and open the address on the screen (or scan the QR code).
3. Joining the AP turns the dash's Wi-Fi glyph from grey to the accent. As the page loads, the dash should switch to the dashboard showing the same numbers as the mock, with "Tap screen to exit preview" along the top. Change something: it should follow within a fraction of a second.
4. **Discard** — the dash returns to the saved config. Change something again, then close the WiFi screen on the dash (tap the dashboard, then X) — the dash goes back to the saved config and live values. Same again, but lock the phone instead: the dash goes back to the QR code. Unlock it and the page's edits come back on the dashboard.
5. **Save**, then power-cycle the dash: the change survives.
6. With the page open, turn the phone's Wi-Fi off: the glyph goes grey and the dash goes back to the QR code, with the unsaved edits gone.
