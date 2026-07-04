# Hardware Telemetry Protocol & Node Ecosystem (design)

Status: **design / not yet built.** A firmware library (ESP32-first) that makes any microcontroller
a self-describing, plug-and-play telemetry source KRStudio reads over USB serial; its channels
become node-graph inputs; processing nodes clean them; the result feeds the actuator state observer
(see ACTUATOR_MODEL.md). This is the grown-up, self-describing, typed version of Firmata (kin to
IEEE-1451 TEDS / Foxglove-MCAP channel announcement).

## Two-level identity: the board vs. what's plugged into it

- **Board UUID** — burned to NVS/eFuse on FIRST flash; survives reflashing, sensor swaps,
  everything. Identifies the physical board; restores its saved config + scene role on reconnect.
- **Capability descriptor + its own hash** — the mutable set of channels currently offered (which
  sensors are attached, their units/formats/ranges/calibration). A new sensor → new descriptor
  hash → re-announce. This is the same content-addressed recognize-as-different pattern as
  `.kscene`/parts: recognize the board, detect config change.

On connect: `board=<uuid>, capabilities=<hash>, channels=[...]`.

## Split transport (the load-bearing, boring part)

Do NOT stream JSON at rate. Two channels:

- **Announce/config** — JSON or CBOR, request/response, versioned (`ktp/1`). Carries the capability
  descriptor and accepts reconfiguration (enable channel X at N Hz, set gain Y). This is the
  "declare I/O like function arguments" surface: PC sends config → board re-announces.
- **Telemetry stream** — compact BINARY, one framed packet per sample-batch: `channelId + values`,
  NO field names on the wire (the handshake mapped id→name/unit). Frame with **COBS** (consistent-
  overhead byte stuffing) so a dropped byte / mid-stream reconnect resynchronizes on the next zero
  delimiter instead of corrupting the pipeline.

**Bidirectional from day one** — the PC pushes config and (later) setpoints, the path to hardware-
in-the-loop. Same shape as the existing `HilBridges` / CAN bridge, over USB-CDC instead of CAN.

## USB "system device" without driver hell

Don't build a custom USB class (Windows driver pain). Use **USB-CDC** (driverless virtual serial
everywhere); on ESP32-S3 native USB set a friendly VID/PID + product string so it shows as
"KRStudio Telemetry Node" in the OS. Do the REAL capability announcement at the application layer.
Discovery = scan serial ports, send a probe magic-byte, protocol-speaking boards answer.

## Time is the make-or-break

USB-CDC latency is tens of ms with jitter and batches — arrival time at the PC is useless for
correlation. So:

- **Device-side timestamp** every sample (monotonic microsecond counter) IN the packet.
- **Host clock-sync estimator** continuously fits device-clock → host-clock (lightweight linear/
  Kalman regression of device-time vs. host-arrival, same idea as PTP / rosbag sender-clock
  reconciliation).

Then all downstream data lives on one coherent timeline and heat/torque/velocity correlation is
real, not smeared. This is the #1 thing to get right.

## Ingestion: one thread, a ring, then it's just nodes

Serial I/O on its OWN thread (never GUI/eval) → lock-free SPSC ring buffer. The eval loop (60 Hz+
`evaluateGraphQuiet`, decoupled via `NodeEditQueue`, proven by `ThreadGate`) consumes latest each
pass. The hardware reader is just another producer feeding the graph — no new concurrency model.

## Channels → source nodes → existing processing → the observer

- Each announced channel → a **hardware source node** (output = raw value + declared units +
  timestamp).
- Route through the EXISTING processing nodes — Kalman/low-pass/moving-average (GATE FILTER),
  sensor noise/uncertainty models, IMU Allan-variance, L2-uncertainty. The raw→clean pipeline is
  mostly WIRING existing nodes, not new ones.
- Clean telemetry feeds the gray-box actuator **state observer** (ACTUATOR_MODEL.md): model =
  process model, telemetry = measurements, filter → hidden state (winding temp, torque-curve
  position, backlash slack). The ghost-validity robot renders it.

## Division of labor (soft real-time, not RTOS)

Fast inner loops (commutation, torque loop) stay on the MCU (microsecond timing). Characterization,
estimation, uncertainty, logging, the physics observer run on the PC. USB-CDC realistically:
~1 kHz streaming, ms-scale latency — plenty for thermal (slow), torque/velocity tracking (into
hundreds of Hz), and an OUTER setpoint loop; NOT a current loop. Make this boundary explicit so no
one closes a stability-critical loop over USB.

## Firmware library ergonomics (its whole value)

Exposing a sensor must be a one-liner, e.g. conceptually:
`telemetry.channel("motor_current", Amps, &readCurrent, 1000 /*Hz*/);`
and the library invisibly handles the UUID, handshake, descriptor, COBS framing, device
timestamps, and the reconfig RPC. ESP32 first (native USB, NVS, RAM); keep the core transport
MCU-agnostic so Arduino/STM32/Teensy is a thin HAL shim. If it's harder than `Serial.println`,
nobody uses it; if it Just Shows Up in KRStudio with correct units, it's the sticky thing.

## Honest hard parts

- **Hot-plug/disconnect** — mid-stream unplug, buffer overrun, brownout reboot mid-packet. COBS
  resync + heartbeat/watchdog + graceful node "stale" state (reuse PropertyCatalog's stale-aware
  frequency).
- **Backpressure** — PC stall → board buffer overflow. Policy: drop-oldest for telemetry; the board
  REPORTS drops so you know the stream had gaps.
- **Calibration ownership** — keep the board dumb (raw counts + declared nominal scale); do real
  calibration on the PC (versionable, improvable) — same philosophy as keeping physics/uncertainty
  on the host.
- **Trust** — fine for your own hardware; if public, a rogue descriptor must only be able to define
  channels, nothing more. Note, not a day-1 concern.

## Phased build (each independently useful)

1. **Transport + handshake + one hardcoded channel** — COBS framing, device UUID, JSON announce,
   device timestamps + host clock-sync. Gate: a simulated/loopback device round-trips a descriptor
   + timestamped stream; clock-sync recovers a known offset.
2. **Channels → source nodes + reconfiguration** — announced channels auto-populate node outputs;
   PC enable/disable/re-rate. Gate: a fake device's channels appear as nodes with correct units; a
   reconfig RPC changes the stream.
3. **Processing pipeline wiring** — sources through existing filter/noise/uncertainty nodes to a
   clean estimate. Mostly integration.
4. **State observer** — gray-box model as a filter, telemetry as measurements, real-time hidden
   state out, rendered on the ghost robot. Loop closure with the actuator ecosystem.

Steps 1-2 = plug-a-board-in-and-see-live-data (great standalone milestone). Step 4 = the real-2-sim
faithful thing.

## Freeze before flashing firmware

Nail the **wire protocol spec** (framing, announce schema, timestamp/clock-sync, reconfig RPC)
before any firmware ships — the library and PC ingestion both depend on it and you don't want
version churn after boards are flashed in the field.
