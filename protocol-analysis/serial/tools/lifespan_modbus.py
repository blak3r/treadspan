"""LifeSpan's observed Modbus subset. No serial I/O or hardware side effects."""

import struct

SLAVE_ADDR = 1
FUNC_READ = 3
FUNC_WRITE = 6
REG_START = 0x0001
REG_PAUSE = 0x0002
REG_SPEED = 0x000A
REG_STEPS = 0x000F
REG_VERSION1 = 0x0101
REG_ACTUAL_SPEED = 0xD10A
REG_BOOT_INFO = 0x010E
REG_IDLE_INFO = 0x0410
REG_CONTROL = 0x0000
REG_SETTING = 0x000B

# Paired reads changed 0x2001 -> 0x2201 after safety-key removal. Reinsertion,
# stopped initialization, and stats Reset did not clear it in the bench trial.
# The stock console's normal On/Start action subsequently restored 0x2001.
# This is not a live key-position sensor; physical reinsertion needs confirmation.
# REG_VERSION1 is retained for compatibility; 0x0101 carries status information.
SAFETY_KEY_CONDITION_MASK = 0x0200

SPEED_CODE_STOP = 50
REG_NAMES = {
    REG_START: "START",
    REG_PAUSE: "PAUSE",
    REG_SPEED: "TARGET_SPEED",
    REG_STEPS: "STEPS",
    REG_VERSION1: "STATUS_STATE",
    REG_ACTUAL_SPEED: "ACTUAL_SPEED",
    REG_BOOT_INFO: "BOOT_INFO_UNKNOWN",
    REG_IDLE_INFO: "IDLE_INFO_UNKNOWN",
    REG_CONTROL: "CONTROL_UNKNOWN",
    REG_SETTING: "SETTING_UNKNOWN",
}


def reg_name(register):
    return REG_NAMES.get(register, f"0x{register:04X}")


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def append_crc(data: bytes) -> bytes:
    return data + crc16(data).to_bytes(2, "little")


def check_crc(frame: bytes) -> bool:
    return len(frame) >= 5 and crc16(frame[:-2]) == int.from_bytes(frame[-2:], "little")


def build_write(register: int, value: int) -> bytes:
    return append_crc(struct.pack(">BBHH", SLAVE_ADDR, FUNC_WRITE, register, value))


def build_read(register: int, count: int = 1) -> bytes:
    if not 1 <= count <= 125:
        raise ValueError("Modbus reads require 1..125 registers")
    return append_crc(struct.pack(">BBHH", SLAVE_ADDR, FUNC_READ, register, count))


def response_matches(request: bytes, response: bytes) -> bool:
    """Accept only the requested operation and the DT3's exact write echoes.

    FC03 replies contain no register address, so callers MUST serialize requests.
    A CRC-valid exception is a failure, never an acknowledgment.
    """
    if len(request) != 8 or not check_crc(response):
        return False
    if response[:2] != request[:2]:
        return False
    if request[1] == FUNC_READ:
        count = int.from_bytes(request[4:6], "big") * 2
        return len(response) == count + 5 and response[2] == count
    if request[1] == FUNC_WRITE:
        # The isolated DT3 Reset captures acknowledge 000F=0000 exactly.
        # A different console's 0F00 echo is historical evidence, not a valid
        # acknowledgment for this app's writes.
        return response == request
    return False


class FrameDecoder:
    """Incremental CRC-checked parser; each byte is consumed at most once.

    direction='request' for console TX, 'response' for base TX. 'mixed' is
    for old one-wire logs only; FC06 direction cannot be inferred from bytes.
    Unknown function codes are skipped, never executed or guessed.
    """

    def __init__(self, direction="mixed"):
        if direction not in ("request", "response", "mixed"):
            raise ValueError("Invalid direction")
        self.direction = direction
        self.buffer = bytearray()
        self.discarded = 0

    def feed(self, chunk: bytes) -> list[bytes]:
        self.buffer.extend(chunk)
        frames = []
        while len(self.buffer) >= 5:
            if self.buffer[0] != SLAVE_ADDR:
                self._discard()
                continue
            func = self.buffer[1]
            lengths = []
            if func == FUNC_WRITE:
                lengths = [8]
            elif func == FUNC_READ:
                if self.direction != "response":
                    lengths.append(8)
                if self.direction != "request":
                    count = self.buffer[2]
                    if 2 <= count <= 250 and count % 2 == 0:
                        lengths.append(count + 5)
            elif (
                func in (FUNC_READ | 0x80, FUNC_WRITE | 0x80)
                and self.direction != "request"
            ):
                lengths = [5]
            for length in lengths:
                if len(self.buffer) >= length and check_crc(self.buffer[:length]):
                    frames.append(bytes(self.buffer[:length]))
                    del self.buffer[:length]
                    break
            else:
                if lengths and len(self.buffer) < max(lengths):
                    # A corrupt length byte must not hide a complete valid frame
                    # behind it. Scan ahead only when this prefix is incomplete.
                    offset = self._resync_offset()
                    if offset is not None:
                        self.discarded += offset
                        del self.buffer[:offset]
                        continue
                    break
                self._discard()
                continue
        return frames

    def _discard(self):
        del self.buffer[0]
        self.discarded += 1

    def _resync_offset(self):
        for offset in range(1, len(self.buffer) - 4):
            tail = self.buffer[offset:]
            if tail[0] != SLAVE_ADDR:
                continue
            lengths = []
            if tail[1] == FUNC_WRITE:
                lengths.append(8)
            elif tail[1] == FUNC_READ:
                if self.direction != "response":
                    lengths.append(8)
                if (
                    self.direction != "request"
                    and 2 <= tail[2] <= 250
                    and tail[2] % 2 == 0
                ):
                    lengths.append(tail[2] + 5)
            elif tail[1] in (0x83, 0x86) and self.direction != "request":
                lengths.append(5)
            if any(len(tail) >= size and check_crc(tail[:size]) for size in lengths):
                return offset
        return None


def describe_frame(frame: bytes, direction="mixed") -> str:
    if frame[1] & 0x80:
        return f"EXCEPTION function=0x{frame[1] & 0x7F:02X} code=0x{frame[2]:02X}"
    if frame[1] == FUNC_READ and len(frame) != 8:
        values = [
            int.from_bytes(frame[i : i + 2], "big") for i in range(3, len(frame) - 2, 2)
        ]
        return f"READ_REPLY values={values} (register requires paired request)"
    register = int.from_bytes(frame[2:4], "big")
    value = int.from_bytes(frame[4:6], "big")
    kind = (
        "READ"
        if frame[1] == FUNC_READ
        else "WRITE/ACK"
        if direction == "mixed"
        else "WRITE"
        if direction == "request"
        else "WRITE_ACK"
    )
    return f"{kind} reg=0x{register:04X} {reg_name(register)} value={value}"
