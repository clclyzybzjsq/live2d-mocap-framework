# 构建与运行要求

## 一、构建环境

| 依赖 | 版本 / 说明 |
|---|---|
| Windows | 10 / 11，64 位 |
| CMake | ≥ 3.21 |
| Ninja | 任意近期版本 |
| MinGW-w64 | **msvcrt 变体**（POSIX 线程）。实测 GCC 16。`scripts/download_mingw_msvcrt.ps1` 可获取 |
| Qt | 6.x，**官方 mingw_64 构建**。需要 Widgets、OpenGL、OpenGLWidgets 三个模块 |
| Live2D Cubism SDK for Native | **5-r.5**，需自行从 Live2D 官网下载（许可不允许转发） |
| GLEW | 2.2.0 **源码**（需要 `src/glew.c` 与 `include/GL`），构建时直接编译进项目 |
| 官方 DirectXMath | 需要 `DirectXMath.h`。MinGW 自带的精简版缺 `XMMATRIX`，缺官方版时 CMake 在配置阶段就会报错退出 |

`scripts/bootstrap_toolchain.ps1` 可以自动装好 CMake、Ninja、MinGW、Qt，能省掉手工步骤。

### 三个必须显式指定的路径

CMake 里这三项的默认值是开发机路径，必须用 `-D` 覆盖：

```
-DLB_CUBISM_SDK_DIR=<Cubism SDK 根目录>
-DLB_GLEW_DIR=<GLEW 根目录>
-DLB_DIRECTXMATH_INC=<DirectXMath 的 Inc 目录>
```

## 二、构建步骤

### 1. 生成 Cubism Core 的 MinGW 导入库（必须在配置之前）

Cubism Core 官方只提供 MSVC 格式的库，MinGW 无法直接链接。这一步用 `gendef` + `dlltool` 从官方 dll 生成导入库：

```
powershell -ExecutionPolicy Bypass -File scripts\build_cubism_core_lib.ps1 -SdkDir <Cubism SDK 根目录>
```

产物是 `build\cubism_link\libLive2DCubismCore.a`。**没有它 CMake 会在配置阶段直接退出**，所以这一步不能省也不能后做。

### 2. 配置

```
cmake -S . -B build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Debug ^
  -DCMAKE_CXX_COMPILER=<MinGW>\bin\g++.exe ^
  -DCMAKE_PREFIX_PATH=<Qt mingw_64 根目录> ^
  -DLB_BUILD_UI=ON ^
  -DLB_WITH_CUBISM=ON ^
  -DLB_CUBISM_SDK_DIR=<Cubism SDK 根目录> ^
  -DLB_GLEW_DIR=<GLEW 根目录> ^
  -DLB_DIRECTXMATH_INC=<DirectXMath 的 Inc 目录>
```

**`LB_WITH_CUBISM` 默认是 OFF。** 不开这一项也能编译通过，但拿到的是 QPainter 占位渲染，没有 Live2D 形象——这是最容易以为"构建成功但功能不对"的地方。

### 3. 编译

```
cmake --build build
```

### 4. 部署运行时

编译产物 `build\ui\lb_ui.exe` 还不能直接双击运行，需要把 Qt 与 MinGW 运行库放到它旁边：

```
windeployqt --no-translations --no-system-d3d-compiler --no-opengl-sw build\ui\lb_ui.exe
```

然后把 MinGW 的 `libgcc_s_seh-1.dll`、`libstdc++-6.dll`、`libwinpthread-1.dll` 复制到 `build\ui\`。
`scripts/build.ps1` 这四步（配置除外）都做了，但它只在 `build\CMakeCache.txt` 不存在时才会配置，**且不传上面那些 `-D` 参数**。所以推荐：先按第 1、2 步手工配置一次，之后就可以直接用 `scripts\build.ps1` 做增量构建。

## 三、运行前置

1. Python 3.10 ~ 3.12（64 位），安装推理依赖：

   ```
   pip install -r inference\requirements.txt
   ```

   即 `mediapipe` 与 `numpy`。

2. 下载推理模型文件（不入库，需联网下载一次，约 9 MB）：

   ```
   powershell -ExecutionPolicy Bypass -File inference\fetch_models.ps1
   ```

   产物在 `inference\models\`：`face_landmarker.task`、`pose_landmarker_lite.task`。

3. 自检边车是否可用：

   ```
   python inference\tracker_server.py --selftest --model-dir inference\models
   ```

4. 虚拟摄像头首次开启会弹一次 UAC（程序要注册 DirectShow 滤镜）。
   需要手动注册时：`powershell -ExecutionPolicy Bypass -File scripts\register_vcam.ps1`，
   验证：`scripts\verify_vcam.ps1`。

## 四、三个已知的坑

1. **运行时 PATH 顺序：MinGW 的 bin 必须排在 Qt 的 bin 之前。**
   否则加载器会取到 Qt 自带的 GCC 13 运行库，与 GCC 16 编译的产物不匹配，进程启动即报 `0xC0000139`。

2. **不要用 `windeployqt --compiler-runtime`。**
   它会把 Qt 自带的 GCC 13 运行库一起拷进来，同样导致"无法定位程序输入点"。运行库必须来自实际使用的 MinGW。

3. **Qt 必须是 msvcrt 构建。**
   Qt 官方 mingw 构建基于 msvcrt，与 UCRT 版 MinGW 混用会出现跨 CRT 堆冲突（同样表现为启动崩溃）。

## 五、可选构建开关

| 开关 | 默认 | 说明 |
|---|---|---|
| `LB_BUILD_UI` | ON | Qt6 桌面界面 |
| `LB_WITH_CUBISM` | **OFF** | Live2D Cubism 渲染；关闭时为占位渲染 |
| `LB_BUILD_SMOKE` | OFF | 管线冒烟测试 |
| `LB_BUILD_VCAM_PROBE` | OFF | 虚拟摄像头注册探测工具 |
| `LB_BUILD_VCAM_FLOW` | OFF | 虚拟摄像头链路测试 |

其余的诊断工具开关见根目录 `CMakeLists.txt`。

此readme由ai生成
