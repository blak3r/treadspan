"""Decoder for the Flipper Zero `treadcap` capture of the treadmill DB9 harness.

Runs the `treadcap` command on the Flipper (see ../flipper/README.md), decodes
4800 8N1 serial on DB9 pin 4 (console -> base) and DB9 pin 3 (base -> console)
from edge timings, and prints Modbus frames alongside level changes on DB9
pins 1, 5, 6 and 7.

    python3 treadcap_decode.py --seconds 30        # capture (needs pyserial)
    python3 treadcap_decode.py --replay capture.log # decode a saved capture

Every raw line from the Flipper is saved, so a capture can be decoded again later.
"""

import argparse
import glob
import sys
import time
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lifespan_modbus import check_crc, describe_frame, reg_name  # noqa: E402

CLOCK = 64_000_000
BAUD = 4800
BIT = CLOCK / BAUD
FRAME_GAP = 3.5 * 10 * BIT  # Modbus RTU inter-frame silence
GLITCH = BIT / 4  # low pulses shorter than this are noise (mains spikes at power-up)
PROMPT = b">: "
UART_PINS = {4: "console→base", 3: "base→console"}
LEVEL_NAMES = {1: "12 V supply", 5: "≈5 V line", 6: "key / enable", 7: "pin 7"}


class Clock:
    """Unwraps the Flipper's 32-bit cycle counter into seconds since capture start."""

    def __init__(self):
        self.base = None
        self.last = None
        self.offset = 0

    def seconds(self, raw):
        if self.last is not None and raw < self.last and self.last - raw > 1 << 31:
            self.offset += 1 << 32
        self.last = raw
        cycles = raw + self.offset
        if self.base is None:
            self.base = cycles
        return (cycles - self.base) / CLOCK


class UartLine:
    """Decodes 8N1 from (time, level) transitions on one idle-high line."""

    def __init__(self, name):
        self.name = name
        self.transitions = []  # (cycles, level) not yet consumed
        self.level = 1
        self.frame = bytearray()
        self.frame_start = None
        self.last_byte_end = None
        self.errors = 0
        self.frame_errors = 0
        self.glitches = 0

    def edge(self, cycles, level):
        if level == self.level:
            return []  # repeated read; the edge was shorter than the ISR latency
        self.level = level
        last = self.transitions[-1] if self.transitions else None
        if level == 1 and last and last[1] == 0 and cycles - last[0] < GLITCH:
            # A low pulse far shorter than a bit is noise, not a start bit.
            self.transitions.pop()
            self.glitches += 1
            return []
        self.transitions.append((cycles, level))
        return self.decode(cycles)

    def level_at(self, cycles, start_index):
        level = 0  # the start bit's level
        for t, value in self.transitions[start_index:]:
            if t > cycles:
                break
            level = value
        return level

    def decode(self, now):
        out = []
        while True:
            # Find a falling edge: the start bit.
            start_index = next(
                (i for i, (_, value) in enumerate(self.transitions) if value == 0), None
            )
            if start_index is None:
                self.transitions = self.transitions[-1:]
                break
            start = self.transitions[start_index][0]
            end = start + 9.5 * BIT  # middle of the stop bit
            if now < end:
                self.transitions = self.transitions[start_index:]
                break  # wait for the rest of this byte
            value = 0
            for bit in range(8):
                if self.level_at(start + (1.5 + bit) * BIT, start_index):
                    value |= 1 << bit
            stop_ok = self.level_at(end, start_index) == 1
            if not stop_ok:
                self.errors += 1
            out.extend(self.byte(start, value, stop_ok))
            # Resume after the stop bit's middle.
            rest = [(t, v) for t, v in self.transitions if t > end]
            self.transitions = [(end, 1)] + rest if stop_ok else rest
        return out

    def byte(self, start, value, stop_ok):
        out = []
        if self.last_byte_end is not None and start - self.last_byte_end > FRAME_GAP:
            out.extend(self.flush())
        if not self.frame:
            self.frame_start = start
        self.frame.append(value)
        if not stop_ok:
            self.frame_errors += 1
        self.last_byte_end = start + 10 * BIT
        return out

    def idle(self, now):
        """Finish bytes whose stop bit has passed, then flush a frame after silence.

        A byte ending in 1 bits has no edge after its stop bit, so it can only be
        completed once time moves on.
        """
        out = self.decode(now)
        if self.frame and self.last_byte_end is not None and now - self.last_byte_end > FRAME_GAP:
            out.extend(self.flush())
        return out

    def flush(self):
        if not self.frame:
            return []
        frame, start = bytes(self.frame), self.frame_start
        self.frame = bytearray()
        bad, self.frame_errors = self.frame_errors, 0
        return [(start, self.name, frame, bad)]


class Decoder:
    def __init__(self, out):
        self.out = out
        self.clock = Clock()
        self.lines = {pin: UartLine(name) for pin, name in UART_PINS.items()}
        self.levels = {}
        self.last_request = None
        self.counts = {"frames": 0, "crc_bad": 0, "dropped": 0}

    def emit(self, seconds, text):
        print(f"{seconds:+11.6f}  {text}", file=self.out, flush=True)

    def frame(self, start, name, frame, bad):
        self.counts["frames"] += 1
        seconds = (start - (self.clock.base or start)) / CLOCK
        hexed = frame.hex(" ").upper()
        ok = len(frame) >= 4 and check_crc(frame)
        if not ok:
            self.counts["crc_bad"] += 1
        meaning = ""
        if ok:
            if name == "console→base" and frame[1] == 0x03 and len(frame) == 8:
                self.last_request = int.from_bytes(frame[2:4], "big")
                meaning = f"read {reg_name(self.last_request)}"
            elif name == "base→console" and frame[1] == 0x03 and len(frame) != 8:
                value = int.from_bytes(frame[3:5], "big") if len(frame) >= 7 else None
                register = reg_name(self.last_request) if self.last_request is not None else "?"
                meaning = f"reply {register} = 0x{value:04X} ({value})" if value is not None else "reply"
            else:
                meaning = describe_frame(frame, "request" if name == "console→base" else "reply")
        flag = "" if ok else "  CRC BAD" + (f", {bad} framing errors" if bad else "")
        self.emit(seconds, f"{name:13}  {hexed:<26} {meaning}{flag}")

    def line(self, raw):
        text = raw.strip()
        if text.startswith("E") and " " in text:
            head, stamp = text.split()
            pin, level = int(head[1:-1]), int(head[-1])
            cycles = int(stamp, 16)
            self.clock.seconds(cycles)
            cycles += self.clock.offset
            if pin in self.lines:
                found = self.lines[pin].edge(cycles, level)
                for item in sorted(found):
                    self.frame(*item)
            elif self.levels.get(pin) != level:
                self.levels[pin] = level
                seconds = (cycles - self.clock.base) / CLOCK
                self.emit(seconds, f"DB9 pin {pin} ({LEVEL_NAMES.get(pin, '')}) → {'HI' if level else 'LO'}")
            for uart in self.lines.values():
                for item in uart.idle(cycles):
                    self.frame(*item)
        elif text.startswith("S "):
            _, stamp, levels, dropped = text.split()
            cycles = int(stamp, 16)
            self.clock.seconds(cycles)
            cycles += self.clock.offset
            for uart in self.lines.values():
                for item in uart.idle(cycles):
                    self.frame(*item)
            dropped = int(dropped)
            if dropped > self.counts["dropped"]:
                self.emit((cycles - self.clock.base) / CLOCK, f"! Flipper dropped {dropped} events")
                self.counts["dropped"] = dropped
            names = ["1", "3", "4", "5", "6", "7"]
            first = not self.levels.get("reported")
            if first:
                self.levels["reported"] = True
                state = "  ".join(f"pin {n} {'HI' if v == '1' else 'LO'}" for n, v in zip(names, levels))
                self.emit((cycles - self.clock.base) / CLOCK, f"levels: {state}")
                for n, v in zip(names, levels):
                    if int(n) in LEVEL_NAMES:
                        self.levels[int(n)] = int(v)
        elif text.startswith("#"):
            print(text, file=self.out, flush=True)


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodemflip_*"))
    if not ports:
        sys.exit("No Flipper found on USB (/dev/cu.usbmodemflip_*).")
    return ports[0]


def capture(args):
    import signal

    import serial

    # Stop cleanly on kill as well as Ctrl+C, so the Flipper command is ended.
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt))

    log_path = Path(args.log or f"treadcap-{datetime.now():%Y%m%d-%H%M%S}.log")
    decoder = Decoder(sys.stdout)
    print(f"# saving raw capture to {log_path}", flush=True)
    # Exclusive create: existing evidence is never overwritten.
    with open(log_path, "x") as log, serial.Serial(
        args.port or find_port(), 230400, timeout=0.05
    ) as port:
        port.write(b"\x03\r")
        time.sleep(0.3)
        port.reset_input_buffer()
        port.write(b"treadcap\r")
        deadline = time.monotonic() + args.seconds if args.seconds else None
        pending = b""
        try:
            while deadline is None or time.monotonic() < deadline:
                pending += port.read(8192)
                *lines, pending = pending.split(b"\n")
                for raw in lines:
                    text = raw.decode(errors="replace").strip()
                    if not text or text == "treadcap":
                        continue
                    log.write(text + "\n")
                    decoder.line(text)
        except KeyboardInterrupt:
            pass
        finally:
            port.write(b"\x03")
            time.sleep(0.3)
            summary(decoder)


def replay(args):
    decoder = Decoder(sys.stdout)
    with open(args.replay) as log:
        for text in log:
            decoder.line(text)
    summary(decoder)


def summary(decoder):
    c = decoder.counts
    errors = {line.name: line.errors for line in decoder.lines.values()}
    glitches = {line.name: line.glitches for line in decoder.lines.values()}
    print(
        f"# {c['frames']} frames, {c['crc_bad']} with bad CRC, framing errors {errors}, "
        f"glitches ignored {glitches}, "
        f"Flipper dropped {c['dropped']} events",
        flush=True,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", help="Flipper serial port (default: first usbmodemflip_*)")
    parser.add_argument("--seconds", type=float, default=0, help="stop after this long (0 = Ctrl+C)")
    parser.add_argument("--log", help="raw capture path (default ./treadcap-<time>.log)")
    parser.add_argument("--replay", help="decode a saved raw capture instead of capturing")
    args = parser.parse_args()
    replay(args) if args.replay else capture(args)


if __name__ == "__main__":
    main()
