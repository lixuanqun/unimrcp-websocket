# UniMRCP WebSocket 插件集成指南

本文档详细说明如何将外部 TTS/ASR 服务通过 WebSocket 协议与 UniMRCP 服务器集成。

---

## 目录

1. [架构概述](#1-架构概述)
2. [TTS 集成指南](#2-tts-集成指南)
3. [ASR 集成指南](#3-asr-集成指南)
4. [服务端配置](#4-服务端配置)
5. [客户端对接](#5-客户端对接)
6. [调试与监控](#6-调试与监控)
7. [常见问题](#7-常见问题)

---

## 1. 架构概述

### 1.1 系统架构图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              MRCP 客户端                                     │
│                    (FreeSWITCH / Oasis / 自定义客户端)                        │
└────────────────────────────────┬────────────────────────────────────────────┘
                                 │ MRCP/SIP
                                 ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           UniMRCP Server                                     │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │                        Plugin Factory                                │    │
│  │  ┌─────────────────────┐        ┌─────────────────────┐             │    │
│  │  │  websocket-synth    │        │  websocket-recog    │             │    │
│  │  │  (TTS Plugin)       │        │  (ASR Plugin)       │             │    │
│  │  │                     │        │                     │             │    │
│  │  │  ┌───────────────┐  │        │  ┌───────────────┐  │             │    │
│  │  │  │ ws_client_t   │  │        │  │ ws_client_t   │  │             │    │
│  │  │  └───────────────┘  │        │  └───────────────┘  │             │    │
│  │  └──────────┬──────────┘        └──────────┬──────────┘             │    │
│  └─────────────┼──────────────────────────────┼─────────────────────────┘    │
└────────────────┼──────────────────────────────┼─────────────────────────────┘
                 │ WebSocket                     │ WebSocket
                 ▼                               ▼
┌─────────────────────────────┐    ┌─────────────────────────────┐
│      TTS WebSocket 服务      │    │      ASR WebSocket 服务      │
│   ws://host:port/tts        │    │   ws://host:port/asr        │
│                             │    │                             │
│  接收: JSON 文本请求         │    │  接收: 二进制音频数据         │
│  返回: 二进制音频流          │    │  返回: NLSML/JSON 识别结果   │
└─────────────────────────────┘    └─────────────────────────────┘
```

### 1.2 数据流

**TTS 流程**:
```
MRCP SPEAK → Plugin → WebSocket JSON → TTS 服务 → 音频流 → Plugin → RTP → 客户端
```

**ASR 流程**:
```
RTP 音频 → Plugin (VAD) → WebSocket 二进制 → ASR 服务 → NLSML → Plugin → MRCP 事件
```

---

## 2. TTS 集成指南

### 2.1 协议规范

#### 2.1.1 WebSocket 连接

```
URL: ws://{host}:{port}{path}
示例: ws://localhost:8080/tts
```

#### 2.1.2 请求格式 (JSON)

UniMRCP 发送到 TTS 服务:

```json
{
    "action": "tts",
    "text": "要合成的文本内容",
    "voice": "发音人名称",
    "speed": 1.0,
    "volume": 1.0,
    "sample_rate": 8000,
    "format": "pcm",
    "session_id": "会话唯一标识"
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| action | string | 是 | 固定值 `"tts"` |
| text | string | 是 | 待合成文本，已 JSON 转义 |
| voice | string | 否 | 发音人，默认 `"default"` |
| speed | float | 否 | 语速，范围 0.5-2.0，默认 1.0 |
| volume | float | 否 | 音量，范围 0.0-1.0，默认 1.0 |
| sample_rate | int | 否 | 采样率，8000 或 16000 |
| format | string | 否 | 音频格式，固定 `"pcm"` |
| session_id | string | 否 | MRCP 会话标识 |

#### 2.1.3 响应格式

**音频数据帧** (Binary):
```
帧类型: WebSocket Binary Frame (opcode 0x02)
数据格式: 原始 PCM
- 编码: 16-bit signed integer, Little-Endian
- 通道: Mono
- 采样率: 与请求一致
建议: 每帧 20ms (8kHz: 320 bytes, 16kHz: 640 bytes)
```

**完成消息** (Text):
```json
{"status": "complete"}
```

或发送 WebSocket Close 帧。

**错误消息** (Text):
```json
{
    "status": "error",
    "code": "ERROR_CODE",
    "message": "错误描述"
}
```

### 2.2 TTS 服务实现示例

#### Python 实现

```python
import asyncio
import json
import struct
import math
from websockets.server import serve

async def handle_tts(websocket):
    async for message in websocket:
        request = json.loads(message)
        
        if request.get("action") != "tts":
            await websocket.send(json.dumps({
                "status": "error",
                "code": "INVALID_ACTION",
                "message": "Expected action: tts"
            }))
            continue
        
        text = request.get("text", "")
        voice = request.get("voice", "default")
        sample_rate = request.get("sample_rate", 8000)
        
        # 调用实际的 TTS 引擎
        # audio_generator = your_tts_engine.synthesize(text, voice, sample_rate)
        
        # 示例: 生成正弦波音频
        duration_ms = len(text) * 100
        samples = int(sample_rate * duration_ms / 1000)
        
        for i in range(0, samples, sample_rate // 50):  # 20ms chunks
            chunk_size = min(sample_rate // 50, samples - i)
            audio_chunk = bytearray(chunk_size * 2)
            
            for j in range(chunk_size):
                t = (i + j) / sample_rate
                sample = int(32767 * 0.3 * math.sin(2 * math.pi * 440 * t))
                struct.pack_into('<h', audio_chunk, j * 2, sample)
            
            await websocket.send(bytes(audio_chunk))
            await asyncio.sleep(0.01)
        
        # 发送完成消息
        await websocket.send(json.dumps({"status": "complete"}))

async def main():
    async with serve(handle_tts, "0.0.0.0", 8080):
        await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main())
```

#### Node.js 实现

```javascript
const WebSocket = require('ws');

const wss = new WebSocket.Server({ port: 8080 });

wss.on('connection', (ws) => {
    ws.on('message', async (message) => {
        const request = JSON.parse(message.toString());
        
        if (request.action !== 'tts') {
            ws.send(JSON.stringify({
                status: 'error',
                code: 'INVALID_ACTION',
                message: 'Expected action: tts'
            }));
            return;
        }
        
        const { text, voice, sample_rate = 8000 } = request;
        
        // 调用实际的 TTS 引擎
        // const audioStream = await yourTTSEngine.synthesize(text, voice);
        
        // 示例: 生成简单音频
        const durationMs = text.length * 100;
        const samples = Math.floor(sample_rate * durationMs / 1000);
        const chunkSize = Math.floor(sample_rate / 50); // 20ms
        
        for (let i = 0; i < samples; i += chunkSize) {
            const size = Math.min(chunkSize, samples - i);
            const buffer = Buffer.alloc(size * 2);
            
            for (let j = 0; j < size; j++) {
                const t = (i + j) / sample_rate;
                const sample = Math.floor(32767 * 0.3 * Math.sin(2 * Math.PI * 440 * t));
                buffer.writeInt16LE(sample, j * 2);
            }
            
            ws.send(buffer);
            await new Promise(r => setTimeout(r, 10));
        }
        
        ws.send(JSON.stringify({ status: 'complete' }));
    });
});

console.log('TTS WebSocket server running on ws://localhost:8080');
```

#### Go 实现

```go
package main

import (
    "encoding/binary"
    "encoding/json"
    "math"
    "net/http"
    "time"

    "github.com/gorilla/websocket"
)

var upgrader = websocket.Upgrader{}

type TTSRequest struct {
    Action     string  `json:"action"`
    Text       string  `json:"text"`
    Voice      string  `json:"voice"`
    SampleRate int     `json:"sample_rate"`
}

func handleTTS(w http.ResponseWriter, r *http.Request) {
    conn, _ := upgrader.Upgrade(w, r, nil)
    defer conn.Close()

    for {
        _, message, err := conn.ReadMessage()
        if err != nil {
            break
        }

        var req TTSRequest
        json.Unmarshal(message, &req)

        if req.Action != "tts" {
            conn.WriteJSON(map[string]string{
                "status": "error",
                "code":   "INVALID_ACTION",
            })
            continue
        }

        sampleRate := req.SampleRate
        if sampleRate == 0 {
            sampleRate = 8000
        }

        // 调用实际 TTS 引擎
        // audioData := yourTTSEngine.Synthesize(req.Text, req.Voice)

        // 示例: 生成音频
        durationMs := len([]rune(req.Text)) * 100
        samples := sampleRate * durationMs / 1000
        chunkSize := sampleRate / 50 // 20ms

        for i := 0; i < samples; i += chunkSize {
            size := chunkSize
            if samples-i < chunkSize {
                size = samples - i
            }

            buf := make([]byte, size*2)
            for j := 0; j < size; j++ {
                t := float64(i+j) / float64(sampleRate)
                sample := int16(32767 * 0.3 * math.Sin(2*math.Pi*440*t))
                binary.LittleEndian.PutUint16(buf[j*2:], uint16(sample))
            }

            conn.WriteMessage(websocket.BinaryMessage, buf)
            time.Sleep(10 * time.Millisecond)
        }

        conn.WriteJSON(map[string]string{"status": "complete"})
    }
}

func main() {
    http.HandleFunc("/tts", handleTTS)
    http.ListenAndServe(":8080", nil)
}
```

#### Java 实现

```java
import org.java_websocket.server.WebSocketServer;
import org.java_websocket.WebSocket;
import org.java_websocket.handshake.ClientHandshake;
import com.google.gson.*;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

public class TTSServer extends WebSocketServer {
    
    public TTSServer(int port) {
        super(new InetSocketAddress(port));
    }
    
    @Override
    public void onMessage(WebSocket conn, String message) {
        JsonObject request = JsonParser.parseString(message).getAsJsonObject();
        
        if (!"tts".equals(request.get("action").getAsString())) {
            conn.send("{\"status\":\"error\",\"code\":\"INVALID_ACTION\"}");
            return;
        }
        
        String text = request.get("text").getAsString();
        int sampleRate = request.has("sample_rate") ? 
            request.get("sample_rate").getAsInt() : 8000;
        
        // 调用实际 TTS 引擎
        // byte[] audio = yourTTSEngine.synthesize(text);
        
        // 示例: 生成音频
        int durationMs = text.length() * 100;
        int samples = sampleRate * durationMs / 1000;
        int chunkSize = sampleRate / 50; // 20ms
        
        for (int i = 0; i < samples; i += chunkSize) {
            int size = Math.min(chunkSize, samples - i);
            ByteBuffer buf = ByteBuffer.allocate(size * 2);
            buf.order(ByteOrder.LITTLE_ENDIAN);
            
            for (int j = 0; j < size; j++) {
                double t = (double)(i + j) / sampleRate;
                short sample = (short)(32767 * 0.3 * Math.sin(2 * Math.PI * 440 * t));
                buf.putShort(sample);
            }
            
            conn.send(buf.array());
            try { Thread.sleep(10); } catch (Exception e) {}
        }
        
        conn.send("{\"status\":\"complete\"}");
    }
    
    @Override
    public void onOpen(WebSocket conn, ClientHandshake handshake) {}
    @Override
    public void onClose(WebSocket conn, int code, String reason, boolean remote) {}
    @Override
    public void onError(WebSocket conn, Exception ex) { ex.printStackTrace(); }
    @Override
    public void onStart() { System.out.println("TTS Server started on port 8080"); }
    
    public static void main(String[] args) {
        new TTSServer(8080).start();
    }
}
```

### 2.3 MRCP 头部映射

| MRCP Header | JSON 字段 | 说明 |
|-------------|-----------|------|
| Voice-Name | voice | 发音人名称 |
| Prosody-Rate | speed | 语速 |
| Prosody-Volume | volume | 音量 |
| Content-Type | - | 文本类型 (SSML/plain) |
| Speech-Language | - | 语言代码 |

---

## 3. ASR 集成指南

### 3.1 协议规范

#### 3.1.1 WebSocket 连接

```
URL: ws://{host}:{port}{path}
示例: ws://localhost:8080/asr
```

#### 3.1.2 音频数据帧 (Binary)

UniMRCP 发送到 ASR 服务:

```
帧类型: WebSocket Binary Frame (opcode 0x02)
数据格式: 原始 PCM
- 编码: 16-bit signed integer, Little-Endian
- 通道: Mono
- 采样率: 8000 或 16000 Hz
```

**发送模式**:
- **批量模式** (默认): VAD 检测到语音结束后，一次性发送全部音频
- **流式模式** (streaming=true): 实时发送音频帧

#### 3.1.3 识别结果格式

**推荐: NLSML 格式**

```xml
<?xml version="1.0"?>
<result>
  <interpretation grammar="session:request" confidence="0.95">
    <instance>识别出的文本</instance>
    <input mode="speech">识别出的文本</input>
  </interpretation>
</result>
```

**简单 JSON 格式** (也支持):

```json
{
    "status": "success",
    "text": "识别出的文本",
    "confidence": 0.95
}
```

**错误响应**:

```json
{
    "status": "error",
    "code": "NO_SPEECH",
    "message": "未检测到语音"
}
```

### 3.2 ASR 服务实现示例

#### Python 实现

```python
import asyncio
import json
from websockets.server import serve

def generate_nlsml(text, confidence=0.95):
    return f'''<?xml version="1.0"?>
<result>
  <interpretation grammar="session:request" confidence="{confidence:.2f}">
    <instance>{text}</instance>
    <input mode="speech">{text}</input>
  </interpretation>
</result>'''

async def handle_asr(websocket):
    audio_buffer = bytearray()
    
    async for message in websocket:
        if isinstance(message, bytes):
            # 收集音频数据
            audio_buffer.extend(message)
            print(f"Received audio: {len(message)} bytes, total: {len(audio_buffer)}")
            
        elif isinstance(message, str):
            # 控制消息
            try:
                control = json.loads(message)
                if control.get("action") == "end":
                    # 执行识别
                    result = await recognize_audio(bytes(audio_buffer))
                    await websocket.send(result)
                    audio_buffer.clear()
            except json.JSONDecodeError:
                pass
    
    # 连接关闭时处理剩余音频
    if audio_buffer:
        result = await recognize_audio(bytes(audio_buffer))
        # 连接已关闭，无法发送

async def recognize_audio(audio_data):
    """
    调用实际的 ASR 引擎
    """
    # 示例: 调用云服务 ASR
    # from alibabacloud_nls import SpeechRecognizer
    # recognizer = SpeechRecognizer(...)
    # result = recognizer.recognize(audio_data)
    # return generate_nlsml(result.text, result.confidence)
    
    # 演示: 返回模拟结果
    duration = len(audio_data) / (8000 * 2)  # 假设 8kHz 16-bit
    print(f"Recognizing {duration:.2f} seconds of audio")
    
    return generate_nlsml("这是识别结果", 0.95)

async def main():
    async with serve(handle_asr, "0.0.0.0", 8080):
        print("ASR WebSocket server running on ws://localhost:8080/asr")
        await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main())
```

#### Node.js 实现

```javascript
const WebSocket = require('ws');

function generateNLSML(text, confidence = 0.95) {
    return `<?xml version="1.0"?>
<result>
  <interpretation grammar="session:request" confidence="${confidence.toFixed(2)}">
    <instance>${text}</instance>
    <input mode="speech">${text}</input>
  </interpretation>
</result>`;
}

const wss = new WebSocket.Server({ port: 8080 });

wss.on('connection', (ws) => {
    const audioBuffer = [];
    
    ws.on('message', async (message) => {
        if (Buffer.isBuffer(message)) {
            // 收集音频数据
            audioBuffer.push(message);
            console.log(`Received audio: ${message.length} bytes`);
            
        } else {
            // 控制消息
            try {
                const control = JSON.parse(message.toString());
                if (control.action === 'end') {
                    const audioData = Buffer.concat(audioBuffer);
                    audioBuffer.length = 0;
                    
                    // 调用实际 ASR 引擎
                    // const result = await yourASREngine.recognize(audioData);
                    
                    // 示例
                    const result = generateNLSML('这是识别结果', 0.95);
                    ws.send(result);
                }
            } catch (e) {}
        }
    });
    
    ws.on('close', () => {
        if (audioBuffer.length > 0) {
            console.log('Connection closed with pending audio');
        }
    });
});

console.log('ASR WebSocket server running on ws://localhost:8080');
```

#### Go 实现

```go
package main

import (
    "bytes"
    "fmt"
    "net/http"

    "github.com/gorilla/websocket"
)

var upgrader = websocket.Upgrader{}

func generateNLSML(text string, confidence float64) string {
    return fmt.Sprintf(`<?xml version="1.0"?>
<result>
  <interpretation grammar="session:request" confidence="%.2f">
    <instance>%s</instance>
    <input mode="speech">%s</input>
  </interpretation>
</result>`, confidence, text, text)
}

func handleASR(w http.ResponseWriter, r *http.Request) {
    conn, _ := upgrader.Upgrade(w, r, nil)
    defer conn.Close()

    var audioBuffer bytes.Buffer

    for {
        messageType, message, err := conn.ReadMessage()
        if err != nil {
            break
        }

        if messageType == websocket.BinaryMessage {
            // 收集音频
            audioBuffer.Write(message)
            fmt.Printf("Received audio: %d bytes\n", len(message))
            
        } else if messageType == websocket.TextMessage {
            // 控制消息
            // 调用实际 ASR 引擎
            // result := yourASREngine.Recognize(audioBuffer.Bytes())
            
            result := generateNLSML("这是识别结果", 0.95)
            conn.WriteMessage(websocket.TextMessage, []byte(result))
            audioBuffer.Reset()
        }
    }
}

func main() {
    http.HandleFunc("/asr", handleASR)
    fmt.Println("ASR Server running on :8080")
    http.ListenAndServe(":8080", nil)
}
```

### 3.3 流式识别支持

启用流式模式后，UniMRCP 会实时发送音频帧:

```xml
<!-- unimrcpserver.xml -->
<engine id="WebSocket-Recog-1" name="websocketrecog" enable="true">
  <param name="streaming" value="true"/>
</engine>
```

流式 ASR 服务需要:
1. 持续接收音频帧
2. 支持中间结果 (可选)
3. 在音频结束时返回最终结果

```python
# 流式识别示例
async def handle_streaming_asr(websocket):
    recognizer = StreamingRecognizer()
    
    async for message in websocket:
        if isinstance(message, bytes):
            # 实时处理音频
            partial_result = recognizer.process_chunk(message)
            
            # 可选: 发送中间结果
            if partial_result:
                await websocket.send(json.dumps({
                    "type": "partial",
                    "text": partial_result
                }))
    
    # 获取最终结果
    final_result = recognizer.finalize()
    await websocket.send(generate_nlsml(final_result.text, final_result.confidence))
```

---

## 4. 服务端配置

### 4.1 UniMRCP 服务器配置

编辑 `/opt/unimrcp/conf/unimrcpserver.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<unimrcpserver>
  <!-- ... 其他配置 ... -->
  
  <components>
    <plugin-factory>
      <!-- TTS 插件配置 -->
      <engine id="WebSocket-Synth-1" name="websocketsynth" enable="true">
        <max-channel-count>100</max-channel-count>
        <param name="ws-host" value="tts-service.example.com"/>
        <param name="ws-port" value="8080"/>
        <param name="ws-path" value="/tts"/>
        <param name="max-audio-size" value="10485760"/>  <!-- 10MB -->
      </engine>
      
      <!-- ASR 插件配置 -->
      <engine id="WebSocket-Recog-1" name="websocketrecog" enable="true">
        <max-channel-count>100</max-channel-count>
        <param name="ws-host" value="asr-service.example.com"/>
        <param name="ws-port" value="8080"/>
        <param name="ws-path" value="/asr"/>
        <param name="streaming" value="false"/>
      </engine>
    </plugin-factory>
  </components>
  
  <settings>
    <signaling-agent>SIP-Agent-1</signaling-agent>
    <connection-agent>MRCPv2-Agent-1</connection-agent>
    <media-engine>Media-Engine-1</media-engine>
    <rtp-factory>RTP-Factory-1</rtp-factory>
    
    <profiles>
      <!-- TTS Profile -->
      <profile name="websocket-synth">
        <resource id="speechsynth" enable="true">
          <engine>WebSocket-Synth-1</engine>
        </resource>
      </profile>
      
      <!-- ASR Profile -->
      <profile name="websocket-recog">
        <resource id="speechrecog" enable="true">
          <engine>WebSocket-Recog-1</engine>
        </resource>
      </profile>
      
      <!-- 组合 Profile -->
      <profile name="websocket">
        <resource id="speechsynth" enable="true">
          <engine>WebSocket-Synth-1</engine>
        </resource>
        <resource id="speechrecog" enable="true">
          <engine>WebSocket-Recog-1</engine>
        </resource>
      </profile>
    </profiles>
  </settings>
</unimrcpserver>
```

### 4.2 日志配置

编辑 `/opt/unimrcp/conf/logger.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<aptlogger>
  <sources>
    <!-- WebSocket 插件调试日志 -->
    <source name="WEBSOCKET-SYNTH-PLUGIN" priority="DEBUG" masking="NONE"/>
    <source name="WEBSOCKET-RECOG-PLUGIN" priority="DEBUG" masking="NONE"/>
  </sources>
</aptlogger>
```

### 4.3 参数说明

| 参数 | 适用插件 | 说明 |
|------|----------|------|
| ws-host | 两者 | WebSocket 服务器地址 |
| ws-port | 两者 | WebSocket 服务器端口 |
| ws-path | 两者 | WebSocket 路径 |
| max-audio-size | synth | 最大音频缓冲区 (字节) |
| streaming | recog | 是否启用流式识别 |

---

## 5. 客户端对接

### 5.1 FreeSWITCH 集成

**mrcp_profiles.xml**:
```xml
<include>
  <profile name="unimrcp-websocket" version="2">
    <param name="server-ip" value="unimrcp-server-ip"/>
    <param name="server-port" value="8060"/>
    <param name="resource-location" value=""/>
    <param name="speechsynth" value="speechsynthesizer"/>
    <param name="speechrecog" value="speechrecognizer"/>
    
    <synthparams>
      <param name="voice-name" value="default"/>
    </synthparams>
    
    <recogparams>
      <param name="no-input-timeout" value="5000"/>
      <param name="speech-complete-timeout" value="1000"/>
    </recogparams>
  </profile>
</include>
```

**拨号计划示例**:
```xml
<!-- TTS 示例 -->
<extension name="tts-test">
  <condition field="destination_number" expression="^5001$">
    <action application="answer"/>
    <action application="speak" data="unimrcp-websocket|default|你好，这是语音合成测试"/>
    <action application="hangup"/>
  </condition>
</extension>

<!-- ASR 示例 -->
<extension name="asr-test">
  <condition field="destination_number" expression="^5002$">
    <action application="answer"/>
    <action application="play_and_detect_speech" 
            data="say:请说出您的需求 detect:unimrcp-websocket {start-input-timers=false}builtin:speech/transcribe"/>
    <action application="log" data="INFO ASR Result: ${detect_speech_result}"/>
    <action application="hangup"/>
  </condition>
</extension>
```

### 5.2 直接 MRCP 客户端

使用 UniMRCP 客户端库:

```c
#include "unimrcp_client.h"

// 创建 TTS 会话
mrcp_session_t* session = mrcp_session_create(client, profile);
mrcp_channel_t* channel = mrcp_channel_create(session, MRCP_SYNTHESIZER_RESOURCE);

// 发送 SPEAK 请求
mrcp_message_t* request = mrcp_speak_request_create(channel);
mrcp_generic_header_t* generic = mrcp_generic_header_prepare(request);
apt_string_set(&generic->content_type, "text/plain");
apt_string_set(&request->body, "要合成的文本");

mrcp_channel_message_send(channel, request);
```

---

## 6. 调试与监控

### 6.1 日志查看

```bash
# 查看 UniMRCP 服务日志
tail -f /opt/unimrcp/log/unimrcpserver.log

# 过滤 WebSocket 插件日志
grep "WEBSOCKET" /opt/unimrcp/log/unimrcpserver.log
```

### 6.2 连接测试

```bash
# 测试 TTS WebSocket 连接
wscat -c ws://localhost:8080/tts

# 发送 TTS 请求
{"action":"tts","text":"测试","sample_rate":8000}
```

### 6.3 常见日志消息

```
# 成功连接
[INFO] WebSocket connected successfully

# 发送请求
[INFO] TTS request sent, starting audio receive

# 接收完成
[INFO] TTS synthesis complete
[INFO] SPEAK-COMPLETE: cause=0

# 错误
[ERROR] Failed to connect to TTS server
[ERROR] WebSocket handshake failed
```

---

## 7. 常见问题

### 7.1 连接失败

**问题**: `Failed to connect to WebSocket server`

**解决**:
1. 检查 WebSocket 服务是否运行
2. 检查防火墙设置
3. 验证 ws-host/ws-port/ws-path 配置

### 7.2 音频格式不匹配

**问题**: 播放的音频失真或无声

**解决**:
1. 确保返回的 PCM 格式正确 (16-bit LE, Mono)
2. 确保采样率与请求一致
3. 检查字节序 (Little-Endian)

### 7.3 识别超时

**问题**: ASR 无结果返回

**解决**:
1. 检查 VAD 参数设置
2. 确保音频数据有效
3. 增加超时时间

### 7.4 中文乱码

**问题**: 文本显示乱码

**解决**:
1. 确保 JSON 使用 UTF-8 编码
2. 检查 NLSML 响应编码
3. 验证文本转义正确

---

## 附录 A: 错误码定义

| 错误码 | 说明 | 处理建议 |
|--------|------|----------|
| INVALID_REQUEST | 请求格式错误 | 检查 JSON 格式 |
| TEXT_EMPTY | TTS 文本为空 | 提供有效文本 |
| VOICE_NOT_FOUND | 发音人不存在 | 使用有效发音人 |
| AUDIO_ERROR | 音频处理失败 | 检查音频格式 |
| NO_SPEECH | 未检测到语音 | 检查输入音频 |
| TIMEOUT | 处理超时 | 增加超时设置 |
| INTERNAL_ERROR | 内部错误 | 查看服务日志 |

---

## 附录 B: 音频格式参考

| 采样率 | 每帧采样 | 每帧字节 | 帧时长 |
|--------|----------|----------|--------|
| 8000 Hz | 160 | 320 | 20ms |
| 16000 Hz | 320 | 640 | 20ms |

**PCM 格式**:
- 编码: Linear PCM
- 位深: 16 bit
- 通道: Mono
- 字节序: Little-Endian

---

**文档版本**: 1.0  
**最后更新**: 2026-01-27
