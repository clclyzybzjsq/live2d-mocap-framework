# Live2D Mocap Framework

Webcam-driven facial and upper-body mocap for Live2D models, with a virtual camera and a
desktop floating window. Windows desktop tool.

从普通摄像头实时驱动 Live2D 模型的面部与上半身动捕工具，输出到虚拟摄像头与桌面浮窗。Windows 桌面程序。

---

## English

### What it is

A C++ / Qt6 desktop tool that captures a webcam frame, runs face and pose inference in a
separate Python sidecar (MediaPipe), converts the landmarks into semantic pose parameters,
and drives a Live2D Cubism model in real time. The rendered result can be viewed in a
frameless always-on-top floating window and/or fed to other applications through a
self-registered virtual camera (it appears as **LiveBridge Virtual Camera**).

Layers: `core/` platform-independent logic, `platform/windows/` capture / virtual camera /
COM glue, `ui/` Qt6 interface and Cubism renderer, `inference/` Python sidecar talking to the
main process over shared memory.

### Requirements

- Windows 10 / 11, 64-bit
- Python 3.10 – 3.12 (64-bit) with `pip install -r inference/requirements.txt`
- A usable webcam
- A working OpenGL driver (rendering goes through an OpenGL context)

Build from source: see [BUILD.md](BUILD.md).

### Quick start

1. `pip install -r inference/requirements.txt`
2. `powershell -ExecutionPolicy Bypass -File inference\fetch_models.ps1` (downloads the two
   MediaPipe `.task` files, ~9 MB, not committed to this repository)
3. Run `lb_ui.exe`
4. *Models* page: point the model directory at a folder containing `.model3.json`
5. *Mocap* page: choose the camera, press Apply, then Start
6. *Output* page: enable the virtual camera (the first enable raises one UAC prompt to
   register the DirectShow filter)

### Repository layout and the build branch

This branch contains source code only. No Live2D models are included, and neither is the
Live2D Cubism SDK (both must be obtained separately, see BUILD.md).

A prebuilt, runnable package lives in the **`build`** branch of this repository. It contains
the compiled executable, the Qt / MinGW runtime, the Cubism Core DLL, the shaders, the
sidecar scripts and the two MediaPipe model files. It intentionally contains no Live2D
models.

### Known limitations

- Hand and finger tracking is not implemented. The pose landmarks for elbows and wrists are
  already transmitted; only the mapping to model parameters is missing.
- No forward/back torso lean and no torso twist around the vertical axis. A single camera
  cannot measure them reliably, and a naive mapping makes the upper body swing along with
  the head.
- **Tracking is only stable when the person sits roughly 30–50 cm or further from the
  camera.** At closer distances the shoulder landmarks are estimated poorly and the torso
  left/right sway is computed incorrectly.
- Mocap runs at roughly 15 fps (dominated by the face model inference). Rendering runs at
  60 fps and interpolates between mocap samples, so motion looks continuous while the
  underlying data rate stays at ~15 Hz.
- Model physics, breathing and idle motions are not driven by the application.
- Expressions rely on the model shipping its own `exp3.json` files.
- Only Live2D standard parameter IDs are written. If a model lacks one of them, that channel
  simply has no effect on that model.
- Torso axis semantics (`ParamBodyAngleZ` = left/right sway, `ParamBodyAngleY` = vertical
  body translation) were calibrated against one family of game-extracted models. Standard
  Cubism template models may use different conventions.
- Windows only. `core/` is platform-independent in principle, but only the Windows backend
  is implemented.

### Risks

- **Live2D licensing.** The Live2D Cubism SDK and any Live2D model data are subject to
  Live2D's own license terms and to the rights of the model authors. Neither is included in
  this repository. Obtaining the rights is the user's responsibility.
- **The virtual camera modifies the current user's registry** (`HKCU`) by registering a
  self-registered DirectShow filter, and the first enable requires a UAC elevation prompt.
  Unregister with `scripts\register_vcam.ps1 -Unregister`. A leftover registration can
  affect how other applications enumerate cameras.
- The rendered frame is read back from the GPU every frame (640×480). This is unoptimized
  and is the most expensive step in the pipeline.
- Qt 6 is licensed under the LGPL. Anyone redistributing a binary build must comply with the
  LGPL (ship the license text, keep the Qt libraries replaceable).
- A working OpenGL driver is required; virtual machines and remote desktop sessions may not
  provide one.

### Not yet tested

- **The virtual camera is only partially tested.** It was verified with self-written probe
  tools and a single consumer. Real-world use in OBS / Discord / conference software,
  multiple simultaneous consumers, and long-running sessions have not been systematically
  tested.
- Testing was done on a single development machine: one display, one built-in webcam, one
  integrated GPU (Intel Arc).
- Untested: high-DPI scaling, multiple monitors, non-16:9 cameras, USB cameras, other GPU
  vendors, Windows 10, sessions longer than ~30 minutes.
- Model compatibility was only checked against one family of game-extracted Live2D models;
  standard Cubism template models have not been verified.

### License

MIT, see [LICENSE](LICENSE).

---

## 中文

### 这是什么

一个 C++ / Qt6 桌面工具：采集摄像头画面，在独立的 Python 边车进程里跑人脸与上半身推理
（MediaPipe），把关键点换算成语义姿态参数，实时驱动 Live2D Cubism 模型。渲染结果可以显示在
一个无边框置顶浮窗里，也可以通过自注册的虚拟摄像头输出给其他程序（设备名显示为
**LiveBridge Virtual Camera**）。

分层：`core/` 平台无关逻辑，`platform/windows/` 采集、虚拟摄像头与 COM 胶水，`ui/` Qt6 界面与
Cubism 渲染器，`inference/` 通过共享内存与主进程通信的 Python 边车。

### 运行要求

- Windows 10 / 11，64 位
- Python 3.10 ~ 3.12（64 位），并执行 `pip install -r inference/requirements.txt`
- 一个可用的摄像头
- 可用的 OpenGL 驱动（渲染走 OpenGL 上下文）

从源码构建见 [BUILD.md](BUILD.md)。

### 快速开始

1. `pip install -r inference/requirements.txt`
2. `powershell -ExecutionPolicy Bypass -File inference\fetch_models.ps1`
   （下载两个 MediaPipe 的 `.task` 文件，约 9 MB，未纳入本仓库）
3. 运行 `lb_ui.exe`
4. 「模型」页：把模型目录指向包含 `.model3.json` 的文件夹
5. 「动捕」页：选摄像头 → 应用 → 开始
6. 「输出」页：开启虚拟摄像头（首次开启会弹一次 UAC 用于注册 DirectShow 滤镜）

### 仓库结构与 build 分支

本分支只包含源码。不含 Live2D 模型，也不含 Live2D Cubism SDK（两者都需自行获取，见 BUILD.md）。

可直接运行的完整构建放在本仓库的 **`build`** 分支：包含编译好的可执行文件、Qt 与 MinGW 运行时、
Cubism Core 的 DLL、着色器、边车脚本以及两个 MediaPipe 模型文件。**同样不含任何 Live2D 模型。**

### 已知限制

- 手部与手指追踪未实现。肘、腕的 pose 关键点已经在传，缺的只是到模型参数的映射。
- 躯干前后倾、以及绕纵轴的扭转未实现。单目摄像头测不准这两项，硬接会跟着头一起甩。
- **人体需要距摄像头约 30 ~ 50 厘米或更远才能稳定追踪。** 距离更近时肩部关键点估计不准，
  躯干的左右摇摆会被错误地解析。
- 动捕帧率约 15 fps（主要耗在人脸模型推理上），渲染 60 fps 并在动捕样本之间插值，
  所以观感是连续的，但底层数据率仍是约 15 Hz。
- 模型自带的物理、呼吸与待机动作不由本程序驱动。
- 表情依赖模型自身携带的 `exp3.json`。
- 只写 Live2D 标准参数 ID。模型缺某个参数时，该通道在该模型上就没有效果。
- 躯干轴语义（`ParamBodyAngleZ` 为左右摇摆、`ParamBodyAngleY` 为上下平动）是按某一批
  游戏导出模型的实测结果确定的。标准 Cubism 模板模型可能采用不同约定。
- 仅支持 Windows。`core/` 原则上平台无关，但目前只实现了 Windows 后端。

### 风险

- **Live2D 许可。** Live2D Cubism SDK 与 Live2D 模型数据受 Live2D 自身许可条款及模型作者
  权利约束，二者都不包含在本仓库中。取得授权是使用者的责任。
- **虚拟摄像头会修改当前用户的注册表**（`HKCU`），注册一个自注册的 DirectShow 滤镜；
  首次开启需要一次 UAC 提权。卸载用 `scripts\register_vcam.ps1 -Unregister`。
  注册残留可能影响其他程序的摄像头枚举。
- 每帧都要从 GPU 回读 640×480 的渲染结果，这一步没有优化，是整条链上最贵的环节。
- Qt 6 采用 LGPL。任何人分发二进制构建都需要遵守 LGPL（附带许可文本、保证 Qt 库可替换）。
- 需要可用的 OpenGL 驱动；虚拟机与远程桌面里可能没有。

### 尚未测试

- **虚拟摄像头只是部分测试。** 只用自写的探针工具和单个消费端验证过；在 OBS、Discord、
  会议软件里的实际接入、多消费端并发、长时间运行都没有系统测试。
- 只在单台开发机上验证：单屏、内置摄像头、单块集成显卡（Intel Arc）。
- 未测试：高 DPI 缩放、多显示器、非 16:9 摄像头、USB 摄像头、其他显卡、Windows 10、
  连续运行超过约 30 分钟。
- 模型兼容性只在某一批游戏导出的 Live2D 模型上验证过；标准 Cubism 模板模型未验证。

### 许可证

MIT，见 [LICENSE](LICENSE)。

---

**本 README 由 AI 生成。** / **This README was generated by an AI.**
