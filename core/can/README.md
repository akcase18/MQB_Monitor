# core/can/

Everything that knows this dashboard gets its numbers over a CAN bus: the UDS protocol
itself, the table describing which requests feed which signal, the scheduler that decides
what to ask for and when, and the decoder that turns answers back into stored values —
all of it behind one `core::ISignalSource`, so the composition root never sees any of it.

The dividing line with [core/vehicle](../vehicle/README.md) is deliberate and one-way.
`core/vehicle` owns *what a signal is* — its name, unit, display range, current value,
warning state. This directory owns *how one is obtained*. Nothing in `core/vehicle`
includes anything from here, which is what lets a second product acquire the same signals
from analog sensors without touching the catalog, the store, the warning evaluator, the
persisted config schema, or any of `gui/`.

## Files

- **`inc/uds_signal_source.hpp`** / **`src/uds_signal_source.cpp`** — `UdsSignalSource`,
  the facade and the only class here anything outside this directory constructs. Implements
  [`core::ISignalSource`](../vehicle/inc/signal_source.hpp):
  - `set_active(ids, count)` — forwards to the poller.
  - `tick(now_ms)` — drains responses, then sends at most one request. The order is the
    reason this is one call rather than two: a response landing this iteration frees the
    poller's single in-flight slot, so draining first lets the next request go out in the
    same pass instead of a loop later.
  - Owns the client, decoder and poller, and routes `UdsClient`'s C-style callbacks
    through `this`. Before it existed, `src/main.cpp` held all three separately, called two
    of them in a fixed order every loop, and routed responses through file-scope callbacks
    that reached back into the composition singleton — all UDS-specific knowledge living in
    the one file that is supposed to be able to swap acquisition wholesale.
  - Ignores `tick()`'s `now_ms`: the poller and decoder both already hold the same `IClock`
    and read it themselves. Timestamping off `now_ms` instead would shift every value's
    `last_update_ms` by however long the rest of the loop iteration took, which
    `core::Logger`'s on-change spacing would see.

- **`inc/uds_client.hpp`** / **`src/uds_client.cpp`** — `UdsClient`, the protocol:
  - `UdsRequestSpec { byte1, byte2, offset, scale }` — identifies one UDS
    data-by-identifier request and how to decode its raw payload (`(raw + offset) * scale`).
  - `MAX_REQUESTS_PER_SIGNAL` — how many requests may feed one signal. Lives here, not in
    the catalog, because it sizes UDS request tables.
  - `send(spec)` — sends a request for the given `(byte1, byte2)` identifier pair.
  - `poll(cb, ctx, ncb, nctx)` — drains every pending response on the bus, decodes each
    one (1-byte or 2-byte big-endian payload), and invokes `cb(ctx, byte1, byte2, raw)`
    for it. Structurally can't dereference anything on an unmatched response — there's no
    lookup table here to miss.
  - Negative responses (`03 7F <service> <nrc>`) go to the optional `ncb(nctx, service, nrc)`
    instead, and are dropped when it's null. They name no data identifier — the ECU only
    echoes the service it's rejecting — so only a caller tracking its own outstanding
    request can attribute one, which is exactly what `SignalPoller` does with it.

- **`inc/uds_binding.hpp`** / **`src/uds_binding.cpp`** — `UDS_BINDINGS`, the acquisition
  table. One `UdsSignalBinding` per catalog signal: `signal_id`, `cycle_time_ms`, up to
  `MAX_REQUESTS_PER_SIGNAL` `UdsRequestSpec`s, and a `transform` combining their decoded
  values into the signal's final value.
  - This is the half of the old `SignalSpec` that knew about CAN. Splitting it out is what
    removed `core/vehicle`'s dependency on this directory.
  - **`UDS_BINDINGS` is a parallel array to `core::SIGNAL_CATALOG`** — same length, same
    order, so entry *i* in each describes the same signal. Both are emitted by one
    filtered pass of [`tools/gen_catalog.py`](../../tools/gen_catalog.py) over
    `core/vehicle/data/mqb/signals.json`, so the correspondence holds by construction rather
    than by hand. `test_core_vehicle`'s `test_catalog_and_bindings_are_parallel` asserts
    it anyway, because `UdsDecoder` indexes the store by binding position on every
    response and would silently write the wrong signal if it ever drifted.
  - Only the `transform_*` functions are hand-written; a signal's JSON `"transform"` field
    names one of them. `transform_default` returns request 0's value; `sum`/`difference`/
    `average`/`min`/`max` are generic two-request combiners the monitor tool's Registry
    "New Compound Signal…" builder can emit without any new C++.
  - `uds_binding_find(signal_id)` for callers holding an id rather than an index.

- **`inc/uds_decoder.hpp`** / **`src/uds_decoder.cpp`** — `UdsDecoder`:
  - `on_response(byte1, byte2, raw, now_ms)` — finds every binding whose requests match
    the pair, applies that request's offset/scale, and once *all* of a signal's requests
    have reported for this cycle, runs its `transform` and writes the result to
    `core::SignalStore`.
  - This was `SignalStore::on_uds_response()`, and the per-request accumulators behind it
    were store members. Both moved here so the store's only write path is `set_at()` — a
    plain "somebody measured this" setter. That mattered structurally as well as
    aesthetically: `core/config`'s schema header reaches `signal_store.hpp` through
    `warning.hpp`, so a UDS include there leaked all the way into the persisted config.

- **`inc/signal_poller.hpp`** / **`src/signal_poller.cpp`** — `SignalPoller`, which owns
  *what to ask for and when*:
  - `set_active(signal_ids, count)` — points it at the displayed panel set. **Only
    displayed signals are polled.** Everything else is dead weight on the bus until the
    user puts it on a panel. Deduplicates DIDs across signals (Int Pres and Int Pres Act.
    both read `0x3E70`) at the faster of their cadences, since `UdsDecoder` already fans a
    single response out to every binding that owns the DID. Unchanged lists are a no-op.
  - `tick()` — sends **at most one request, and only when nothing is outstanding**. A UDS
    server handles one request at a time, so this is what stops the tail of a burst being
    silently dropped. Picks the most-overdue entry rather than walking in order, which is
    what keeps a slow signal from starving behind always-due fast ones.
  - `on_response()` / `on_negative_response()` — close the flow-control loop. A response
    frees the in-flight slot; so does a negative response, except NRC `0x78`
    (response-pending), which restarts the deadline instead. A DID that repeatedly times
    out or is rejected backs off to `BACKOFF_PERIOD_MS`.
  - Lived in `core/vehicle` until the acquisition split; it is a UDS request scheduler, so
    it belongs here.

## Usage

`src/main.cpp` constructs one `UdsSignalSource`, bound to the standard OBD2
enhanced-session arbitration IDs (`0x7E0` tx / `0x7E8` rx, 500 kbps), and calls `tick()`
once per `loop()` iteration through a `core::ISignalSource&`. Everything above — the
drain-then-send ordering, the callback routing, the flow-control loop — is internal to
this directory now.

Swapping the whole directory out for a different acquisition method is a change to the two
lines in `main.cpp` that name the concrete type. Nothing in `core/vehicle`, `core/config`
or `gui/` is involved.
