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
not been established by these tests. Intermittent corrupt/missing serial replies
remain unresolved; acknowledgments and software stopping do not replace a
physical cutoff.

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

Stop writes `0x000A = 50`, then about one second later `0x0000 = 0`.
The isolated DT3 Stop actions did **not** clear `0x000F`. Deceleration is not
instantaneous: the original console's 4 MPH run took about 14 seconds to stop.
Confirm speed feedback and physical stop instead of equating a write echo with
an already stationary belt. No DT3 pause-register sequence was established;
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
`0x000F = 0`, `0x000B = 250` pair. Thus an unchanged display does not imply
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

## Capture provenance and reproduction

- `buttons-console-raw.jsonl.gz`: original-console request tap on pin 4.
- `buttons-base-raw.jsonl.gz`: base response tap on pin 3 in a **separate pass**.
- `speed-ramp-base-raw.jsonl.gz`: base acknowledgments during the labeled full ramp.
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
