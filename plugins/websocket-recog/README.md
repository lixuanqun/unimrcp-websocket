# WebSocket Recognizer Plugin

## 概述

WebSocket Recognizer Plugin 是一个 UniMRCP 插件，它通过 WebSocket 协议连接到外部语音识别服务。该插件实现了 MRCP Recognizer 资源接口，允许 UniMRCP 服务器通过 WebSocket 将音频数据发送到外部识别服务并接收识别结果。

## 功能特性

- 支持通过 WebSocket 连接到外部语音识别服务
- 实现完整的 MRCP Recognizer 接口
- 支持音频流传输
- 可配置的 WebSocket 服务器地址、端口和路径
- 支持语音活动检测（VAD）
- 异步处理，不阻塞主线程

## 配置

在 `unimrcpserver.xml` 配置文件中添加以下配置：

```xml
<plugin-factory>
  <engine id="WebSocket-Recog-1" name="websocketrecog" enable="true">
    <max-channel-count>100</max-channel-count>
    <param name="ws-host" value="localhost"/>
    <param name="ws-port" value="8080"/>
    <param name="ws-path" value="/asr"/>
  </engine>
</plugin-factory>
```

### 配置参数

- `ws-host`: WebSocket 服务器主机地址（默认: localhost）
- `ws-port`: WebSocket 服务器端口（默认: 8080）
- `ws-path`: WebSocket 连接路径（默认: /）

## 编译

### 使用 Autotools

```bash
./bootstrap
./configure --enable-websocketrecog-plugin
make
make install
```

### 使用 CMake

```bash
mkdir build
cd build
cmake .. -DENABLE_WEBSOCKETRECOG_PLUGIN=ON
make
make install
```

## WebSocket 协议

插件使用标准的 WebSocket 协议（RFC 6455）进行通信：

1. **连接建立**: 插件在识别请求开始时建立 WebSocket 连接
2. **音频传输**: 音频数据以二进制帧格式发送
3. **结果接收**: 识别结果以文本帧格式接收（JSON 格式）

### WebSocket 消息格式

#### 发送音频数据
- 帧类型: 二进制帧 (opcode 0x2)
- 负载: PCM 音频数据（16-bit, 8kHz 或 16kHz）

#### 接收识别结果
- 帧类型: 文本帧 (opcode 0x1)
- 负载: JSON 格式的识别结果

示例 JSON 格式：
```json
{
  "status": "success",
  "text": "识别结果文本",
  "confidence": 0.95
}
```

## 使用示例

### Java 示例

```java
import java.net.URI;
import java.nio.ByteBuffer;
import javax.websocket.*;

@ClientEndpoint
public class WebSocketASRClient {
    private Session session;
    
    @OnOpen
    public void onOpen(Session session) {
        this.session = session;
        System.out.println("WebSocket connected");
    }
    
    @OnMessage
    public void onMessage(String message) {
        System.out.println("Received: " + message);
        // 处理识别结果
    }
    
    public void sendAudio(byte[] audioData) {
        try {
            session.getBasicRemote().sendBinary(ByteBuffer.wrap(audioData));
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
    
    public static void main(String[] args) {
        WebSocketContainer container = ContainerProvider.getWebSocketContainer();
        try {
            URI uri = new URI("ws://localhost:8080/asr");
            container.connectToServer(WebSocketASRClient.class, uri);
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}
```

### Python 示例

```python
import asyncio
import websockets
import json

async def asr_client():
    uri = "ws://localhost:8080/asr"
    async with websockets.connect(uri) as websocket:
        print("WebSocket connected")
        
        # 发送音频数据
        with open("audio.pcm", "rb") as f:
            audio_data = f.read()
            await websocket.send(audio_data)
        
        # 接收识别结果
        result = await websocket.recv()
        data = json.loads(result)
        print(f"识别结果: {data['text']}")
        print(f"置信度: {data['confidence']}")

asyncio.run(asr_client())
```

### Node.js 示例

```javascript
const WebSocket = require('ws');
const fs = require('fs');

const ws = new WebSocket('ws://localhost:8080/asr');

ws.on('open', function open() {
    console.log('WebSocket connected');
    
    // 读取并发送音频文件
    const audioData = fs.readFileSync('audio.pcm');
    ws.send(audioData, { binary: true });
});

ws.on('message', function message(data) {
    const result = JSON.parse(data.toString());
    console.log('识别结果:', result.text);
    console.log('置信度:', result.confidence);
});

ws.on('error', function error(err) {
    console.error('WebSocket error:', err);
});
```

### Go 示例

```go
package main

import (
    "encoding/json"
    "fmt"
    "io/ioutil"
    "log"
    "net/url"
    
    "github.com/gorilla/websocket"
)

type ASRResult struct {
    Status     string  `json:"status"`
    Text       string  `json:"text"`
    Confidence float64 `json:"confidence"`
}

func main() {
    u := url.URL{Scheme: "ws", Host: "localhost:8080", Path: "/asr"}
    
    c, _, err := websocket.DefaultDialer.Dial(u.String(), nil)
    if err != nil {
        log.Fatal("dial:", err)
    }
    defer c.Close()
    
    fmt.Println("WebSocket connected")
    
    // 读取音频文件
    audioData, err := ioutil.ReadFile("audio.pcm")
    if err != nil {
        log.Fatal("read file:", err)
    }
    
    // 发送音频数据
    err = c.WriteMessage(websocket.BinaryMessage, audioData)
    if err != nil {
        log.Fatal("write:", err)
    }
    
    // 接收识别结果
    _, message, err := c.ReadMessage()
    if err != nil {
        log.Fatal("read:", err)
    }
    
    var result ASRResult
    json.Unmarshal(message, &result)
    fmt.Printf("识别结果: %s\n", result.Text)
    fmt.Printf("置信度: %.2f\n", result.Confidence)
}
```

## 测试

### 1. 启动 WebSocket 服务器

创建一个简单的 WebSocket 服务器来接收音频并返回识别结果：

```python
# websocket_server.py
import asyncio
import websockets
import json

async def handle_client(websocket, path):
    print("Client connected")
    try:
        # 接收音频数据
        audio_data = await websocket.recv()
        print(f"Received {len(audio_data)} bytes of audio")
        
        # 模拟识别结果
        result = {
            "status": "success",
            "text": "这是一个测试识别结果",
            "confidence": 0.95
        }
        
        # 发送识别结果
        await websocket.send(json.dumps(result))
    except websockets.exceptions.ConnectionClosed:
        print("Client disconnected")

start_server = websockets.serve(handle_client, "localhost", 8080)
asyncio.get_event_loop().run_until_complete(start_server)
asyncio.get_event_loop().run_forever()
```

### 2. 配置 UniMRCP 服务器

确保 `unimrcpserver.xml` 中已正确配置插件。

### 3. 启动 UniMRCP 服务器

```bash
unimrcpserver
```

### 4. 使用 MRCP 客户端测试

使用 UniMRCP 客户端工具或任何兼容的 MRCP 客户端连接到服务器并发送识别请求。

## 故障排除

### 插件未加载

- 检查插件文件是否存在于 `plugin` 目录
- 检查配置文件中的插件名称是否正确
- 查看服务器日志文件

### WebSocket 连接失败

- 检查 WebSocket 服务器是否运行
- 验证配置的主机、端口和路径是否正确
- 检查防火墙设置

### 识别结果未返回

- 检查 WebSocket 服务器是否正确处理音频数据
- 验证返回的 JSON 格式是否正确
- 查看插件日志输出

## 开发说明

### 代码结构

- `websocket_recog_engine.c`: 插件主实现文件
  - 实现 MRCP Engine 接口
  - 实现 WebSocket 客户端功能
  - 处理音频流和识别结果

### 扩展功能

可以扩展以下功能：

1. **完整的 WebSocket 帧解析**: 当前实现是简化版本，可以添加完整的 WebSocket 协议支持
2. **结果解析**: 解析不同格式的识别结果（JSON、XML 等）
3. **重连机制**: 添加自动重连功能
4. **SSL/TLS 支持**: 支持 WSS 安全连接
5. **多路复用**: 支持单个 WebSocket 连接处理多个识别会话

## 许可证

Apache License 2.0

## 作者

UniMRCP WebSocket Plugin Contributors

