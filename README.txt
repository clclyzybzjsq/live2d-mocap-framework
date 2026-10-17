Live2D Mocap Framework —— 预构建包
=================================

本分支是可直接运行的预构建包。源码、构建说明与完整文档在 main 分支：
  README.md   功能、已知限制、风险、未测试项
  BUILD.md    从源码构建的依赖与步骤

运行要求
  · Windows 10 / 11，64 位
  · Python 3.10 ~ 3.12（64 位），并安装推理依赖：
      pip install -r inference\requirements.txt
  · 一个可用的摄像头
  · 首次开启虚拟摄像头会弹一次 UAC（用于注册 DirectShow 滤镜）

启动步骤
  1. pip install -r inference\requirements.txt
  2. 双击 lb_ui.exe
  3. 「模型」页：把模型目录指向你自己的 Live2D 模型目录
     （本包不含任何 Live2D 模型，需自行准备含 .model3.json 的目录）
  4. 「动捕」页：选摄像头 → 应用 → 开始
  5. 「输出」页：开启虚拟摄像头后，在 OBS / Discord / 会议软件里选择
       LiveBridge Virtual Camera

已知问题
  · 人体需距摄像头约 30 ~ 50 厘米或更远才能稳定追踪，更近时躯干左右摇摆会解析错误
  · 虚拟摄像头只是部分测试，多消费端并发与长时间运行未系统验证
  · 手部与手指追踪未实现

完整说明见 main 分支的 README.md。
