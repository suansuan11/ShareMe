# ShareMe

ShareMe 是面向一名主播和一名观众的桌面影片实时共享与语音通话软件。
主播持有本地片源，观众通过 WebRTC 实时观看；影片声音、主播麦克风和
观众麦克风保持独立。

项目当前处于阶段 0。已经建立可移植 C++ 核心、构建基线、队列约束和
同步决策测试；FFmpeg/Qt 播放、WebRTC 测试流及 Windows 进程音频捕获
仍是后续独立技术验证，不应视为已经实现。

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
- Ninja；
- 支持 C++20 的编译器（macOS 使用 Apple Clang，Windows 使用
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

Qt、FFmpeg 和 libwebrtc 默认关闭。对应技术验证接入后，通过以下选项
显式启用，不允许在缺失依赖时悄悄使用替代实现：

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

## 验证状态

| 范围 | 状态 |
| --- | --- |
| macOS ARM64 可移植核心 | 本地已验证；Core CI 待首次运行 |
| Windows x64 可移植核心 | Core CI 待首次运行 |
| Windows Qt/FFmpeg/libwebrtc | 尚未接入 |
| Windows 进程级音频捕获 | 尚未接入 |
| 媒体性能指标 | 尚未测量 |

状态必须以最近一次真实构建或测试结果为准。平台、硬件或网络未参与
验证时，应明确标记为未验证或环境受限。
