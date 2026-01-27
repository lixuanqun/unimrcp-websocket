# WebSocket Synth Plugin

UniMRCP 语音合成插件，通过 WebSocket 协议对接外部 TTS 服务。

## 功能特性

- 通过 WebSocket 连接外部 TTS 服务
- 支持 MRCP SPEAK、STOP、PAUSE、RESUME 等请求
- 支持 8kHz 和 16kHz 采样率
- 支持语音参数配置（语速、音调、音量、发音人等）
- 线程安全的音频缓冲区设计
- 非阻塞的异步处理架构

## 配置参数

在 `unimrcpserver.xml` 中配置插件：

```xml
<engine id="WebSocket-Synth-1" name="websocketsynth" enable="true">
  <max-channel-count>100</max-channel-count>
  <param name="ws-host" value="localhost"/>
  <param name="ws-port" value="8080"/>
  <param name="ws-path" value="/tts"/>
</engine>
```

### 参数说明

| 参数 | 默认值 | 说明 |
|------|--------|------|
| ws-host | localhost | WebSocket 服务器地址 |
| ws-port | 8080 | WebSocket 服务器端口 |
| ws-path | /tts | WebSocket 服务路径 |

## WebSocket 协议接口

### 请求格式 (JSON)

客户端向 TTS 服务发送的请求格式：

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

#### 字段说明

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| action | string | 是 | 固定值 "tts" |
| text | string | 是 | 要合成的文本内容 |
| voice | string | 否 | 发音人名称，默认 "default" |
| speed | float | 否 | 语速，1.0 为正常速度 |
| pitch | float | 否 | 音调，1.0 为正常音调 |
| volume | float | 否 | 音量，1.0 为正常音量 |
| sample_rate | int | 否 | 采样率，8000 或 16000 |
| format | string | 否 | 音频格式，固定 "pcm" |
| session_id | string | 否 | 会话标识符 |

### 响应格式

TTS 服务应返回以下格式的响应：

#### 1. 音频数据帧 (Binary)

- WebSocket opcode: 0x02 (Binary)
- 内容: 原始 PCM 音频数据 (16-bit, Little-Endian)
- 可以发送多个二进制帧

#### 2. 完成消息 (Text)

合成完成时发送 JSON 消息：

```json
{
    "status": "complete"
}
```

或发送 WebSocket Close 帧表示结束。

### 响应流程

1. 客户端发送 TTS 请求 (JSON)
2. 服务端返回多个音频数据帧 (Binary)
3. 服务端发送完成消息 (Text) 或关闭连接

## MRCP 支持的请求

| 请求类型 | 支持状态 | 说明 |
|----------|----------|------|
| SPEAK | ✅ | 开始语音合成 |
| STOP | ✅ | 停止当前合成 |
| PAUSE | ✅ | 暂停播放 |
| RESUME | ✅ | 恢复播放 |
| SET-PARAMS | ✅ | 设置参数 |
| GET-PARAMS | ✅ | 获取参数 |
| BARGE-IN-OCCURRED | ✅ | 打断事件 |
| CONTROL | ❌ | 未实现 |
| DEFINE-LEXICON | ❌ | 未实现 |

## MRCP 头字段映射

| MRCP 头字段 | JSON 字段 | 说明 |
|-------------|-----------|------|
| Voice-Name | voice | 发音人名称 |
| Prosody-Rate | speed | 语速 |
| Prosody-Pitch | pitch | 音调 |
| Prosody-Volume | volume | 音量 |

## 构建

### CMake 构建

```bash
mkdir build && cd build
cmake .. -DENABLE_WEBSOCKETSYNTH_PLUGIN=ON
make
```

### Autotools 构建

```bash
./bootstrap
./configure --enable-websocketsynth-plugin
make
```

## 日志配置

在 `logger.xml` 中添加日志源配置：

```xml
<source name="WEBSOCKET-SYNTH-PLUGIN" priority="DEBUG" masking="NONE"/>
```

## 示例 TTS 服务 (Python)

```python
import asyncio
import websockets
import json

async def tts_handler(websocket, path):
    async for message in websocket:
        request = json.loads(message)
        
        if request.get('action') == 'tts':
            text = request.get('text', '')
            sample_rate = request.get('sample_rate', 8000)
            
            # 生成 PCM 音频数据 (示例: 静音)
            # 实际应用中这里调用真实的 TTS 引擎
            duration_ms = len(text) * 100  # 每字符 100ms
            samples = int(sample_rate * duration_ms / 1000)
            audio_data = bytes(samples * 2)  # 16-bit samples
            
            # 分块发送音频
            chunk_size = 1600  # 100ms at 8kHz
            for i in range(0, len(audio_data), chunk_size):
                chunk = audio_data[i:i+chunk_size]
                await websocket.send(chunk)
                await asyncio.sleep(0.1)
            
            # 发送完成消息
            await websocket.send(json.dumps({"status": "complete"}))

async def main():
    async with websockets.serve(tts_handler, "localhost", 8080, path="/tts"):
        await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main())
```

## 版本历史

- 1.0.0: 初始版本
  - 基本 TTS 功能实现
  - WebSocket 客户端
  - MRCP 请求处理
