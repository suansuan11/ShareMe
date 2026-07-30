# ShareMe

ShareMe 是面向一名主播和一名观众的桌面影片实时共享与语音通话软件。
主播持有本地片源，观众通过 WebRTC 实时观看；影片声音、主播麦克风和
观众麦克风保持独立。

项目已完成可移植 C++ 核心、构建基线、队列约束和同步决策测试，并在
macOS ARM64 上验证 FFmpeg 解码、Qt/QML 本地播放、libwebrtc 本机回环，
以及经 Qt/Go 信令建立的双进程测试视频和双向真实麦克风通话。Windows
进程音频捕获仍是后续独立技术验证。

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

## 验证状态

| 范围 | 状态 |
| --- | --- |
| macOS ARM64 可移植核心 | 本地与 Core CI 已验证 |
| Windows x64 可移植核心 | Core CI 已验证 |
| macOS ARM64 Qt/FFmpeg 本地播放 | 自动化通过；首屏人工检查通过 |
| Windows Qt/FFmpeg 播放 | 环境受限，尚未验证 |
| macOS ARM64 libwebrtc 合成音视频回环 | 自动化与 CLI 实跑通过 |
| macOS ARM64 libwebrtc 麦克风回环 | 10 秒真实麦克风验收通过 |
| Windows x64 libwebrtc | 尚未真机构建；默认 Core CI 通过 |
| Windows 进程级音频捕获 | 尚未接入 |
| 媒体性能指标 | 尚未测量 |
| 本地 Go 信令服务 | 自动化 WebSocket 集成测试及 Qt 原生客户端接入已验证 |
| macOS ARM64 Qt + WebRTC 双进程测试通话 | 双端视频与合成音频收发自动化通过 |
| macOS ARM64 Qt + WebRTC 双进程真实麦克风通话 | 双端真实麦克风 RTP 与本地音频电平验收通过 |

当前已提供本地信令服务和双进程 WebRTC 测试媒体、真实麦克风通话，运行与验证方式见
[信令基础验证](docs/verification/signaling-foundation.md)与
[WebRTC 双进程真实麦克风通话验证](docs/verification/signaled-microphone-call.md)。
下一功能阶段接入真实影片轨道与 TURN。Windows 真机 WebRTC 与进程级
音频捕获验收继续并行进行；在完成前不得宣称 Windows 原生媒体支持已经验证。

状态必须以最近一次真实构建或测试结果为准。平台、硬件或网络未参与
验证时，应明确标记为未验证或环境受限。
