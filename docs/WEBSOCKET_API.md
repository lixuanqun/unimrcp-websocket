# UniMRCP WebSocket 接口对接文档

本文档描述 UniMRCP WebSocket 插件与外部 TTS/ASR 服务的通信协议。

## 目录

1. [概述](#1-概述)
2. [WebSocket 连接](#2-websocket-连接)
3. [TTS 接口协议](#3-tts-接口协议)
4. [ASR 接口协议](#4-asr-接口协议)
5. [错误处理](#5-错误处理)
6. [最佳实践](#6-最佳实践)

---

## 1. 概述

### 1.1 架构图

```
┌─────────────────┐     MRCP      ┌─────────────────┐    WebSocket    ┌─────────────────┐
│   MRCP Client   │ ◄──────────► │  UniMRCP Server │ ◄─────────────► │   TTS/ASR 服务   │
│  (IVR/FreeSWITCH)│              │  (插件宿主)      │                │   (你的服务)     │
└─────────────────┘              └─────────────────┘                └─────────────────┘
```

### 1.2 插件说明

| 插件 | 功能 | WebSocket 角色 |
|------|------|----------------|
| websocket-synth | TTS 语音合成 | 客户端 (发送文本，接收音频) |
| websocket-recog | ASR 语音识别 | 客户端 (发送音频，接收文本) |

### 1.3 配置参数

在 `unimrcpserver.xml` 中配置：

```xml
<!-- TTS 插件 -->
<engine id="WebSocket-Synth-1" name="websocketsynth" enable="true">
  <param name="ws-host" value="localhost"/>
  <param name="ws-port" value="8080"/>
  <param name="ws-path" value="/tts"/>
</engine>

<!-- ASR 插件 -->
<engine id="WebSocket-Recog-1" name="websocketrecog" enable="true">
  <param name="ws-host" value="localhost"/>
  <param name="ws-port" value="8080"/>
  <param name="ws-path" value="/asr"/>
</engine>
```

---

## 2. WebSocket 连接

### 2.1 连接建立

插件作为 WebSocket 客户端连接到你的服务：

```
GET /tts HTTP/1.1
Host: localhost:8080
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: <base64-encoded-key>
Sec-WebSocket-Version: 13
```

### 2.2 连接生命周期

```
1. 通道创建时建立连接
2. 每个 SPEAK/RECOGNIZE 请求复用连接
3. 通道关闭时断开连接
```

### 2.3 心跳机制

- 插件会响应服务端发送的 Ping 帧
- 建议服务端定期发送 Ping 保持连接活跃

---

## 3. TTS 接口协议

### 3.1 请求格式

**方向**: UniMRCP → TTS 服务

**帧类型**: Text (opcode 0x01)

**格式**: JSON

```json
{
    "action": "tts",
    "text": "要合成的文本内容",
    "voice": "speaker_name",
    "speed": 1.0,
    "pitch": 1.0,
    "volume": 1.0,
    "sample_rate": 8000,
    "format": "pcm",
    "session_id": "unique_session_id"
}
```

### 3.2 请求字段说明

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| action | string | 是 | - | 固定值 "tts" |
| text | string | 是 | - | 要合成的文本，已转义特殊字符 |
| voice | string | 否 | "default" | 发音人名称 |
| speed | float | 否 | 1.0 | 语速 (0.5-2.0) |
| pitch | float | 否 | 1.0 | 音调 (0.5-2.0) |
| volume | float | 否 | 1.0 | 音量 (0.0-1.0) |
| sample_rate | int | 否 | 8000 | 采样率 (8000 或 16000) |
| format | string | 否 | "pcm" | 音频格式，固定 "pcm" |
| session_id | string | 否 | "" | 会话标识符 |

### 3.3 响应格式

**方向**: TTS 服务 → UniMRCP

#### 3.3.1 音频数据帧

**帧类型**: Binary (opcode 0x02)

**格式**: 原始 PCM 音频

| 属性 | 值 |
|------|-----|
| 编码 | 16-bit 有符号整数 |
| 字节序 | Little-Endian |
| 通道数 | 1 (单声道) |
| 采样率 | 与请求中的 sample_rate 一致 |

**分帧建议**:
- 每帧 20ms 音频数据
- 8kHz: 320 字节/帧
- 16kHz: 640 字节/帧

#### 3.3.2 完成消息

**帧类型**: Text (opcode 0x01)

**格式**: JSON

```json
{
    "status": "complete"
}
```

或发送 WebSocket Close 帧表示结束。

### 3.4 时序图

```
UniMRCP                          TTS Service
   │                                  │
   │──── WebSocket Connect ──────────►│
   │◄─── 101 Switching Protocols ─────│
   │                                  │
   │──── TTS Request (JSON) ─────────►│
   │                                  │
   │◄─── Audio Frame 1 (Binary) ──────│
   │◄─── Audio Frame 2 (Binary) ──────│
   │◄─── Audio Frame N (Binary) ──────│
   │                                  │
   │◄─── Complete (JSON) ─────────────│
   │                                  │
```

---

## 4. ASR 接口协议

### 4.1 音频数据帧

**方向**: UniMRCP → ASR 服务

**帧类型**: Binary (opcode 0x02)

**格式**: 原始 PCM 音频

| 属性 | 值 |
|------|-----|
| 编码 | 16-bit 有符号整数 |
| 字节序 | Little-Endian |
| 通道数 | 1 (单声道) |
| 采样率 | 8000 或 16000 Hz |

**说明**:
- 音频在语音活动结束后批量发送
- 包含从语音活动开始到结束的完整音频

### 4.2 识别结果格式

**方向**: ASR 服务 → UniMRCP

**帧类型**: Text (opcode 0x01)

**格式**: JSON 或 NLSML

#### 4.2.1 简单 JSON 格式

```json
{
    "status": "success",
    "text": "识别出的文本",
    "confidence": 0.95
}
```

#### 4.2.2 NLSML 格式 (推荐)

```xml
<?xml version="1.0"?>
<result>
  <interpretation grammar="session:grammar" confidence="0.95">
    <instance>识别出的文本</instance>
    <input mode="speech">识别出的文本</input>
  </interpretation>
</result>
```

### 4.3 响应字段说明

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| status | string | 是 | "success" 或 "error" |
| text | string | 是 | 识别结果文本 |
| confidence | float | 否 | 置信度 (0.0-1.0) |

### 4.4 时序图

```
UniMRCP                          ASR Service
   │                                  │
   │──── WebSocket Connect ──────────►│
   │◄─── 101 Switching Protocols ─────│
   │                                  │
   │     [等待语音活动检测]            │
   │                                  │
   │──── Audio Data (Binary) ────────►│
   │                                  │
   │◄─── Recognition Result ──────────│
   │     (JSON/NLSML)                 │
   │                                  │
```

---

## 5. 错误处理

### 5.1 错误响应格式

```json
{
    "status": "error",
    "code": "ERROR_CODE",
    "message": "错误描述"
}
```

### 5.2 错误码定义

| 错误码 | 说明 |
|--------|------|
| INVALID_REQUEST | 请求格式错误 |
| TEXT_EMPTY | 文本为空 |
| VOICE_NOT_FOUND | 发音人不存在 |
| AUDIO_ERROR | 音频处理错误 |
| TIMEOUT | 处理超时 |
| INTERNAL_ERROR | 内部错误 |

### 5.3 连接错误处理

| 情况 | 插件行为 |
|------|----------|
| 连接失败 | 返回 MRCP 失败响应 |
| 连接断开 | 尝试重新连接 |
| 超时 | 发送 RECOGNITION-COMPLETE (timeout) |

---

## 6. 最佳实践

### 6.1 TTS 服务实现建议

1. **流式输出**: 边合成边发送音频帧，减少首字延迟
2. **缓冲区管理**: 控制每帧大小为 20ms
3. **错误恢复**: 发送错误消息而非直接断开连接
4. **资源清理**: 合成完成后及时释放资源

### 6.2 ASR 服务实现建议

1. **流式识别**: 支持边接收边识别
2. **VAD 集成**: 服务端也可实现 VAD 获得更好效果
3. **超时处理**: 设置合理的识别超时
4. **中间结果**: 可发送中间识别结果 (可选)

### 6.3 性能优化

1. **连接复用**: 保持长连接，避免频繁握手
2. **压缩**: 考虑使用 permessage-deflate 扩展
3. **负载均衡**: 多实例部署时使用负载均衡

---

## 附录 A: 音频格式详情

### PCM 格式说明

| 参数 | 8kHz | 16kHz |
|------|------|-------|
| 采样率 | 8000 Hz | 16000 Hz |
| 位深 | 16 bit | 16 bit |
| 通道 | Mono | Mono |
| 每秒字节数 | 16000 | 32000 |
| 20ms 帧大小 | 320 bytes | 640 bytes |

### 字节序

所有音频数据使用 **Little-Endian** 字节序。

---

## 附录 B: 示例代码

请参考 `examples/` 目录下的各语言实现：

- `examples/python/` - Python 实现
- `examples/nodejs/` - Node.js 实现
- `examples/java/` - Java 实现
- `examples/go/` - Go 实现
