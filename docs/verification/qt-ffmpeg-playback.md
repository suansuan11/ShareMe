# Qt + FFmpeg 本地播放验证

验证日期：2026-07-28

## 验证范围

本阶段验证本地影片通过 FFmpeg libraries 解封装和解码，由独立播放会话
控制队列、状态与跳转，再由 Qt Multimedia 输出 RGBA 视频帧和 48 kHz
双声道 S16 PCM。它不包含 WebRTC、房间信令、远端观众或 Windows
进程级音频捕获。

测试媒体由 CTest 调用 `ffmpeg` 在构建目录生成，未提交二进制媒体文件。

## 本地环境

- macOS 26.6，Apple Silicon；
- CMake 4.3.3；
- Ninja 1.13.2；
- Apple Clang 21.0.0；
- FFmpeg 8.1.1；
- Qt Base、Qt Declarative、Qt Multimedia 6.11.1。

Homebrew 安装最小依赖：

```bash
brew install ffmpeg qtbase qtdeclarative qtmultimedia
```

## 可复现命令

可移植核心：

```bash
cmake --fresh --preset dev
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure
```

FFmpeg 媒体层：

```bash
cmake --fresh --preset media-dev
cmake --build --preset build-media-dev
ctest --preset test-media-dev --output-on-failure
```

Qt + FFmpeg 播放：

```bash
cmake --fresh --preset playback-dev -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build --preset build-playback-dev
cmake --build --preset build-playback-dev --target shareme_playback_demo_qmllint
ctest --preset test-playback-dev --output-on-failure
```

## 结果

| 检查项 | 结果 |
| --- | --- |
| 可移植核心 Debug | 2/2 通过 |
| FFmpeg 媒体层 | 6/6 通过 |
| Qt + FFmpeg 播放 | 8/8 通过 |
| QML 静态检查 | 通过，无警告 |
| macOS 应用窗口首屏 | 通过，标准 `.app` 可启动且控件状态正确 |
| 人工加载、暂停、跳转和可听音频 | 未完成；系统锁屏中断交互 |
| Windows Qt/FFmpeg | 环境受限，未验证 |

端到端媒体 smoke test 会打开生成的 MP4、开始播放，并要求控制器在
2.5 秒内进入 `ended`。人工未验证的交互或声音不得由该测试替代声明。
