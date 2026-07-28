## 一、定义产品

你的产品不是普通远程控制软件，而是一个面向两人场景的：

> **本地影片实时共享 + 双向语音通话客户端**

核心场景：

- 只有主播拥有影片文件。
- 主播在软件中打开影片并播放。
- 观众无需下载片源，直接接收实时视频流和影片声音。
- 双方同时进行低延迟语音通话。
- 影片声音、主播麦克风、观众麦克风必须完全隔离。
- 主播与观众看到的影片进度需要尽可能一致。

需要特别说明：“原画”如果指比特级无损，在普通家庭上行带宽和实时传输环境下基本不可行。合理目标应是：

> **通过硬件编码获得视觉上接近原片的画质，同时将延迟控制在双方能够自然交流的范围内。**

------

# 二、完整需求定义

## 1. 用户角色

### 主播端

负责：

- 创建房间并生成邀请码。
- 选择本地影片。
- 播放、暂停、拖动进度和切换字幕。
- 发送影片画面、影片声音和麦克风声音。
- 接收观众麦克风声音。
- 查看网络质量、实际码率、丢包率和延迟。

### 观众端

负责：

- 输入邀请码加入房间。
- 接收影片画面和影片声音。
- 发送自己的麦克风声音。
- 分别调整影片音量和通话音量。
- 全屏播放、切换显示比例。
- 查看当前画质和网络状态。

------

## 2. 核心功能

| 模块       | 功能                                           |
| ---------- | ---------------------------------------------- |
| 房间       | 创建房间、加入房间、临时邀请码、离开房间       |
| 影片播放   | 选择文件、播放、暂停、进度跳转、音轨和字幕选择 |
| 实时共享   | 1080p/1440p/4K，30/60 FPS，可调码率            |
| 语音通话   | 双向麦克风、静音、降噪、回声消除、输入设备选择 |
| 音频隔离   | 影片、主播语音、观众语音三条独立音频路径       |
| 音量控制   | 影片音量、对方语音音量、本地监听音量分别控制   |
| 同步控制   | 主播与观众影片时间轴偏差检测和自动纠偏         |
| 网络自适应 | 根据带宽、丢包和 RTT 自动调整码率或分辨率      |
| 状态监控   | 码率、FPS、编解码器、丢帧、RTT、抖动、丢包率   |
| 故障恢复   | 断线重连、ICE 重启、关键帧请求、设备切换恢复   |

------

# 三、性能验收指标

下面是合理的第一阶段目标，不是理论极限。

| 指标                 | 目标                        |
| -------------------- | --------------------------- |
| 默认画质             | 1080p 60 FPS                |
| 高画质               | 1440p 60 FPS                |
| 实验画质             | 4K 60 FPS                   |
| 语音单向延迟         | 直连 P50 ≤120ms，P95 ≤200ms |
| TURN 中继语音延迟    | 尽量 ≤250ms                 |
| 影片端到端延迟       | 低延迟模式 150～300ms       |
| 高画质模式延迟       | 250～500ms                  |
| 影片音画同步误差     | ≤50ms                       |
| 主播与观众影片进度差 | 稳定后 ≤80～100ms           |
| 稳定网络下丢帧率     | ≤1%                         |
| 断线自动恢复         | 5 秒内尝试恢复              |
| 音频采样             | 48kHz                       |
| 影片音频             | Opus 双声道 128～192kbps    |
| 语音音频             | Opus 单声道 32～64kbps      |

Opus 原生支持语音和立体声音乐；其规范将全频带立体声音乐的常见优质码率区间列为 64～128kbps，所以影片音轨使用 128kbps 起步、提供 192kbps 高音质档比较合理。[RFC 编辑器](https://www.rfc-editor.org/info/rfc6716/?utm_source=chatgpt.com)

------

# 四、最关键的架构决策

## 1. 主模式：影片直推，而不是捕获屏幕

这是整个项目最重要的设计。

### 不推荐的传统流程

```
播放器解码影片
→ 显示到桌面
→ 捕获桌面画面
→ RGB/YUV 转换
→ 再次编码
→ 网络传输
```

它存在：

- 多一次桌面合成。
- 可能捕获通知、鼠标和其他窗口。
- HDR、缩放和色彩空间更难控制。
- 系统音频需要额外环回采集。
- 更容易把通话声音重复传回去。

### 推荐流程

```
本地影片文件
    ↓
FFmpeg 解封装
    ↓
硬件解码
    ↓
GPU 视频帧
    ├── 本地主播渲染
    └── 硬件重新编码 → WebRTC → 观众

影片音频
    ↓
解码为 PCM
    ├── 主播本地播放
    └── Opus 编码 → WebRTC → 观众
```

FFmpeg 能处理大量容器、视频、音频和字幕流，并提供硬件加速接口，适合作为片源读取和解码基础。[FFmpeg](https://www.ffmpeg.org/ffmpeg.html?utm_source=chatgpt.com)

这种方案仍然是实时转码，不需要朋友持有文件，但相比普通屏幕捕获：

- 画质更稳定。
- 字幕可以直接烧录到发送画面。
- 影片声音直接来自解码器，不经过系统音量混音。
- 不会捕获 QQ、系统提示音或朋友语音。
- 可以精确获取每帧 PTS，用于同步。
- 更容易实现 GPU 零拷贝。

------

## 2. 兜底模式：窗口或桌面共享

需要共享网页、播放器或桌面时，再启用捕获模式。

Windows 使用：

- `Windows.Graphics.Capture` 捕获窗口或显示器。
- D3D11 纹理传递。
- WASAPI Process Loopback 只捕获目标播放器进程。

Windows Graphics Capture 可以直接取得显示器或应用窗口帧；Windows 的进程级音频环回机制则支持只包含或排除指定进程及其子进程的声音。[Microsoft Learn](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture?utm_source=chatgpt.com)

例如：

```
捕获 mpv.exe 的画面
捕获 mpv.exe 的声音
排除本软件语音播放
排除 QQ、微信和系统提示
```

macOS 后续版本使用 ScreenCaptureKit。它支持高性能画面和音频捕获，并允许排除当前应用进程的声音。[Apple Developer](https://developer.apple.com/documentation/screencapturekit?changes=_1&utm_source=chatgpt.com)

------

# 五、推荐技术栈

## 客户端

### 第一选择

```
C++20
Qt 6 + QML
libwebrtc
FFmpeg libraries
D3D11 / DirectComposition
WASAPI
NVENC / AMD AMF / Intel VPL
```

### 为什么选择 C++ 而不是 Electron

这个项目的性能瓶颈在：

- GPU 纹理传递。
- 硬件编解码。
- Windows 原生捕获。
- 音频缓冲区。
- WebRTC Native API。
- 零拷贝处理。

libwebrtc 本身就是大型原生 C++ 工程，支持 Windows、macOS、Linux、Android 和 iOS。使用 C++ 能减少跨语言拷贝和绑定层复杂度。[WebRTC Git Repositories](https://webrtc.googlesource.com/src/%2B/5da0f2ef2a247da9835bc894a6c2e5517e887b4b/docs/native-code/development/?utm_source=chatgpt.com)

Qt 只负责：

- 登录和房间界面。
- 播放控制。
- 视频渲染区域。
- 音量与设备设置。
- 网络状态面板。

媒体逻辑全部放在独立的 C++ Core 中，不要写进 QML。

------

## 服务端

```
Go
├── HTTP API
├── WebSocket 信令
├── 房间状态
├── 临时 Token
└── ICE 配置下发

coturn
├── STUN
└── TURN UDP/TCP/TLS
```

一对一场景优先使用 WebRTC P2P：

```
主播客户端 ←──── WebRTC UDP ────→ 观众客户端
```

只有无法直连时：

```
主播客户端 ←→ coturn ←→ 观众客户端
```

WebRTC 本身负责媒体连接、拥塞控制、抖动缓冲、加密和 ICE，但不规定信令实现，因此需要单独的 WebSocket 或 HTTP 信令服务。实际公网环境也通常需要 TURN 作为直连失败时的中继。[WebRTC](https://webrtc.org/getting-started/peer-connections?hl=en&utm_source=chatgpt.com)

第一版服务端不需要 Spring Boot、MySQL 或复杂微服务。房间生命周期很短，可以直接存内存；需要多实例时再引入 Redis。

------

# 六、媒体轨道设计

一个 PeerConnection 中传输以下轨道：

```
Video Track
└── 影片画面或共享画面

Media Audio Track
└── 影片立体声音频

Host Voice Track
└── 主播麦克风

Viewer Voice Track
└── 观众麦克风

Data Channel
└── 播放状态、延迟报告、控制消息
```

## 影片声音

处理原则：

- 48kHz。
- 双声道。
- Opus music 模式。
- 不开启回声消除。
- 不开启噪声抑制。
- 不开启自动增益。
- 远端有独立音量控制。
- 编码前提供数字增益和限幅器。

## 麦克风声音

处理原则：

- 48kHz 单声道。
- 10ms 音频帧。
- 开启 AEC、NS、AGC。
- 使用 WebRTC Audio Processing Module。
- 语音轨道优先级高于视频。
- 网络拥堵时先降低视频，不牺牲语音。

## 防止重复声音

直推模式下，影片声音直接来自文件解码器，因此天然不会包含朋友语音。

窗口共享模式下：

```
只采集播放器进程音频
不采集整个系统混音
```

此外，麦克风回声消除的参考信号应该包含：

```
本地播放的影片声音
+
对方通话声音
```

否则使用外放时，麦克风可能再次拾取影片声音。即使实现完整 AEC，实际使用仍建议双方戴耳机，因为大音量电影对白和环境混响会增加回声消除难度。

------

# 七、视频编码策略

## 编码器优先级

```
1. AV1 硬件编码：双方显卡均支持时启用
2. H.264 硬件编码：默认兼容模式
3. 软件 H.264：最后兜底
```

HEVC 可以放在后续阶段，不建议作为第一版默认：

- WebRTC 集成兼容性更复杂。
- 许可证和终端支持需要额外评估。
- H.264 更容易完成稳定的 MVP。
- AV1 更适合作为现代高画质选项。

NVIDIA NVENC、AMD AMF 和 Intel VPL 都提供硬件视频编解码接口。NVIDIA 和 AMD 当前接口覆盖 H.264、HEVC 和 AV1；Intel VPL 也提供硬件加速的视频解码、编码和处理能力。[NVIDIA Developer](https://developer.nvidia.com/video-codec-sdk?source=post_page-----ec442ad2fb66----------------------&utm_source=chatgpt.com)

## 建议预设

以下是工程初始值，最终必须根据真实网络测试调整。

| 档位     | 分辨率    | 帧率 | 建议码率   |
| -------- | --------- | ---- | ---------- |
| 流畅     | 1920×1080 | 30   | 6～10Mbps  |
| 默认     | 1920×1080 | 60   | 10～18Mbps |
| 清晰     | 2560×1440 | 60   | 18～30Mbps |
| 4K AV1   | 3840×2160 | 60   | 25～45Mbps |
| 4K H.264 | 3840×2160 | 60   | 40～70Mbps |

主播稳定上行建议至少为目标码率的 1.5 倍，并以实际持续上行而不是运营商标称速度为准。

## 低延迟编码参数

- 关闭 B 帧。
- 关闭 Lookahead。
- 禁用多遍高延迟分析。
- GOP 约 1～2 秒。
- 支持观众主动请求关键帧。
- 使用 CBR 或低延迟 VBR。
- 编码队列最多保留 1～2 帧。
- 拥塞时丢弃旧帧，不允许积压成数秒延迟。
- GPU 纹理直接进入硬件编码器。

AMD AMF 本身提供 `ultralowlatency`、`lowlatency` 等实时编码预设，并明确区分质量和延迟取舍。[GitHub](https://github.com/GPUOpen-LibrariesAndSDKs/AMF/wiki/AMF-Encoder-Settings-and-Tuning-in-FFmpeg?utm_source=chatgpt.com)

------

# 八、真正的“双方同步”机制

只保证音画同步还不够。

普通直播中：

```
主播本地看到画面：0ms
观众看到画面：约 200～500ms 后
双方语音：约 80～180ms
```

这样主播针对某个画面说话时，观众可能还没有看到该画面。

## 推荐解决方案：延迟主播本地播放

主播和观众都以影片 PTS 为共同时间轴。

观众每 250ms 通过 DataChannel 上报：

```
{
  "type": "playout-report",
  "renderedPtsMs": 125430,
  "bufferMs": 160,
  "receiveTimeMs": 9865321
}
```

主播掌握：

- 当前送出的影片 PTS。
- 自己本地渲染的 PTS。
- 观众实际已经显示的 PTS。
- 网络 RTT。
- 观众抖动缓冲长度。

然后动态调整主播本地渲染缓冲：

```
观众比主播落后 260ms
→ 主播本地播放延迟增加到约 260ms
→ 双方看到相近的影片时间点
→ 双方语音仍保持实时
```

不要延迟语音去匹配影片。应该延迟主播本地影片去匹配观众，因为自然通话比主播端零延迟预览更重要。

## 同步控制规则

- 差异小于 50ms：不处理。
- 50～120ms：缓慢调整本地渲染队列。
- 120～300ms：临时调整 0.98～1.02 倍速。
- 超过 300ms：清空旧帧并跳转至目标 PTS。
- 网络恢复后逐渐减少缓冲，避免突然跳动。
- 音频始终作为影片音画同步的主时钟。

------

# 九、代码仓库规划

```
ShareMe/
├── client/
│   ├── app/                    # Qt/QML 应用
│   ├── core/
│   │   ├── room/               # 房间状态机
│   │   ├── signaling/          # WebSocket 信令
│   │   ├── rtc/                # libwebrtc 封装
│   │   ├── sync/               # PTS 和播放同步
│   │   └── metrics/            # 网络与性能指标
│   ├── media/
│   │   ├── demux/              # FFmpeg 解封装
│   │   ├── decode/             # 硬件/软件解码
│   │   ├── render/             # D3D11 渲染
│   │   ├── encode/             # NVENC/AMF/VPL
│   │   ├── subtitle/           # 字幕渲染
│   │   └── audio/              # PCM、Opus、设备管理
│   ├── capture/
│   │   ├── windows_graphics/
│   │   └── process_loopback/
│   └── platform/
│       ├── windows/
│       └── macos/
├── server/
│   ├── cmd/signaling/
│   ├── internal/room/
│   ├── internal/auth/
│   ├── internal/ws/
│   └── internal/ice/
├── deploy/
│   ├── docker-compose.yml
│   ├── coturn/
│   └── nginx/
├── tests/
│   ├── network/
│   ├── avsync/
│   ├── codec/
│   └── endurance/
└── docs/
    ├── architecture.md
    ├── protocols.md
    ├── performance-targets.md
    └── agent-contracts.md
```

------

# 十、多 Agent 开发方案

## Agent 0：架构与集成负责人

职责：

- 固定模块边界。
- 维护接口契约。
- 审核跨模块改动。
- 控制依赖版本。
- 管理主分支合并。
- 每个阶段执行集成测试。

不得直接大规模编写具体媒体模块，避免和其他 Agent 冲突。

交付物：

```
docs/architecture.md
docs/protocols.md
docs/agent-contracts.md
CMakePresets.json
根目录构建脚本
CI 工作流
```

------

## Agent 1：媒体文件与播放管线

负责：

- FFmpeg 解封装。
- 音视频轨道选择。
- PTS 计算。
- 暂停、播放、Seek。
- 字幕读取和烧录。
- GPU 解码。
- 主播本地播放队列。

验收标准：

- MKV、MP4、MOV 可播放。
- H.264、HEVC、AV1 片源可按硬件能力解码。
- Seek 后 500ms 内恢复画面。
- 长时间播放音画漂移小于 50ms。
- 能输出统一的视频帧和 PCM 接口。

------

## Agent 2：硬件编码与视频发送

负责：

- 统一 `IVideoEncoder` 接口。
- NVENC 实现。
- AMF 实现。
- Intel VPL 实现。
- 软件编码兜底。
- 动态码率修改。
- 强制关键帧。
- GPU 零拷贝。

接口示例：

```
class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;

    virtual bool initialize(const EncoderConfig& config) = 0;
    virtual EncodeResult encode(const GpuVideoFrame& frame) = 0;
    virtual void setBitrate(uint32_t bitrateBps) = 0;
    virtual void requestKeyFrame() = 0;
    virtual void flush() = 0;
};
```

验收标准：

- 1080p60 编码不持续占用大量 CPU。
- 运行中切换码率不重建房间。
- 不支持硬件编码时自动降级。
- 编码队列不会无限增长。

------

## Agent 3：WebRTC 与网络传输

负责：

- libwebrtc 集成。
- Offer、Answer、ICE 信令。
- 视频、影片音频和语音轨道。
- DataChannel。
- ICE Restart。
- 网络统计。
- 带宽估计与码率反馈。
- TURN 回退。

验收标准：

- 局域网可直连。
- 不同公网环境可通过 TURN 建立连接。
- 网络切换后自动恢复。
- 音频优先于视频。
- 可以读取 RTT、丢包、抖动和可用带宽。

------

## Agent 4：音频与通话

负责：

- 麦克风采集。
- 播放设备选择。
- WebRTC APM。
- 影片音频 Opus 编码。
- 麦克风 Opus 编码。
- 混音和独立音量。
- 回声参考信号。
- 进程级音频捕获。

验收标准：

- 影片和语音音量独立。
- 对方语音不会进入影片轨道。
- 静音、设备拔插和切换可恢复。
- 戴耳机时无可感知回声。
- 外放时有可用的回声抑制效果。
- 影片音频保持双声道。

------

## Agent 5：同步算法

负责：

- 统一媒体时钟。
- RTP 时间戳与影片 PTS 映射。
- 观众渲染进度反馈。
- 主播本地延迟队列。
- 缓慢纠偏与硬纠偏。
- 暂停、恢复和 Seek 同步。

验收标准：

- 稳定网络下双方影片差异 ≤100ms。
- 暂停和恢复操作差异 ≤150ms。
- Seek 后双方在 1 秒内重新稳定。
- 不因纠偏产生频繁画面跳动。

------

## Agent 6：Qt 客户端界面

负责：

- 创建和加入房间。
- 文件选择。
- 播放控制。
- 全屏。
- 音轨和字幕选择。
- 音量控制。
- 网络状态。
- 设备设置。
- 错误提示。

约束：

- QML 不直接操作 libwebrtc。
- QML 不直接处理媒体帧。
- 所有耗时任务在后台线程。
- UI 只能通过稳定的 Facade 接口调用 Core。

------

## Agent 7：服务端与部署

负责：

- Go WebSocket 信令服务。
- 临时房间。
- 邀请码。
- 一次性身份 Token。
- ICE Server 配置。
- coturn 部署。
- TLS。
- Docker Compose。
- 限流和基础日志。

验收标准：

- 房间关闭后状态及时清理。
- Token 短期有效。
- WebSocket 断线可以恢复房间状态。
- TURN UDP、TCP 和 TLS 均可测试。
- 服务端不接收和存储影片内容。

------

## Agent 8：测试与性能负责人

负责构造：

- 0%～10% 丢包。
- 20～200ms RTT。
- 带宽突然下降。
- Wi-Fi 切换。
- TURN 中继。
- 音频设备拔插。
- 2 小时和 8 小时稳定性测试。

必须输出：

```
连接建立时间
实际码率
视频编码耗时
视频端到端延迟
语音端到端延迟
音画同步误差
主播/观众时间轴差
CPU/GPU/内存占用
丢帧与卡顿次数
```

------

# 十一、Agent 执行顺序

不能让所有 Agent 同时自由编码，否则接口必然失控。

## 阶段 0：技术验证

只做三个独立 Demo：

1. FFmpeg 解码并在 Qt 中播放。
2. WebRTC 发送测试图像和麦克风。
3. Windows 进程级音频捕获。

通过后再进入主工程。

## 阶段 1：最小通话系统

实现：

```
创建房间
加入房间
双向麦克风
测试视频流
P2P + TURN
```

暂时不接入真实影片。

## 阶段 2：影片直推

实现：

```
打开影片
解码
本地播放
H.264 硬件编码
发送影片画面
发送影片声音
```

此阶段只支持 1080p60 SDR。

## 阶段 3：完整音频隔离

实现：

```
影片音频轨道
主播麦克风轨道
观众麦克风轨道
独立音量
AEC/NS/AGC
```

## 阶段 4：同步算法

增加：

- 观众渲染 PTS 上报。
- 主播本地播放延迟。
- 自动纠偏。
- Seek 同步。
- 网络抖动恢复。

## 阶段 5：质量和自适应

增加：

- 动态码率。
- 1080p/1440p 切换。
- AV1 能力检测。
- 关键帧请求。
- 低延迟和高画质预设。
- 实时统计面板。

## 阶段 6：窗口共享兜底

增加：

- Windows Graphics Capture。
- 目标进程音频捕获。
- 窗口、显示器选择。
- 排除本软件音频。

## 阶段 7：HDR 与 macOS

最后处理：

- HDR10/10bit。
- HDR→SDR 色调映射。
- AV1/HEVC 10bit。
- macOS ScreenCaptureKit。
- VideoToolbox 编解码。

HDR 不应该进入第一版。实时 HDR 涉及 10bit 像素格式、色彩原色、传递函数、静态或动态元数据、发送端与接收端显示能力匹配等问题，很容易出现过曝、灰暗或色彩错误。

------

# 十二、给总控 Agent 的主提示词

```
你是该项目的总架构与集成 Agent。

项目目标：
开发一个面向一名主播和一名观众的桌面影片实时共享与语音通话软件。只有主播拥有片源，观众通过实时网络流观看。优先保证画质、音画同步、双方影片进度同步以及语音实时性。

固定技术方案：
1. Windows 第一阶段。
2. C++20 + CMake。
3. Qt 6/QML 仅负责 UI。
4. FFmpeg libraries 负责解封装、解码、音轨和字幕。
5. libwebrtc 负责 P2P 媒体传输、DataChannel 和拥塞控制。
6. NVENC、AMD AMF、Intel VPL 提供硬件编码，H.264 为默认。
7. Go 提供 WebSocket 信令服务。
8. coturn 提供 STUN/TURN。
9. 主模式是影片文件直接解码、渲染、硬件重编码和发送。
10. Windows Graphics Capture 与进程音频捕获只作为兜底模式。
11. 影片音频、主播麦克风、观众麦克风必须是独立轨道。
12. AEC、NS、AGC 只能应用于麦克风轨道。
13. 音频优先于视频。
14. 禁止媒体队列无限积压，拥塞时丢弃旧视频帧。
15. 主播本地影片播放需要根据观众渲染 PTS 动态延迟，以保证双方看到相近时间点。

工作要求：
- 先维护 docs/architecture.md、docs/protocols.md 和模块接口。
- 每个 Agent 只能修改自己的目录。
- 跨模块接口必须先提交契约变更，再提交实现。
- 每个功能必须包含单元测试、集成测试和性能指标。
- 禁止用伪实现代替关键媒体链路。
- 禁止默认回退到 CPU 编码而不提示用户。
- 所有线程、队列和缓冲区必须有明确的容量与生命周期。
- 每次合并前执行 Windows Release 构建和端到端连接测试。
```

## 最终落地建议

第一版严格控制在：

```
Windows
一对一房间
1080p60 SDR
H.264 硬件编码
影片直推
双向 Opus 语音
P2P + TURN
独立音量
基础同步纠偏
```

不要一开始做多人、网页端、4K HDR、跨平台和账号社交系统。先把以下闭环做稳定：

> **朋友能加入房间，清晰看到影片，正常听到影片声音，双方可以自然对话，且不会出现重复声音和明显进度差。**