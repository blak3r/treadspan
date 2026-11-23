# Passive serial parser regression tests

Run on a host without hardware or Arduino libraries:

```sh
c++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  arduino/test/native/serial_parser_test.cpp -o /tmp/treadspan-serial-test
/tmp/treadspan-serial-test
```

The parser validates frame length, address, function and CRC before accessing
values. It handles fragmented/coalesced packets, exception replies, noise,
bad byte counts and modulo-32-bit time. Requests and replies use separate
fixed-size buffers. A bounded request queue includes writes, preventing a
speed-write echo from being interpreted as a step reply. Lost synchronization
clears correlation; counters remain unchanged when no valid matched read exists.

The 100 ms partial-frame and 500 ms request expiry use host-observed times,
not UART wire-edge timestamps. FC03 replies contain no register address, so a
completely missed request/reply with the same shape can still be ambiguous;
CRC validation cannot recover missing data or fix electrical interference.
Queue overflow drops correlation. No bytes are transmitted by this parser.

Existing session inference from speed targets is retained, with the added
requirement that the command be a complete CRC-valid frame. Actual movement
confirmation and model-specific speed conversions are separate changes.
These tests exercise the portable parser; ESP32 hardware timing still needs
validation on the maintainer's device.
