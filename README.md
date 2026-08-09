# ShareMe

ShareMe 是面向一名主持人和一名参与者的高质量桌面屏幕共享与语音通话
软件。主持人共享当前屏幕，参与者通过六位房间码加入；屏幕视频与双向
语音通过 WebRTC 传输。影片分享能力保留为技术基础，但当前产品开发主线
是简洁完整的连麦屏幕共享体验。

项目已完成可移植 C++ 核心、构建基线、队列约束和同步决策测试，并在
macOS ARM64 上已验证 FFmpeg 解码、Qt/QML 本地播放、libwebrtc 本机回环，
以及经 Qt/Go 信令建立的双进程真实影片音视频和双向真实麦克风通话；
Windows 上也已完成 Qt/FFmpeg/libwebrtc 真机构建、影片与麦克风回归、
Desktop Duplication 硬件采集及本地双进程桌面传输。macOS 已完成完整 GUI、
ScreenCaptureKit、VideoToolbox H.264 和本地双端屏幕/语音自动化验收；Windows
完整 GUI 真机验收、硬件编码及两台设备人工音画验收是下一阶段重点。

## 当前范围

- Windows 为首发平台；
- C++20 与 CMake；
- Qt 6/QML 只负责界面；
- FFmpeg libraries 负责片源处理；
- libwebrtc 负责媒体传输和 DataChannel；
- Go WebSocket 服务负责信令；
- coturn 提供 STUN/TURN；
- 第一版限定一对一、1080p60 SDR、H.264 硬件编码和双向 Opus 语音。

## 构建可移植核心

前置条件：

- CMake 3.25 或更高版本；
- Ninja（macOS 和日常命令行构建）；
- 支持 C++20 的编译器（macOS 使用 Apple Clang，Windows CI 使用
  Visual Studio 2022/MSVC）。

执行完整开发工作流：

```bash
cmake --workflow --preset dev-workflow
```

也可以分步执行：

```bash
cmake --preset dev
cmake --build --preset build-dev
ctest --preset test-dev
```

Release 构建：

```bash
cmake --preset release
cmake --build --preset build-release
```

Windows Visual Studio 2022 构建：

```powershell
cmake --preset windows-dev
cmake --build --preset build-windows-dev
ctest --preset test-windows-dev
```

macOS 上构建 FFmpeg 解码与 Qt/QML 播放演示：

```bash
brew install ffmpeg qtbase qtdeclarative qtmultimedia
cmake --fresh --preset playback-dev -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build --preset build-playback-dev
ctest --preset test-playback-dev
./build/playback-dev/client/app/shareme_playback_demo.app/Contents/MacOS/shareme_playback_demo
```

只验证不含 Qt 的 FFmpeg 媒体层时，使用 `media-dev`、
`build-media-dev` 和 `test-media-dev` 三个预设。

libwebrtc 依赖按 `deps/webrtc.lock.json` 固定，并放在仓库外。依赖准备
及验证详情见
[WebRTC 测试媒体探针验证](docs/verification/webrtc-test-media-probe.md)。
依赖已准备后可运行：

```bash
cmake --fresh --preset webrtc-dev \
  -DWEBRTC_ROOT=/path/to/shareme-webrtc
cmake --build --preset build-webrtc-dev
ctest --preset test-webrtc-dev
./build/webrtc-dev/client/tools/webrtc_probe/shareme_webrtc_probe \
  --audio synthetic --seconds 3
```

Qt、FFmpeg 和 libwebrtc 默认关闭。对应技术验证通过以下选项显式启用，
不允许在缺失依赖时悄悄使用替代实现：

```text
SHAREME_ENABLE_QT
SHAREME_ENABLE_FFMPEG
SHAREME_ENABLE_WEBRTC
```

## 启动完整 ShareMe 界面

准备好仓库外 libwebrtc 依赖后，在 macOS 上构建屏幕通话版本：

```bash
cmake --preset call-dev -DWEBRTC_ROOT=/path/to/shareme-webrtc
cmake --build --preset build-call-dev
./build/call-dev/client/tools/rtc_demo/shareme_rtc_demo
```

无参数启动会打开 ShareMe 首页、创建/加入会前检查和完整通话界面。开发时
先运行本地 Go 信令服务；设置页默认连接
`ws://127.0.0.1:8080/v1/ws`。显式传入 `--server`、`--role` 等参数时仍使用
严格的自动化/诊断 CLI，不会悄悄回退到交互界面。macOS 首次共享需在系统
设置中授予屏幕录制和麦克风权限。

## 工程契约

- [架构与模块边界](docs/architecture.md)
- [信令和 DataChannel 协议](docs/protocols.md)
- [目录所有权与提交规则](docs/agent-contracts.md)
- [性能目标与测量口径](docs/performance-targets.md)
- [阶段 0 基础设计](docs/superpowers/specs/2026-07-28-phase0-foundation-design.md)
- [阶段 0 基础实施计划](docs/superpowers/plans/2026-07-28-phase0-foundation.md)
- [Qt + FFmpeg 播放设计](docs/superpowers/specs/2026-07-28-qt-ffmpeg-playback-design.md)
- [Qt + FFmpeg 播放验证](docs/verification/qt-ffmpeg-playback.md)
- [WebRTC 测试媒体探针验证](docs/verification/webrtc-test-media-probe.md)
- [WebRTC 双进程信令通话验证](docs/verification/webrtc-signaled-call.md)
- [WebRTC 双进程真实麦克风通话验证](docs/verification/signaled-microphone-call.md)
- [WebRTC 双进程真实影片视频验证](docs/verification/signaled-movie-video.md)
- [WebRTC 双进程真实影片音频验证](docs/verification/signaled-movie-audio.md)
- [Windows 跨平台媒体回归验证](docs/verification/windows-cross-platform-regression.md)
- [Windows Desktop Duplication 桌面共享验证](docs/verification/windows-desktop-duplication.md)
- [主机播放控制验证](docs/verification/host-playback-controls.md)
- [完整 GUI 验证](docs/verification/complete-gui.md)

## 验证状态

| 范围 | 状态 |
| --- | --- |
| macOS ARM64 可移植核心 | 本地与 Core CI 已验证 |
| Windows x64 可移植核心 | Core CI 已验证 |
| macOS ARM64 Qt/FFmpeg 本地播放 | 自动化通过；首屏人工检查通过 |
| Windows Qt/FFmpeg 播放 | Windows 真机构建与影片回归通过；组合配置 36/36 CTest 通过 |
| macOS ARM64 libwebrtc 合成音视频回环 | 自动化与 CLI 实跑通过 |
| macOS ARM64 libwebrtc 麦克风回环 | 10 秒真实麦克风验收通过 |
| Windows x64 libwebrtc | MSVC 真机构建、16/16 通话配置及本地双进程音视频/麦克风通话通过 |
| Windows Desktop Duplication 桌面共享 | 38/38 组合配置、4K HDR 硬件采集及本地双进程接收通过 |
| Windows 进程级音频捕获 | 尚未接入 |
| 媒体性能指标 | 尚未测量 |
| 本地 Go 信令服务 | 自动化 WebSocket 集成测试及 Qt 原生客户端接入已验证 |
| macOS ARM64 Qt + WebRTC 双进程测试通话 | 双端视频与合成音频收发自动化通过 |
| macOS ARM64 Qt + WebRTC 双进程真实麦克风通话 | 双端真实麦克风 RTP 与本地音频电平验收通过 |
| macOS ARM64 FFmpeg + Qt + WebRTC 真实影片视频 | 主播独占片源，观众端收到解码影片帧并通过自动化验收 |
| macOS ARM64 FFmpeg + Qt + WebRTC 独立影片音频 | 观众端收到 48 kHz 双声道 Opus 解码 PCM，且主播发送端影片音视频 PTS 偏差通过自动化验收 |
| macOS ARM64 接收端播放状态通道 | 可靠有序数据通道、真实双 Peer 状态收发与 37/37 组合测试通过；GUI 视觉验收受当前捕获环境限制 |
| macOS ARM64 主机影片播放控制 | 共享时间线、独立音视频暂停/继续/前后跳转、generation 状态发布与 38/38 组合测试通过；QML 已构建但视觉验收未执行 |
| macOS ARM64 完整屏幕通话 GUI | 首页、会前检查、通话舞台、音频控制、详情、离开/恢复自动化通过；真实 H.264 屏幕与双向语音计数通过 |
| Windows 完整屏幕通话 GUI | 代码可移植性门禁通过；Windows 真机 GUI、硬件编码和两设备人工音画尚未验证 |

当前已提供本地信令服务和双进程 WebRTC 测试媒体、真实麦克风通话及
独立影片音视频轨道，运行与验证方式见
[信令基础验证](docs/verification/signaling-foundation.md)与
[WebRTC 双进程真实影片音频验证](docs/verification/signaled-movie-audio.md)，
首个接收端播放状态阶段见
[播放器/接收端控制验证](docs/verification/player-receiver-control.md)，主机端
暂停、继续和前后跳转见
[主机播放控制验证](docs/verification/host-playback-controls.md)，当前完整屏幕
通话界面见[完整 GUI 验证](docs/verification/complete-gui.md)。现有仓库外缓存
的 libwebrtc 构建依赖仍用于这些验证并已保留。下一阶段应优先完成 Windows
原生屏幕/语音与完整 GUI 真机对齐；TURN、Windows
进程级音频捕获、持续性能测量和跨机器公网验收仍需后续完成。已有 Windows 结论仅覆盖上述真机
构建、媒体回归、硬件桌面采集和本地双进程范围，不代表这些后续项已验证。

状态必须以最近一次真实构建或测试结果为准。平台、硬件或网络未参与
验证时，应明确标记为未验证或环境受限。
