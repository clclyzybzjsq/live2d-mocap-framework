# 与 core/include/lb/core/ipc_protocol.h 保持同步。
import struct

MAGIC = 0x4C425F31  # "LB_1"
VERSION = 2

MAX_WIDTH = 1280
MAX_HEIGHT = 720
MAX_PITCH = MAX_WIDTH * 4  # BGRA32
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

# 结果布局：face 关键点数组、52 路表情系数、pose 关键点数组、标志位。
RESULT_FMT = "<I" + "f" * (478 * 3) + "I" + "f" * 52 + "I" + "f" * (33 * 3) + "BBfQ"
RESULT = struct.Struct(RESULT_FMT)
assert RESULT.size < RESULT_BYTES, "result layout must fit the result buffer"


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


def pack_result(face_landmarks, face_blendshapes, pose_landmarks,
                face_detected, upper_body_detected, confidence, ts_us):
    """face_landmarks/pose_landmarks 为 (x,y,z) 扁平列表，向量长度可为 0。"""
    fl = list(face_landmarks) if face_landmarks else []
    fb = list(face_blendshapes) if face_blendshapes else []
    pl = list(pose_landmarks) if pose_landmarks else []
    fl += [0.0] * (478 * 3 - len(fl))
    fb += [0.0] * (52 - len(fb))
    pl += [0.0] * (33 * 3 - len(pl))
    return RESULT.pack(len(face_landmarks) // 3 if face_landmarks else 0,
                       *fl,
                       len(face_blendshapes) if face_blendshapes else 0,
                       *fb,
                       len(pose_landmarks) // 3 if pose_landmarks else 0,
                       *pl,
                       1 if face_detected else 0,
                       1 if upper_body_detected else 0,
                       confidence,
                       ts_us)