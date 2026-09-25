# ournotes_vtube

Windows 桌面 Live2D 动捕工具。通过普通摄像头跟踪面部表情与上半身姿态，
实时驱动 Live2D 模型，输出到虚拟摄像头或桌面浮窗，用于直播、录屏与会议场景。

当前状态：工程骨架（见下方"里程碑"）。方案与评估见 `docs/assessment-plan.md`。

## 目录结构

```
core/                核心层：平台无关接口 + 纯逻辑（语义参数、平滑、映射）
platform/windows/    Windows 适配层：采集、虚拟摄像头、浮窗、GPU、渲染、推理客户端
ui/                  Qt6 桌面界面（黑色主题；无表情符号；按方案的布局与文案规范）
inference/           Python 推理边车（MediaPipe Face/Pose，共享内存与主进程通信）
docs/                项目文档与评估方案
scripts/             工具链引导与构建脚本
```

## 构建

前置工具链（均免管理员，装在用户目录 / D:\deps）：

- CMake、Ninja
- MinGW-w64（UCRT，POSIX 线程）
- Qt 6（win64_mingw 构建）
- Python 3.12+（推理边车用；位于 D:\deps\venv）

一键构建：

```powershell
scripts\build.ps1
```

产物：`build\ui\lb_ui.exe`。

## 里程碑

- [ ] M1 最小闭环：采集 -> MediaPipe 推理 -> 语义参数 -> 浮窗驱动模型
- [ ] M2 虚拟摄像头：DirectShow 用户态 filter，首次运行自动注册（零配置）
- [ ] M3 设置界面与打磨
- [ ] M4 打包交付

## UI 约束（硬性）

- 禁用所有 emoji / 表情符号
- 黑色系、扁平、低圆角（<= 4px）、单一强调色 #4DB8FF
- 禁用营销文案与"AI 味"元素（紫渐变、玻璃拟态、装饰图标堆砌）
- 细节与文案规范见 `docs/assessment-plan.md` 第七节