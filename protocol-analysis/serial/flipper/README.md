# Flipper Zero logic capture (`treadcap`)

A receive-only capture of the console ↔ base DB9 harness using a Flipper Zero
and six ordinary diodes. It timestamps every level change on DB9 pins 3, 4, 6
and 7 with the Flipper's 64 MHz cycle counter, polls pins 1 and 5, and streams
the events over USB. `../tools/treadcap_decode.py` decodes both serial
directions from those timings and prints Modbus frames beside the line levels,
so one capture records requests, replies and the power/key lines together.

The console stays connected and drives the base normally. The Flipper never
drives a harness pin.

## Wiring: one diode per pin

Each probe is a diode with its **stripe (cathode) on the DB9 pin** and its plain
end (anode) on the Flipper pin. The Flipper's internal pull-up holds its pin at
3.3 V. A low DB9 line pulls it low through the diode; a high line, 5 V or 12 V,
reverse-biases the diode, so the Flipper pin never sees more than its own 3.3 V.
This reads high/low only, not voltage. "High" also means "not connected".

| DB9 pin | Role (DT3 console, TR1200 base) | Flipper header pin |
|---|---|---|
| 1 | +12 V from the base while the console is awake | 2 (PA7), polled |
| 2 | Ground | 18 (GND), plain wire, no diode |
| 3 | Base TX → console RX, 5 V TTL | 6 (PB2) |
| 4 | Console TX → base RX, 5 V TTL | 4 (PA4) |
| 5 | About 5 V, purpose unknown | 7 (PC3), polled |
| 6 | Safety-key line: about 12 V key in, 0 V key out | 15 (PC1) |
| 7 | Low in every capture so far | 16 (PC0) |

Rules that protect the Flipper:

- Use rectifier diodes with a printed part number such as **1N4007** or
  1N5819. **Do not use small glass diodes from an assortment**: Zener diodes
  look the same and conduct in reverse at a few volts, which would put 12 V
  from pins 1 or 6 on the Flipper.
- A reversed diode also passes 12 V. Before connecting the treadmill, put every
  stripe on Flipper ground, start `treadcap`, and check that every channel
  reads low (`S` lines show `000000`). Then move only the stripe ends, one at a
  time, to their DB9 pins.
- Leave Flipper pins 1 (5 V), 9 (3.3 V), 10 and 12 (SWD), 13 and 14 (USART)
  unconnected. Ground is the only direct connection to the harness.

## Build and install

`treadcap` is an external CLI command (a `.fal` plugin), built with
[ufbt](https://pypi.org/project/ufbt/) against the SDK of the firmware on the
Flipper. The API version must match. It was developed on Momentum `mntm-010`
(API 86.0):

```sh
python3 -m pip install ufbt
ufbt update --hw-target=f7 \
  --url=https://github.com/Next-Flip/Momentum-Firmware/releases/download/mntm-010/flipper-z-f7-sdk-mntm-010.zip
cd protocol-analysis/serial/flipper/treadcap && ufbt
```

Copy `dist/cli_treadcap.fal` to `/ext/apps_data/cli/plugins/` on the SD card
(qFlipper, or `storage write_chunk` over the CLI), then run `reload_ext_cmds`
in the Flipper CLI. `treadcap` should now appear in `help`. `treadcap.fap` is
only the build parent required for plugins; it does nothing.

## Capture and decode

```sh
python3 protocol-analysis/serial/tools/treadcap_decode.py --seconds 120
python3 protocol-analysis/serial/tools/treadcap_decode.py --replay treadcap-20261119-221810.log
```

Capturing needs `pyserial`; replay needs only the standard library. The raw
Flipper output is saved (exclusive create, never overwritten) and can be decoded
again later. Close qFlipper first so the CLI port is free.

Raw line format, one event per line:

- `E<db9><level> <cycles>`: a level change; cycles is 32-bit hex at 64 MHz and
  wraps every ~67 s (the decoder unwraps it).
- `S <cycles> <levels> <dropped>`: once a second, the levels of DB9 1, 3, 4, 5,
  6, 7 and the count of events lost to a full buffer.

## Design notes

- Interrupts use EXTI lines 0, 1, 2 and 4. Line 7 is taken by the expansion-module
  detector (registering it halts the firmware), and the OK and Down buttons use
  lines 3 and 6. That is why DB9 1 and 5 are polled at 1 kHz and DB9 3 uses
  PB2 rather than PA6.
- The hardware UARTs are not used: enabling LPUART would drive pin 15 (PC1),
  which here is on the key line.
- The decoder samples each bit at its middle, groups frames on the 3.5-character
  Modbus gap, and ignores low pulses shorter than a quarter bit. Those appear at
  mains frequency on DB9 pin 3 while the base powers up.
- Flipper JavaScript was too slow for this: about 360 pin reads per second.
