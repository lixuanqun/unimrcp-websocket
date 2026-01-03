# UniMRCP WebSocket 插件

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)

## 项目概述

本项目是 [UniMRCP](https://www.unimrcp.org/) 的一个分支（fork），UniMRCP 是一个开源的媒体资源控制协议（MRCP）实现。本分支新增了 WebSocket 语音识别插件，允许 UniMRCP 服务器通过 WebSocket 协议连接到外部语音识别服务。

## 新增功能

### WebSocket 语音识别插件

本分支引入了一个新的 **WebSocket 语音识别插件**，使 UniMRCP 服务器能够：

- 通过 WebSocket 协议（RFC 6455）连接到外部语音识别服务
- 实时流式传输音频数据到 WebSocket 服务器
- 异步接收识别结果
- 支持可配置的 WebSocket 服务器端点（主机、端口、路径）
- 与现有的 MRCP Recognizer 接口无缝集成

## 功能特性

- ✅ 完整的 MRCP Recognizer 资源接口实现
- ✅ WebSocket 协议支持（符合 RFC 6455 标准）
- ✅ 实时音频流传输
- ✅ 语音活动检测（VAD）
- ✅ 异步处理（非阻塞）
- ✅ 可配置的 WebSocket 端点
- ✅ 支持 8kHz 和 16kHz 音频采样率
- ✅ LPCM 音频格式支持

## 安装

### 前置要求

- APR (Apache Portable Runtime) >= 1.2.x
- APR-util >= 1.2.x
- Sofia-SIP >= 1.12.6（服务器需要）
- 构建工具：autoconf, automake, libtool, pkg-config

### 快速开始

```bash
# 克隆仓库
git clone <repository-url>
cd unimrcp-websocket

# 生成配置脚本并配置
./bootstrap
./configure --enable-websocketrecog-plugin

# 编译
make -j4

# 安装
sudo make install
```

详细的安装说明，请参见 [WSL 编译部署指南](WSL_BUILD_DEPLOY_GUIDE.md)。

## 配置

在 `unimrcpserver.xml` 配置文件中添加 WebSocket 插件配置：

```xml
<plugin-factory>
  <engine id="WebSocket-Recog-1" name="websocketrecog" enable="true">
    <max-channel-count>100</max-channel-count>
    <param name="ws-host" value="localhost"/>
    <param name="ws-port" value="8080"/>
    <param name="ws-path" value="/asr"/>
    <param name="audio-buffer-size" value="32000"/>
  </engine>
</plugin-factory>
```

### 配置参数

| 参数 | 说明 | 默认值 | 示例 |
|------|------|--------|------|
| `ws-host` | WebSocket 服务器主机地址 | `localhost` | `192.168.1.100` |
| `ws-port` | WebSocket 服务器端口 | `8080` | `9000` |
| `ws-path` | WebSocket 连接路径 | `/` | `/asr` |
| `audio-buffer-size` | 音频缓冲区大小（字节） | 自动计算 | `32000` |

## 使用方法

### WebSocket 协议

插件使用标准的 WebSocket 协议（RFC 6455）：

1. **连接建立**：识别请求开始时建立连接
2. **音频传输**：音频数据以二进制帧格式发送（PCM，16-bit，8kHz/16kHz）
3. **结果接收**：识别结果以文本帧格式接收（JSON 格式）

### 消息格式

#### 发送音频数据
- 帧类型：二进制帧（opcode 0x2）
- 负载：PCM 音频数据（16-bit，8kHz 或 16kHz）

#### 接收识别结果
- 帧类型：文本帧（opcode 0x1）
- 负载：JSON 格式的识别结果

JSON 格式示例：
```json
{
  "status": "success",
  "text": "识别结果文本",
  "confidence": 0.95
}
```

## 代码示例

### Java

```java
import java.net.URI;
import javax.websocket.*;

@ClientEndpoint
public class WebSocketASRClient {
    private Session session;
    
    @OnOpen
    public void onOpen(Session session) {
        this.session = session;
    }
    
    @OnMessage
    public void onMessage(String message) {
        System.out.println("识别结果: " + message);
    }
    
    public void sendAudio(byte[] audioData) {
        session.getBasicRemote().sendBinary(
            ByteBuffer.wrap(audioData)
        );
    }
    
    public static void main(String[] args) throws Exception {
        WebSocketContainer container = 
            ContainerProvider.getWebSocketContainer();
        URI uri = new URI("ws://localhost:8080/asr");
        container.connectToServer(
            WebSocketASRClient.class, uri
        );
    }
}
```

### Python

```python
import asyncio
import websockets
import json

async def asr_client():
    uri = "ws://localhost:8080/asr"
    async with websockets.connect(uri) as websocket:
        # 发送音频数据
        audio_data = b'\x00\x01\x02...'  # PCM 音频字节
        await websocket.send(audio_data)
        
        # 接收识别结果
        result = await websocket.recv()
        data = json.loads(result)
        print(f"识别结果: {data['text']}")

asyncio.run(asr_client())
```

### Node.js

```javascript
const WebSocket = require('ws');

const ws = new WebSocket('ws://localhost:8080/asr');

ws.on('open', () => {
  console.log('已连接到 WebSocket 服务器');
  
  // 发送音频数据
  const audioBuffer = Buffer.from([0x00, 0x01, 0x02, ...]);
  ws.send(audioBuffer);
});

ws.on('message', (data) => {
  const result = JSON.parse(data.toString());
  console.log('识别结果:', result.text);
});

ws.on('error', (error) => {
  console.error('WebSocket 错误:', error);
});
```

### Go

```go
package main

import (
    "encoding/json"
    "github.com/gorilla/websocket"
    "log"
)

type RecognitionResult struct {
    Status     string  `json:"status"`
    Text       string  `json:"text"`
    Confidence float64 `json:"confidence"`
}

func main() {
    conn, _, err := websocket.DefaultDialer.Dial(
        "ws://localhost:8080/asr", nil)
    if err != nil {
        log.Fatal("连接失败:", err)
    }
    defer conn.Close()
    
    // 发送音频数据
    audioData := []byte{0x00, 0x01, 0x02, ...}
    err = conn.WriteMessage(websocket.BinaryMessage, audioData)
    if err != nil {
        log.Println("发送失败:", err)
        return
    }
    
    // 接收识别结果
    _, message, err := conn.ReadMessage()
    if err != nil {
        log.Println("接收失败:", err)
        return
    }
    
    var result RecognitionResult
    json.Unmarshal(message, &result)
    log.Printf("识别结果: %s", result.Text)
}
```

## 文档

- [WSL 编译部署指南](WSL_BUILD_DEPLOY_GUIDE.md) - WSL 环境下的完整安装和部署指南
- [插件开发指南](docs/PLUGIN_DEVELOPMENT_GUIDE.md) - UniMRCP 插件开发文档
- [插件规范说明](plugins/websocket-recog/PLUGIN_SPECIFICATION.md) - 详细的插件规范和符合性报告
- [WebSocket 插件 README](plugins/websocket-recog/README.md) - 插件特定文档

## 从源码编译

详细的编译说明请参见 [WSL_BUILD_DEPLOY_GUIDE.md](WSL_BUILD_DEPLOY_GUIDE.md)。

## 贡献

欢迎贡献代码！请随时提交 Pull Request。

## 许可证

本项目采用 Apache License 2.0 许可证 - 详细信息请参见 [LICENSE](LICENSE) 文件。

## 致谢

- [UniMRCP](https://www.unimrcp.org/) - 原始 UniMRCP 项目
- [Apache Portable Runtime (APR)](https://apr.apache.org/) - 跨平台库
- [Sofia-SIP](https://github.com/freeswitch/sofia-sip) - SIP 协议栈

## 参考资源

- [UniMRCP 官方网站](https://www.unimrcp.org/)
- [MRCP 规范 (RFC 6787)](https://tools.ietf.org/html/rfc6787)
- [WebSocket 协议 (RFC 6455)](https://tools.ietf.org/html/rfc6455)

## 支持

如有问题或疑问：
- 查看[文档](docs/)
- 参考[故障排除指南](WSL_BUILD_DEPLOY_GUIDE.md#故障排除)
- 在 GitHub 上提交 Issue

---

**注意**：这是 UniMRCP 的一个分支，新增了 WebSocket 插件支持。原始 UniMRCP 项目请访问 [unimrcp.org](https://www.unimrcp.org/)。

