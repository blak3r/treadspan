# Passive serial capture and offline decoding

Python 3.10+. Offline decoding uses only the standard library. Install
`pyserial>=3.5` only when recording from USB adapters. These tools do not issue
motor commands or emulate a console.

```sh
python3 protocol-analysis/serial/tools/capture.py decode \
  protocol-analysis/serial/treadmill-serial-comms-log.txt --frames
python3 -m unittest discover -s protocol-analysis/serial/tools/tests -v
```

For passive recording keep the original console and physical safety circuit
connected, with all original DB9 connections passed through. A single RX adapter
can observe one direction. Two independent RX inputs capture both directions:

- Console requests: adapter RX → DB9 pin 4.
- Base replies: second adapter RX → DB9 pin 3.
- Both adapter grounds → DB9 pin 2.
- Leave both adapters' TX, VCC and handshake pins disconnected.

This is 4800 baud, 8N1, **5 V TTL, not RS-232**. Verify your adapter's input
voltage tolerance and connector orientation. Do not connect the DB9 power/key
lines to USB adapter logic pins. Do not wire a generic RS-232 adapter here.

```sh
python3 protocol-analysis/serial/tools/capture.py record \
  --console-port /dev/cu.usbserial-CONSOLE \
  --base-port /dev/cu.usbserial-BASE \
  --seconds 90 --interactive --output buttons.jsonl
python3 protocol-analysis/serial/tools/capture.py timeline buttons.jsonl --only-writes
```

Each output file is exclusively created; existing evidence is not overwritten.
Type labels after physical button actions; `q` ends recording. These markers
and raw chunks have host monotonic timestamps, not exact wire-edge times.
`--console-port` or `--base-port` alone is also supported. One physical port
cannot be used for both directions. Duration must be positive.

`decode` accepts the project's existing decimal REQ/RESP logs, timestamped JSONL
captures, and legacy Python TX/RX logs. It ignores already-decoded duplicate
lines. Independent incremental decoders preserve stream direction and session
boundaries, validate Modbus CRCs, handle fragmentation and noise, and report
exception frames rather than treating them as successful reads.

FC03 replies contain no register address: a decoded reply value alone is not
proof that it represents steps or speed. Associate it with a known request;
separate capture passes cannot be paired exactly. FC06 requests and echoes
are byte-identical, so direction matters. A legacy one-wire LISTEN is marked
mixed/unknown; legacy SNIFF logs came from an active emulator and must not be
reclassified as clean passive observations. The tool does not guess missing
bytes or compensate for electrical corruption.

For `.gz` evidence, decompress to a new local `.jsonl` file before decoding.
Tests use known wire vectors and mock USB fragments, never a real serial port.
