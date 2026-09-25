# 与 core/include/lb/core/ipc_protocol.h 保持同步。
import struct

MAGIC = 0x4C425F31  # "LB_1"
VERSION = 1

MAX_WIDTH = 1280
MAX_HEIGHT = 720
MAX_PITCH = MAX_WIDTH * 3
FRAME_BYTES = MAX_PITCH * MAX_HEIGHT
RESULT_BYTES = 64 * 1024

STATE_IDLE = 0
STATE_RUNNING = 1
STATE_QUITTING = 2

# magic u32, version u32, state u32, frame_index u32, width u32, height u32,
# pitch u32, format u32, timestamp_us u64, result_index u32, result_len u32, reserved[80]
HEADER_FMT = "<IIIIIIIIQII"
HEADER_SIZE = struct.calcsize(HEADER_FMT)
assert HEADER_SIZE + 80 == 128, "header layout must match C++ (128 bytes total)"

FRAME_OFFSET = HEADER_SIZE + 80  # 128
RESULT_OFFSET = FRAME_OFFSET + FRAME_BYTES
SHARED_TOTAL = RESULT_OFFSET + RESULT_BYTES

HEADER = struct.Struct(HEADER_FMT)


def pack_header(state, frame_index, width, height, pitch, fmt, ts_us,
                result_index=0, result_len=0):
    body = HEADER.pack(MAGIC, VERSION, state, frame_index, width, height,
                       pitch, fmt, ts_us, result_index, result_len)
    return body + b"\x00" * 80


def unpack_header(buf):
    h = HEADER.unpack_from(buf, 0)
    return {
        "magic": h[0], "version": h[1], "state": h[2], "frame_index": h[3],
        "width": h[4], "height": h[5], "pitch": h[6], "format": h[7],
        "timestamp_us": h[8], "result_index": h[9], "result_len": h[10],
    }