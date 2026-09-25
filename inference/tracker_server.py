"""Live Bridge 推理边车。

方案决策：MediaPipe C++ 原生仅支持 MSVC，本工程以 Python 边车方式
运行 MediaPipe Face/Pose 推理。主进程（C++）通过共享内存写入采集帧，
本进程推理后写回 JSON 结果（RawFaceFrame）。

协议定义：
    core/include/lb/core/ipc_protocol.h（C++ 侧）
    inference/protocol.py（本文件镜像）

运行：
    python tracker_server.py --model-dir inference/models --shm lb_capture_frames
自检（验证模型文件与推理可用）：
    python tracker_server.py --selftest --model-dir inference/models
"""

import argparse
import json
import mmap
import os
import struct
import sys
import time

import mediapipe as mp
import numpy as np
from mediapipe.tasks import python as mp_python
from mediapipe.tasks.python import vision

from protocol import (FRAME_OFFSET, HEADER_SIZE, MAGIC, RESULT_OFFSET,
                      SHARED_TOTAL, STATE_IDLE, STATE_QUITTING, STATE_RUNNING,
                      VERSION, unpack_header)

POLL_SLEEP_S = 0.002


def load_landmarkers(model_dir):
    face_path = os.path.join(model_dir, "face_landmarker.task")
    pose_path = os.path.join(model_dir, "pose_landmarker_full.task")
    for p in (face_path, pose_path):
        if not os.path.isfile(p):
            raise FileNotFoundError(
                f"缺少模型文件 {p}，先运行 inference/fetch_models.ps1")
    face_opts = vision.FaceLandmarkerOptions(
        base_options=mp_python.BaseOptions(model_asset_path=face_path),
        running_mode=vision.RunningMode.VIDEO,
        num_faces=8,
        min_face_detection_confidence=0.5,
        output_face_blendshapes=True,
        output_facial_transformation_matrixes=False,
    )
    pose_opts = vision.PoseLandmarkerOptions(
        base_options=mp_python.BaseOptions(model_asset_path=pose_path),
        running_mode=vision.RunningMode.VIDEO,
        num_poses=1,
        min_pose_detection_confidence=0.5,
    )
    return (vision.FaceLandmarker.create_from_options(face_opts),
            vision.PoseLandmarker.create_from_options(pose_opts))


def infer_one(face, pose, frame_bgr, ts_ms):
    """对单帧 BGR24 执行推理，返回 RawFaceFrame 的 JSON 字典。"""
    rgb = np.ascontiguousarray(frame_bgr[:, :, ::-1])
    mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
    result = {"timestamp_us": ts_ms * 1000, "face_detected": False,
              "face_confidence": 0.0, "face_landmarks": [],
              "face_blendshapes": [], "pose_landmarks": [],
              "upper_body_detected": False}

    fr = face.detect_for_video(mp_image, ts_ms)
    if fr.face_landmarks:
        lm = fr.face_landmarks[0]
        result["face_detected"] = True
        result["face_confidence"] = 1.0
        result["face_landmarks"] = [v for p in lm for v in (p.x, p.y, p.z)]
    if fr.face_blendshapes:
        result["face_blendshapes"] = [c.score for c in fr.face_blendshapes[0]]

    pr = pose.detect_for_video(mp_image, ts_ms)
    if pr.pose_landmarks:
        lm = pr.pose_landmarks[0]
        result["upper_body_detected"] = True
        result["pose_landmarks"] = [v for p in lm for v in (p.x, p.y, p.z)]
    return result


def run_selftest(model_dir):
    face, pose = load_landmarkers(model_dir)
    frame = np.zeros((720, 1280, 3), dtype=np.uint8)
    r = infer_one(face, pose, frame, 1)
    print("selftest ok:",
          "face", len(r["face_landmarks"]) // 3,
          "blendshapes", len(r["face_blendshapes"]),
          "pose", len(r["pose_landmarks"]) // 3)
    return 0


def run_server(shm_name, model_dir):
    face, pose = load_landmarkers(model_dir)

    # Windows 上 mmap 的 tagname 即命名共享内存。
    with mmap.mmap(-1, SHARED_TOTAL, tagname=shm_name) as shm:
        hdr = unpack_header(shm[:HEADER_SIZE])
        if hdr["magic"] != MAGIC or hdr["version"] != VERSION:
            print("协议不匹配：请同步 protocol.py 与 ipc_protocol.h", file=sys.stderr)
            return 2
        print("tracker_server 就绪，等待主进程帧数据...", flush=True)

        last_handled = -1
        ts_ms = 0
        while True:
            hdr = unpack_header(shm[:HEADER_SIZE])
            if hdr["state"] == STATE_QUITTING:
                break
            if hdr["state"] != STATE_RUNNING:
                time.sleep(POLL_SLEEP_S)
                continue
            if hdr["frame_index"] == last_handled or hdr["width"] == 0:
                time.sleep(POLL_SLEEP_S)
                continue

            w, h, pitch = hdr["width"], hdr["height"], hdr["pitch"]
            ts_ms = max(ts_ms + 1, hdr["timestamp_us"] // 1000)
            raw = shm[FRAME_OFFSET:FRAME_OFFSET + pitch * h]
            frame = np.frombuffer(raw, dtype=np.uint8).reshape(h, pitch)
            result = infer_one(face, pose, frame, ts_ms)
            payload = json.dumps(result, separators=(",", ":")).encode("utf-8")
            if len(payload) > RESULT_BYTES:
                payload = payload[:RESULT_BYTES - 1] + b"}"

            shm[RESULT_OFFSET:RESULT_OFFSET + len(payload)] = payload
            new_hdr = bytearray(shm[:HEADER_SIZE])
            new_hdr[40:44] = struct_hdr_result_index(len(payload))
            shm[:HEADER_SIZE] = bytes(new_hdr)
            last_handled = hdr["frame_index"]
        print("tracker_server 退出", flush=True)
    return 0


def struct_hdr_result_index(result_len):
    # 按 HEADER_FMT 直接写 result_index(偏移40) 与 result_len(偏移44)。
    import struct
    return struct.pack("<I", 0) + struct.pack("<I", result_len)


def main():
    parser = argparse.ArgumentParser(description="Live Bridge 推理边车")
    parser.add_argument("--shm", default="lb_capture_frames")
    parser.add_argument("--model-dir", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "models"))
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    try:
        if args.selftest:
            return run_selftest(args.model_dir)
        return run_server(args.shm, args.model_dir)
    except FileNotFoundError as e:
        print(str(e), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())