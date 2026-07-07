# core/can/

`core::UdsClient` — the UDS (Unified Diagnostic Services) enhanced-diagnostic-session request/response protocol, layered over the platform-provided `ICanBus`. This is protocol logic only: it doesn't own any timers (the [core/time](../time/README.md) `Scheduler` decides *when* to send) and it doesn't know which signal a response belongs to (that's [core/vehicle](../vehicle/README.md)'s job).

## Files

- **`inc/uds_client.hpp`** / **`src/uds_client.cpp`** — `UdsClient`:
  - `UdsRequestSpec { byte1, byte2, offset, scale }` — identifies one UDS data-by-identifier request and how to decode its raw payload (`(raw + offset) * scale`).
  - `send(spec)` — sends a request for the given `(byte1, byte2)` identifier pair.
  - `poll(cb, ctx)` — drains every pending response on the bus, decodes each one (1-byte or 2-byte big-endian payload), and invokes `cb(ctx, byte1, byte2, raw)` for it. Structurally can't dereference anything on an unmatched response — there's no lookup table here to miss.

## Usage

`src/main.cpp` constructs one `UdsClient` bound to the standard OBD2 enhanced-session arbitration IDs (`0x7E0` tx / `0x7E8` rx, 500 kbps) and calls `poll()` once per `loop()` iteration. Every response it decodes is handed to `core::SignalStore::on_uds_response()` (see [core/vehicle](../vehicle/README.md)), which is where `(byte1, byte2)` actually gets matched back to a signal.
