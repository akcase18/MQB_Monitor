# core/can/

`core::UdsClient` — the UDS (Unified Diagnostic Services) enhanced-diagnostic-session request/response protocol, layered over the platform-provided `ICanBus`. This is protocol logic only: it doesn't own any timers or request scheduling (`core::SignalPoller` in [core/vehicle](../vehicle/README.md) decides *what* to ask for and *when*) and it doesn't know which signal a response belongs to (also [core/vehicle](../vehicle/README.md)'s job).

## Files

- **`inc/uds_client.hpp`** / **`src/uds_client.cpp`** — `UdsClient`:
  - `UdsRequestSpec { byte1, byte2, offset, scale }` — identifies one UDS data-by-identifier request and how to decode its raw payload (`(raw + offset) * scale`).
  - `send(spec)` — sends a request for the given `(byte1, byte2)` identifier pair.
  - `poll(cb, ctx, ncb, nctx)` — drains every pending response on the bus, decodes each one (1-byte or 2-byte big-endian payload), and invokes `cb(ctx, byte1, byte2, raw)` for it. Structurally can't dereference anything on an unmatched response — there's no lookup table here to miss.
  - Negative responses (`03 7F <service> <nrc>`) go to the optional `ncb(nctx, service, nrc)` instead, and are dropped when it's null. They name no data identifier — the ECU only echoes the service it's rejecting — so only a caller tracking its own outstanding request can attribute one, which is exactly what `SignalPoller` does with it.

## Usage

`src/main.cpp` constructs one `UdsClient` bound to the standard OBD2 enhanced-session arbitration IDs (`0x7E0` tx / `0x7E8` rx, 500 kbps) and calls `poll()` once per `loop()` iteration, immediately before `SignalPoller::tick()` — drain first, send second, so a response arriving this iteration frees the poller's in-flight slot in time for it to send the next request in the same pass. Every response it decodes is handed to `core::SignalStore::on_uds_response()` (see [core/vehicle](../vehicle/README.md)), which is where `(byte1, byte2)` actually gets matched back to a signal, *and* to `SignalPoller`, which is what closes the flow-control loop.
