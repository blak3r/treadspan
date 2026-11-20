#!/usr/bin/env python3
"""Passive two-direction captures and offline analysis of current/legacy logs."""

import argparse
import collections
import datetime
import json
import re
import select
import sys
import threading
import time
from pathlib import Path

from lifespan_modbus import FrameDecoder, describe_frame


def iter_log_chunks(path):
    """Yield (session, direction, bytes) from raw data only, never parsed duplicates.

    Legacy LISTEN could have been wired to either direction; keep it 'mixed'.
    Legacy SNIFF actively emulated a console and is marked as such in sessions.
    """
    session = 0
    for line in Path(path).read_text().splitlines():
        if line.startswith("{"):
            event = json.loads(line)
            if event.get("type") == "session":
                session += 1
            elif event.get("type") == "bytes":
                yield session, event["direction"], bytes.fromhex(event["hex"])
            continue
        if "=== " in line and "END " not in line:
            session += 1
        match = re.search(r"LISTEN RX \(\d+ bytes\): ([0-9A-Fa-f ]+)", line)
        if match:
            yield session, "mixed", bytes.fromhex(match[1])
            continue
        match = re.search(r"SNIFF RAW \(\d+b\): ([0-9A-Fa-f ]+)", line)
        if match:
            yield session, "request", bytes.fromhex(match[1])
            continue
        match = re.search(
            r"\b(TX|RX RAW) [^:]+: ([0-9A-Fa-f ]+?)(?: \(dec:.*\))?$", line
        )
        if match:
            yield (
                session,
                "request" if match[1] == "TX" else "response",
                bytes.fromhex(match[2]),
            )
            continue
        # TreadSpan's decimal logs use separate directional UARTs.
        match = re.search(r"(REQ :|RESP:)\s*([\d ]+)", line)
        if match:
            yield (
                session,
                "request" if match[1] == "REQ :" else "response",
                bytes(map(int, match[2].split())),
            )


def decode_log(path):
    decoders = {}
    for session, direction, chunk in iter_log_chunks(path):
        key = session, direction
        decoder = decoders.setdefault(key, FrameDecoder(direction))
        for frame in decoder.feed(chunk):
            yield session, direction, frame


def analyze(path, show_frames=False):
    counts = collections.Counter()
    total = 0
    for session, direction, frame in decode_log(path):
        total += 1
        description = describe_frame(frame, direction)
        counts[direction, description] += 1
        if show_frames:
            print(
                f"session={session} {direction}: {frame.hex(' ').upper()}  {description}"
            )
    print(f"{total} CRC-valid frames from raw chunks; session boundaries kept separate")
    print(
        "Legacy LISTEN direction is unknown; legacy SNIFF includes active emulation traffic."
    )
    for (direction, description), count in sorted(counts.items()):
        print(f"{count:6d} {direction:8s} {description}")


def timeline(path, only_writes=False):
    """Show operator markers and decoded frames in recorded host-chunk order.

    USB read completion timestamps are not individual UART-byte timestamps.
    Do not infer exact wire timing or pair FC03 replies from this order alone.
    """
    decoders = {}
    origin = None
    for line_number, line in enumerate(Path(path).read_text().splitlines(), 1):
        try:
            event = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(
                "Timeline requires a JSONL capture; use decode for legacy logs"
            ) from error
        if event.get("type") == "session":
            decoders.clear()
            origin = event.get("time_ns")
            yield f"SESSION {event.get('utc', '')}"
            continue
        timestamp = event.get("time_ns")
        if origin is None and timestamp is not None:
            origin = timestamp
        elapsed = (
            f"+{(timestamp - origin) / 1e9:9.3f}s"
            if timestamp is not None and origin is not None
            else f"line {line_number}"
        )
        if event.get("type") == "marker":
            yield f"{elapsed} MARK {event['label']}"
        elif event.get("type") == "bytes":
            direction = event["direction"]
            decoder = decoders.setdefault(direction, FrameDecoder(direction))
            for frame in decoder.feed(bytes.fromhex(event["hex"])):
                if not only_writes or frame[1] == 6:
                    yield f"{elapsed} {direction:8s} {frame.hex(' ').upper()}  {describe_frame(frame, direction)}"


def _wait_with_markers(stopped, duration, record, interactive):
    deadline = time.monotonic() + duration
    while not stopped.is_set():
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        if not interactive:
            stopped.wait(remaining)
            break
        ready, _, _ = select.select([sys.stdin], [], [], min(remaining, 0.1))
        if not ready:
            continue
        label = sys.stdin.readline()
        if not label:  # EOF disables labels without interrupting the recording.
            interactive = False
            continue
        label = label.strip()
        if label.lower() == "q":
            break
        if label:
            record({"type": "marker", "time_ns": time.monotonic_ns(), "label": label})
            print(f"Marked: {label}")


def capture_ports(console_port, base_port, output, duration, interactive=False):
    # Imported only for real capture: offline decoding requires no pyserial.
    import serial

    if duration <= 0:
        raise ValueError("Duration must be positive")
    if not console_port and not base_port:
        raise ValueError("Specify at least one console or base receive port")
    if base_port and base_port == console_port:
        raise ValueError("Each direction needs its own RX adapter")
    stopped = threading.Event()
    lock = threading.Lock()
    errors = []
    threads = []
    ports = []
    # Exclusive creation protects old capture evidence from being overwritten.
    with open(output, "x") as destination:

        def record(event):
            with lock:
                destination.write(json.dumps(event) + "\n")
                destination.flush()

        record(
            {
                "type": "session",
                "utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                "time_ns": time.monotonic_ns(),
                "console_port": console_port,
                "base_port": base_port,
                "passive": True,
            }
        )

        def receive(port, direction):
            try:
                while not stopped.is_set():
                    data = port.read(max(1, min(256, port.in_waiting)))
                    if data:
                        record(
                            {
                                "type": "bytes",
                                "time_ns": time.monotonic_ns(),
                                "direction": direction,
                                "hex": data.hex(" "),
                            }
                        )
            except (serial.SerialException, OSError) as error:
                errors.append(error)
                stopped.set()

        try:
            for name, direction in ((console_port, "request"), (base_port, "response")):
                if not name:
                    continue
                port = serial.Serial(name, 4800, timeout=0.05, exclusive=True)
                ports.append(port)
                thread = threading.Thread(target=receive, args=(port, direction))
                threads.append(thread)
                thread.start()
            print(
                "Passive capture: adapter RX + GND only. Keep the original console connected."
            )
            if interactive:
                print(
                    "Type an event label and Enter after each physical action; q ends capture."
                )
            _wait_with_markers(stopped, duration, record, interactive)
        except KeyboardInterrupt:
            pass
        finally:
            stopped.set()
            for thread in threads:
                thread.join()
            for port in ports:
                port.close()
        if errors:
            raise OSError(f"Capture interrupted: {errors[0]}")
    print(f"Saved {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    decode = commands.add_parser(
        "decode", help="Analyze logs without accessing hardware"
    )
    decode.add_argument("path")
    decode.add_argument("--frames", action="store_true")
    trace = commands.add_parser(
        "timeline", help="Show JSONL event labels and frames in host-chunk order"
    )
    trace.add_argument("path")
    trace.add_argument(
        "--only-writes",
        action="store_true",
        help="Keep event labels and FC06 writes/acknowledgments",
    )
    live = commands.add_parser("record", help="Receive only; never sends serial data")
    live.add_argument(
        "--console-port",
        help="RX adapter tapped to console TX / DB9 pin 4",
    )
    live.add_argument(
        "--base-port", help="Second RX adapter tapped to base TX / DB9 pin 3"
    )
    live.add_argument("--output", required=True, help="New JSONL file to create")
    live.add_argument("--seconds", type=float, default=60)
    live.add_argument(
        "--interactive", action="store_true", help="Type event labels while recording"
    )
    args = parser.parse_args()
    if args.command == "record" and not args.console_port and not args.base_port:
        parser.error("record requires --console-port, --base-port, or both")
    if args.command == "decode":
        analyze(args.path, args.frames)
    elif args.command == "timeline":
        for line in timeline(args.path, args.only_writes):
            print(line)
    else:
        capture_ports(
            args.console_port,
            args.base_port,
            args.output,
            args.seconds,
            args.interactive,
        )


if __name__ == "__main__":
    main()
