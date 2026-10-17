"""Live Bridge 推理边车。

方案决策：MediaPipe C++ 原生仅支持 MSVC，本工程以 Python 边车方式
运行 MediaPipe Face/Pose 推理。主进程（C++）通过共享内存写入采集帧，
本进程推理后写回二进制结果（SharedResult，见 protocol.py）。

运行：
    python tracker_server.py --model-dir inference/models --shm lb_capture_frames
自检（验证模型文件与推理可用）：
    python tracker_server.py --selftest --model-dir inference/models
"""

import argparse
import mmap
import os
import sys
import time
from concurrent.futures import ThreadPoolExecutor

import mediapipe as mp
import numpy as np
from mediapipe.tasks import python as mp_python
from mediapipe.tasks.python import vision

from protocol import (FRAME_OFFSET, HEADER_SIZE, MAGIC, RESULT_OFFSET,
                      SHARED_TOTAL, STATE_QUITTING, STATE_RUNNING, VERSION,
                      pack_result, unpack_header)

POLL_SLEEP_S = 0.002

# ARKit 52 路表情系数的规范顺序，与 core/src/solve.cpp 里的 kBlend* 索引常量
# 一一对应。必须按名字取值再排成这个顺序：MediaPipe 的 category 顺序目前虽然
# 与之接近，但一旦错位，索引就会指到别的表情上（例如"张嘴"实际读到了
# "睁大眼睛"），表现为只有一只眼睛跟得上、嘴不动之类的怪现象。
ARKIT_ORDER = (
    "browDownLeft", "browDownRight", "browInnerUp", "browOuterUpLeft", "browOuterUpRight",
    "cheekPuff", "cheekSquintLeft", "cheekSquintRight",
    "eyeBlinkLeft", "eyeBlinkRight", "eyeLookDownLeft", "eyeLookDownRight",
    "eyeLookInLeft", "eyeLookInRight", "eyeLookOutLeft", "eyeLookOutRight",
    "eyeLookUpLeft", "eyeLookUpRight", "eyeSquintLeft", "eyeSquintRight",
    "eyeWideLeft", "eyeWideRight",
    "jawForward", "jawLeft", "jawRight", "jawOpen",
    "mouthClose", "mouthDimpleLeft", "mouthDimpleRight", "mouthFrownLeft", "mouthFrownRight",
    "mouthFunnel", "mouthLeft", "mouthLowerDownLeft", "mouthLowerDownRight",
    "mouthPressLeft", "mouthPressRight", "mouthPucker", "mouthRight",
    "mouthRollLower", "mouthRollUpper", "mouthShrugLower", "mouthShrugUpper",
    "mouthSmileLeft", "mouthSmileRight", "mouthStretchLeft", "mouthStretchRight",
    "mouthUpperUpLeft", "mouthUpperUpRight", "noseSneerLeft", "noseSneerRight",
    "tongueOut",
)


def ordered_blendshapes(categories):
    """把 MediaPipe 的 blendshape 按规范顺序排成 52 个分数（缺项补 0）。"""
    by_name = {c.category_name: float(c.score) for c in categories}
    return [by_name.get(name, 0.0) for name in ARKIT_ORDER]


# 推理线程池：face 与 pose 两个模型并行执行（C++ 推理期间释放 GIL）
INFER_POOL = ThreadPoolExecutor(max_workers=2)


def load_landmarkers(model_dir):
    face_path = os.path.join(model_dir, "face_landmarker.task")
    pose_path = os.path.join(model_dir, "pose_landmarker_lite.task")
    if not os.path.isfile(pose_path):
        pose_path = os.path.join(model_dir, "pose_landmarker_full.task")
    for p in (face_path, pose_path):
        if not os.path.isfile(p):
            raise FileNotFoundError(
                f"missing model file {p}, run inference/fetch_models.ps1 first")
    face_opts = vision.FaceLandmarkerOptions(
        base_options=mp_python.BaseOptions(model_asset_path=face_path),
        running_mode=vision.RunningMode.VIDEO,
        num_faces=1,
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


def infer_face(face, frame, ts_ms):
    """只跑人脸链路（关键点 + 52 路表情系数）。"""
    mp_image = mp.Image(image_format=mp.ImageFormat.SRGBA, data=frame)
    out = {"face_detected": False, "face_landmarks": [], "face_blendshapes": [],
           "confidence": 0.0}
    fr = face.detect_for_video(mp_image, ts_ms)
    if fr.face_landmarks:
        lm = fr.face_landmarks[0]
        out["face_detected"] = True
        out["confidence"] = 1.0
        out["face_landmarks"] = [v for p in lm for v in (p.x, p.y, p.z)]
    if fr.face_blendshapes:
        out["face_blendshapes"] = ordered_blendshapes(fr.face_blendshapes[0])
    return out


def infer_pose(pose, frame, ts_ms):
    """只跑上半身链路。"""
    mp_image = mp.Image(image_format=mp.ImageFormat.SRGBA, data=frame)
    out = {"upper_body_detected": False, "pose_landmarks": []}
    pr = pose.detect_for_video(mp_image, ts_ms)
    if pr.pose_landmarks:
        lm = pr.pose_landmarks[0]
        out["upper_body_detected"] = True
        out["pose_landmarks"] = [v for p in lm for v in (p.x, p.y, p.z)]
    return out


def merge_results(fres, pres):
    return {
        "face_detected": fres["face_detected"],
        "confidence": fres["confidence"],
        "face_landmarks": fres["face_landmarks"],
        "face_blendshapes": fres["face_blendshapes"],
        "upper_body_detected": pres["upper_body_detected"],
        "pose_landmarks": pres["pose_landmarks"],
    }


def infer_one(face, pose, frame, ts_ms):
    """对单帧执行推理。frame 为 numpy 数组，BGRA32（format=PixelFormat.BGRA32）。"""
    f_fut = INFER_POOL.submit(infer_face, face, frame, ts_ms)
    p_fut = INFER_POOL.submit(infer_pose, pose, frame, ts_ms)
    return merge_results(f_fut.result(), p_fut.result())


def run_selftest(model_dir):
    face, pose = load_landmarkers(model_dir)
    frame = np.zeros((720, 1280, 4), dtype=np.uint8)
    r = infer_one(face, pose, frame, 1)
    print("selftest ok:",
          "face", len(r["face_landmarks"]) // 3,
          "blendshapes", len(r["face_blendshapes"]),
          "pose", len(r["pose_landmarks"]) // 3)
    return 0


def run_server(shm_name, model_dir, pose_every=4):
    face, pose = load_landmarkers(model_dir)

    with mmap.mmap(-1, SHARED_TOTAL, tagname=shm_name) as shm:
        hdr = unpack_header(shm[:HEADER_SIZE])
        if hdr["magic"] != MAGIC or hdr["version"] != VERSION:
            print("protocol mismatch: sync protocol.py with ipc_protocol.h", file=sys.stderr)
            return 2
        print("tracker_server ready, waiting for frames...", flush=True)

        last_handled = -1
        ts_ms = 0
        last_pose = None  # 上次 pose 结果（body 低频更新时复用）
        frame_count = 0
        while True:
            hdr = unpack_header(shm[:HEADER_SIZE])
            if hdr["state"] == STATE_QUITTING:
                break
            if hdr["state"] != STATE_RUNNING or hdr["frame_index"] == last_handled or hdr["width"] == 0:
                time.sleep(POLL_SLEEP_S)
                continue

            w, h, pitch = hdr["width"], hdr["height"], hdr["pitch"]
            ts_ms = max(ts_ms + 1, hdr["timestamp_us"] // 1000)
            raw = shm[FRAME_OFFSET:FRAME_OFFSET + pitch * h]
            # (h, pitch) -> (h, w, 4)，兼容行距大于 w*4 的情况
            frame = np.frombuffer(raw, dtype=np.uint8).reshape(h, pitch)[:, : w * 4]
            frame = np.ascontiguousarray(frame).reshape(h, w, 4)
            result = infer_one(face, pose, frame, ts_ms)

            # pose 低频策略：body 每 pose_every 帧更新一次，其余帧复用最近结果
            if frame_count % pose_every != 0 and last_pose is not None:
                result["pose_landmarks"] = last_pose["pose_landmarks"]
                result["upper_body_detected"] = last_pose["upper_body_detected"]
            else:
                last_pose = {
                    "pose_landmarks": result["pose_landmarks"],
                    "upper_body_detected": result["upper_body_detected"],
                }
            frame_count += 1
            payload = pack_result(
                result["face_landmarks"], result["face_blendshapes"],
                result["pose_landmarks"], result["face_detected"],
                result["upper_body_detected"], result["confidence"],
                hdr["timestamp_us"])

            shm[RESULT_OFFSET:RESULT_OFFSET + len(payload)] = payload
            head = bytearray(shm[:HEADER_SIZE])
            head[40:44] = hdr["frame_index"].to_bytes(4, "little")  # result_index = 已处理帧号
            head[44:48] = len(payload).to_bytes(4, "little")        # result_len
            shm[:HEADER_SIZE] = bytes(head)
            last_handled = hdr["frame_index"]
        print("tracker_server exiting", flush=True)
    return 0


def main():
    parser = argparse.ArgumentParser(description="Live Bridge inference sidecar")
    parser.add_argument("--shm", default="lb_capture_frames")
    parser.add_argument("--model-dir", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "models"))
    parser.add_argument("--pose-every", type=int, default=4,
                        help="body 模型每 N 帧更新一次（默认 4：躯干动作慢，"
                             "把 CPU 让给人脸模型以提高头部姿态的帧率与响应）")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    try:
        if args.selftest:
            return run_selftest(args.model_dir)
        return run_server(args.shm, args.model_dir, args.pose_every)
    except FileNotFoundError as e:
        print(str(e), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())