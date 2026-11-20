# LifeSpan TR1200 / DT3: observed serial control protocol

These captures extend the Omni/TR1200B notes with a physical TR1200 and its
DT3 console. They are observations from one treadmill/console combination,
not a universal LifeSpan specification. The code values below reproduce the
console's displayed MPH targets; they are not independently calibrated belt
speeds. Keep this profile separate from the existing Omni mapping.

A headless Python controller has reproduced initialization, Start, speed changes,
and Stop, including an empty-belt 1.6 MPH target held for five minutes and an
operator-confirmed stop. The original console was also captured through its
full displayed 0.4–4.0 MPH range. Direct controller operation above 1.6 MPH has
not been established by these tests. Acknowledgments and software stopping do
not replace a physical cutoff.

A later [simultaneous two-direction capture](#simultaneous-two-direction-capture-2026-11-19)
recorded requests, replies and DB9 pins 1 and 5–7 together with microsecond
timing. It corrects the Stop sequence below and narrows the key-status
findings.

## Wire format and registers

4800 baud, 8N1, 5 V TTL UART, Modbus RTU address 1. This DB9 is **not RS-232**.
Register/value fields are big-endian; CRC-16/MODBUS is transmitted low byte first.
FC03 reads return a byte count and values but no register address: associate a
reply with the observed request, and discard uncertain associations after loss.
FC06 writes normally return the exact request as acknowledgment on this DT3.

| Register | Observed use | Interpretation limit |
| --- | --- | --- |
| `0x0000` | Write 0 before Start and after Stop | Wider meaning unknown |
| `0x0001` | Write 1 during Start | Acknowledgment alone does not establish motion |
| `0x000A` | Speed target; value 50 for Stop | Use the DT3 lookup below |
| `0x000B` | 250 at boot/reset; 20 during Start | Units/meaning unknown; do not label as weight or watchdog |
| `0x000F` | Read base counter; write 0 during held Reset | Footfall-related counter; accuracy not calibrated |
| `0x0101` | Status-like value | Changes with key events; not simply a constant version |
| `0x010E` | Boot read, observed value `0x1008` | Wider meaning unknown |
| `0x0410` | Idle read | Wider meaning unknown |
| `0xD10A` | Speed feedback, zero when stopped | Base speed feedback is available; physical scale not calibrated |

## Mains initialization, Start, Stop, and Reset are separate

Mains on, without touching On/Start:

1. Read `0x010E`.
2. About one second later, write `0x000B = 250`.
3. Continue idle/status polling. The belt does not start.

On/Start from powered idle, at displayed 0.4 MPH:

1. Write `0x0000 = 0`.
2. About one second later, write `0x000A = 88`.
3. Write `0x0001 = 1`.
4. Write `0x000B = 20`.
5. Repeat `0x000A = 88`.

The final writes are approximately 70 ms apart. Background reads interleave;
their exact placement differs between captures. This is a captured sequence,
not proof of a minimal mandatory handshake.

Stop writes `0x000A = 50` once and lets the base coast down by itself, at
about 0.23 MPH per second. The console keeps reading `0xD10A` every 2 s and
writes `0x0000 = 0` only once a reading is below about 0.8 MPH:

| From | `0x000A = 50` → `0x0000 = 0` | `0xD10A` readings before the cut |
| --- | --- | --- |
| 4.0 MPH | 13.9 s | 855, 709, 550, 408, 291, 222, **181** |
| 1.6 MPH | 6.0 s | 362, 222, **133** |
| 0.4 MPH | 0.6 s | already low |

A reading of 222 never triggered the cut, so the threshold lies between 182 and
222. Speed reads 0 about 2 s after `0x0000 = 0`. The earlier "about one second
later" description only holds when stopping from the lowest speeds. The
isolated DT3 Stop actions did **not** clear `0x000F`. Confirm speed feedback
and physical stop instead of equating a write echo with an already stationary
belt. No DT3 pause-register sequence was established;
retain the original Omni pause observations as model-specific evidence.

Held Reset after a workout produced this six-write burst:

| Offset from first write | Register / value |
| --- | --- |
| 0 | `0x000F = 0` |
| ~70 ms | `0x000F = 0` |
| ~140 ms | `0x000B = 250` |
| ~210 ms | `0x000F = 0` |
| ~280 ms | `0x000B = 250` |
| ~1 s | `0x0000 = 0` |

When stats were already cleared, held Reset also produced a shorter
`0x000F = 0`, `0x000B = 250` pair. The two-direction capture shows that pair
repeating every 0.07–0.4 s for as long as the button is held (81 pairs in about
16 s), with no `0x0000 = 0`. A Reset after a workout repeated the six-write
burst above exactly. Thus an unchanged display does not imply
no command was sent. The DT3 reset acknowledgment echoed zero exactly;
the older Omni log's `0F00` value must not be treated as a DT3 success echo.

## Exact example frames

| Operation | Hexadecimal frame |
| --- | --- |
| Boot read | `01 03 01 0E 00 01 E4 35` |
| Boot setting | `01 06 00 0B 00 FA 78 4B` |
| Control 0 | `01 06 00 00 00 00 89 CA` |
| 0.4 MPH target | `01 06 00 0A 00 58 A8 32` |
| Start | `01 06 00 01 00 01 19 CA` |
| Start setting | `01 06 00 0B 00 14 F8 07` |
| Stop target | `01 06 00 0A 00 32 28 1D` |
| Counter read | `01 03 00 0F 00 01 B4 09` |
| Counter clear | `01 06 00 0F 00 00 B9 C9` |
| Actual speed read | `01 03 D1 0A 00 01 9D 34` |

## All 37 speed targets

[speed-targets.json](speed-targets.json) preserves the exact acknowledged values
and capture offsets for every displayed 0.1 MPH increment from 0.4 to 4.0.
Examples: 0.4→88, 0.5→117, 0.6→132, 1.0→235, 1.6→367, 4.0→940.
The Omni capture has 0.4→94 and 0.6→141, so one generic linear conversion
must not silently overwrite either console's target mapping.

## Key observations and connector measurements

The photographed treadmill has seven populated contacts, numbered 1–7.

| Pin | Observed role |
| --- | --- |
| 1 | Nominal 12 V supply; measurements vary with configuration/load |
| 2 | Signal ground |
| 3 | Base TX → console RX |
| 4 | Console TX → base RX |
| 5 | About 5 V; supply vs signal remains unknown |
| 6 | Key-related nominal 12 V signal; about 11.5 V inserted, 0 V removed in the reduced original-console setup |
| 7 | Measured near 0 V; function unknown, not established as ground |

These observations come from separate physical measurements, not voltage
samples in the serial files. The original switch's red lead connects to the
12 V supply; its black lead is a switched return, **not ground**. Its full PCB
path to pin 6 remains unmapped. Retaining original console pairs 1/2/6, with
its UART isolated, allowed direct UART control and preserved key signaling.

Paired active reads of `0x0101` changed from `0x2001` to `0x2201` after key
removal; `0x0201` also occurred during a moving key-pull test. Bit mask `0x0200`
is therefore more useful than matching one whole status word. Reinsertion did
not immediately clear it: normal console On/Start later restored `0x2001`.
It is a reported/latched condition, not proof of the key's current position.
The two-direction capture adds the console's side of this. On key removal pin 6
falls and the console immediately writes `0x0000 = 0` twice, then reads
`0x0101` ten times a second. On reinsertion pin 6 rises and the console writes
`0x000B = 250`, `0x000A = 50` and `0x0000 = 0`; `0x0101` stays `0x2201`. The bit
was still set at +111.03 s and clear at +113.24 s. Between those reads the
console wrote `0x0000 = 0`, `0x000A = 88`, `0x0001 = 1` and `0x000B = 20`, so the
On/Start burst clears it. `0x0000 = 0` alone does not (it followed reinsertion
without clearing the bit), which leaves `0x000A = 88`, `0x0001 = 1` or
`0x000B = 20`. No read separated them.
The software Stop preceded sampled zero speed in the key-pull test, so an
independent hardware cutoff with software unresponsive was **not isolated**.
These findings are not instructions to bypass the key or drive a TTL pin at 12 V.

## Counter validation limits

The original notes already identify `0x000F` as the base step counter. In our
five-minute empty-belt run it rose from 0 to 1 near startup, then stayed at 1.
In a walking trial it rose from 1 to 24 while the operator deliberately counted
20 footfalls; mounting the moving belt was an uncontrolled part of the trial.
This supports a footfall-related counter, not a calibration or fixed correction.
Treat failed reads as missing samples, and do not infer a real footstep from
an isolated increment. The raw files below document original-console buttons
and speed targets; these later walking/key results are operator observations.

## Simultaneous two-direction capture (2026-11-19)

A Flipper Zero tapped DB9 pins 1 and 3–7 through diode probes, with the stock
console driving the base. Edges were timestamped at 64 MHz and both 4800 8N1
directions decoded from them, so FC03 replies are paired exactly with their
requests. The operator pressed the buttons in the order shown; the display was
not recorded.

| File | Operator actions | Decoded |
| --- | --- | --- |
| `flipper-reset-key-start-power.log.gz` | Held Reset; key pulled and reinserted; Start and Stop at 0.4; mains off and on | 1,096 frames, 1 bad CRC (capture began mid-frame) |
| `flipper-speed-run-stop-walk.log.gz` | Start, + every ~3 s from 0.4 to 4.0, Stop, Reset, walk to 1.6, Stop | 2,223 frames, 0 bad CRC |

Findings beyond the Stop, Reset and key notes above:

- **Every FC06 write was echoed** by the base about 23 ms later.
- **Idle:** `0x0101` and `0x0410` are read every 2 s, `0xD10A` in between.
  While running, `0x000F` is read twice a second and `0xD10A` every 2 s. An
  unlisted register `0x000D` was written to 0 once while idle.
- **Speed targets:** all 37 written targets match [speed-targets.json](speed-targets.json).
  The console writes a new target at most once a second and combines quick
  presses (0.4 → 0.7 → 1.2 → 1.6 in 3 s).
- **Speed feedback tracks the target scale.** `0xD10A` settled within about 2%
  of the target code above 1.0 MPH (4.0 → 936–938) and up to about 14% off at
  0.4–0.9. That is agreement with the target codes, not a belt calibration.
- **The wire was clean at every speed** up to 4.0 MPH: 0 bad CRCs on the
  console ↔ base link. Intermittent corruption seen by a direct controller is
  therefore more likely in its adapter path than in the harness.
- **Counter:** `0x000F` rose by one at a time, about 1.3 per second while
  walking at 1.6 MPH (31 in 24 s, no hand count), and 3 times in the empty-belt
  ramp.
- **Power cycle:** at mains off pin 6 falls first and pin 1 falls 0.8 s later.
  At mains on, pin 3 shows 3.5 µs spikes at about 60 Hz for about 3 s before
  pin 1 returns. That is noise, not data. Then the console reads `0x010E`
  (`0x1008`) and writes `0x000B = 250` 1 s later, as in the mains sequence above.
- **Pins 5 and 7:** pin 5 read high and pin 7 low throughout, apart from 3 µs
  spikes on pin 7. A diode probe cannot tell high from floating.

Decode with the Flipper probe's `treadcap_decode.py --replay` (see
`../flipper/README.md` in the capture-tools contribution).

## Capture provenance and reproduction

- `buttons-console-raw.jsonl.gz`: original-console request tap on pin 4.
- `buttons-base-raw.jsonl.gz`: base response tap on pin 3 in a **separate pass**.
- `speed-ramp-base-raw.jsonl.gz`: base acknowledgments during the labeled full ramp.
- `flipper-*.log.gz`: simultaneous two-direction Flipper captures, unmodified.
- `capture-provenance.json`: hashes and the limited publication redactions.

USB device paths in session headers were redacted. Raw byte chunks, directions,
host timestamps, and operator markers are preserved. Separate-pass FC03 replies
cannot be exactly paired to another pass's requests. FC06 echoes identify their
register/value. Markers indicate instructions and subsequent operator reports,
not exact physical button-edge timestamps. Times are host receive times, not
logic-analyzer measurements.

To inspect without hardware: `gzip -dc buttons-console-raw.jsonl.gz | less`.
These are data/documentation contributions; they make no changes to firmware
behavior or existing Omni defaults.
