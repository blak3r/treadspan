"""Flipper treadcap decoding from synthetic edge timings; no hardware or serial port."""

import io
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import treadcap_decode as td  # noqa: E402

READ_VERSION = bytes.fromhex("01 03 01 01 00 01 D4 36")
VERSION_REPLY = bytes.fromhex("01 03 02 20 01 60 44")


def edges(db9, frame, start_cycles):
    """Edge lines for 4800 8N1 bytes sent back to back on an idle-high line."""
    lines, level, t = [], 1, start_cycles
    for byte in frame:
        bits = [0] + [(byte >> i) & 1 for i in range(8)] + [1]
        for bit in bits:
            if bit != level:
                level = bit
                lines.append(f"E{db9}{bit} {int(t) & 0xFFFFFFFF:08X}")
            t += td.BIT
    return lines, t


def decode(lines):
    out = io.StringIO()
    decoder = td.Decoder(out)
    for line in lines:
        decoder.line(line)
    return out.getvalue(), decoder


class TreadcapDecodeTests(unittest.TestCase):
    def test_request_and_reply_decode_with_register_pairing(self):
        request, t = edges(4, READ_VERSION, 1000)
        reply, t = edges(3, VERSION_REPLY, t + 0.023 * td.CLOCK)
        status = f"S {int(t + td.CLOCK) & 0xFFFFFFFF:08X} 111110 0"
        text, decoder = decode(request + reply + [status])
        self.assertRegex(text, r"console→base +01 03 01 01 00 01 D4 36 +read \S+")
        # FC03 replies carry no register; it is paired with the last request.
        self.assertRegex(text, r"base→console +01 03 02 20 01 60 44 +reply \S+ = 0x2001")
        self.assertEqual(decoder.counts["crc_bad"], 0)

    def test_final_byte_without_trailing_edge_is_completed_by_later_events(self):
        # 0xFF ends on 1 bits: only its start bit has edges.
        frame = bytes.fromhex("01 03 04 10 00 01 84 FF")
        lines, t = edges(4, frame, 0)
        text, decoder = decode(lines + [f"S {int(t + td.CLOCK):08X} 111110 0"])
        self.assertIn("84 FF", text)
        self.assertEqual(decoder.counts["frames"], 1)

    def test_short_spikes_are_ignored_and_counted(self):
        spike = ["E30 00001000", "E31 000010E0"]  # 3.5 µs low pulse
        request, t = edges(4, READ_VERSION, 100_000)
        text, decoder = decode(spike + request + [f"S {int(t + td.CLOCK):08X} 111110 0"])
        self.assertEqual(decoder.lines[3].glitches, 1)
        self.assertEqual(decoder.counts["crc_bad"], 0)

    def test_counter_wrap_and_level_changes(self):
        lines = ["S FFFFFF00 111110 0", "E60 00000100"]  # wraps past 2^32
        text, _ = decode(lines)
        (line,) = [l for l in text.splitlines() if "DB9 pin 6" in l]
        self.assertIn("(key / enable) → LO", line)
        # 0x100 after the wrap is 512 cycles later, not ~67 s earlier.
        self.assertAlmostEqual(float(line.split()[0]), 512 / td.CLOCK, places=6)


if __name__ == "__main__":
    unittest.main()
