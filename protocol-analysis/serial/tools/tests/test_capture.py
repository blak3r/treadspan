"""Offline regression tests; no real ports are opened."""

import json
import sys
import tempfile
import time
import types
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import capture
from lifespan_modbus import FrameDecoder, append_crc, build_read, build_write, check_crc


class ProtocolTests(unittest.TestCase):
    def test_captured_crc_vectors(self):
        self.assertEqual(build_read(15).hex(), "0103000f0001b409")
        self.assertEqual(build_write(10, 88).hex(), "0106000a0058a832")
        self.assertTrue(check_crc(bytes.fromhex("01030200017984")))

    def test_all_fragment_boundaries(self):
        frame = bytes.fromhex("01030200017984")
        for cut in range(len(frame) + 1):
            d = FrameDecoder("response")
            self.assertEqual(d.feed(frame[:cut]) + d.feed(frame[cut:]), [frame])

    def test_corrupt_crc_does_not_become_a_value(self):
        frame = bytes.fromhex("01030200017984")
        bad = bytearray(frame)
        bad[-1] ^= 1
        d = FrameDecoder("response")
        self.assertEqual(d.feed(bytes(bad) + frame), [frame])
        self.assertGreater(d.discarded, 0)

    def test_corrupt_length_resynchronizes(self):
        frame = bytes.fromhex("01030200017984")
        self.assertEqual(
            FrameDecoder("response").feed(b"\x01\x03\xfa" + frame), [frame]
        )

    def test_exception_is_not_a_normal_read(self):
        frame = append_crc(bytes.fromhex("018302"))
        self.assertEqual(FrameDecoder("response").feed(frame), [frame])
        self.assertIn("EXCEPTION", capture.describe_frame(frame, "response"))

    def test_legacy_log_does_not_duplicate_printed_decode(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "legacy.txt"
            p.write_text(
                "REQ : 1 3 0 15 0 1 180 9\nRESP: 1 3 2 0 1 121 132\nREAD_REPLY values=[1]\n"
            )
            frames = list(capture.decode_log(p))
            self.assertEqual(len(frames), 2)
            self.assertEqual([f[1] for f in frames], ["request", "response"])

    def test_session_boundaries_discard_partial_frames(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "capture.jsonl"
            events = [
                {"type": "session"},
                {"type": "bytes", "direction": "response", "hex": "01 03 02"},
                {"type": "session"},
                {"type": "bytes", "direction": "response", "hex": "00 01 79 84"},
            ]
            p.write_text("\n".join(map(json.dumps, events)))
            self.assertEqual(list(capture.decode_log(p)), [])

    def test_timeline_preserves_labels_and_filters_reads(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "capture.jsonl"
            events = [
                {"type": "session", "time_ns": 0},
                {"type": "marker", "time_ns": 1, "label": "Start pressed"},
                {
                    "type": "bytes",
                    "time_ns": 2,
                    "direction": "request",
                    "hex": build_read(15).hex(),
                },
                {
                    "type": "bytes",
                    "time_ns": 3,
                    "direction": "request",
                    "hex": build_write(10, 88).hex(),
                },
            ]
            p.write_text("\n".join(map(json.dumps, events)))
            result = "\n".join(capture.timeline(p, True))
            self.assertIn("Start pressed", result)
            self.assertIn("WRITE", result)
            self.assertNotIn("READ ", result)


class PassiveCaptureTests(unittest.TestCase):
    def test_receive_only_and_no_overwrite(self):
        opened = []

        class Port:
            in_waiting = 0

            def __init__(self, *args, **kwargs):
                self.closed = False
                self.sent = False
                opened.append(self)

            def read(self, count):
                time.sleep(0.001)
                if not self.sent:
                    self.sent = True
                    return bytes.fromhex("01030200017984")
                return b""

            def write(self, data):
                raise AssertionError("Passive recorder transmitted bytes")

            def close(self):
                self.closed = True

        serial = types.SimpleNamespace(Serial=Port, SerialException=OSError)
        with (
            tempfile.TemporaryDirectory() as tmp,
            patch.dict(sys.modules, serial=serial),
        ):
            p = Path(tmp) / "capture.jsonl"
            capture.capture_ports(None, "mock", p, 0.01)
            self.assertTrue(opened[0].closed)
            self.assertEqual(len(list(capture.decode_log(p))), 1)
            with self.assertRaises(FileExistsError):
                capture.capture_ports(None, "mock", p, 0.01)
            with self.assertRaises(ValueError):
                capture.capture_ports("mock", "mock", Path(tmp) / "other", 0.01)
            with self.assertRaises(ValueError):
                capture.capture_ports(None, "mock", Path(tmp) / "other", 0)


if __name__ == "__main__":
    unittest.main()
