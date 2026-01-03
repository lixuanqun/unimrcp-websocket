# UniMRCP WebSocket Plugin

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)

## Overview

This project is a fork of [UniMRCP](https://www.unimrcp.org/), an open source implementation of the Media Resource Control Protocol (MRCP). This fork adds a WebSocket Recognizer plugin that allows UniMRCP servers to connect to external speech recognition services via WebSocket protocol.

## What's New

### WebSocket Recognizer Plugin

This fork introduces a new **WebSocket Recognizer Plugin** that enables UniMRCP servers to:

- Connect to external speech recognition services via WebSocket protocol (RFC 6455)
- Stream audio data in real-time to WebSocket servers
- Receive recognition results asynchronously
- Support configurable WebSocket server endpoints (host, port, path)
- Integrate seamlessly with existing MRCP Recognizer interface

## Features

- ✅ Full MRCP Recognizer resource interface implementation
- ✅ WebSocket protocol support (RFC 6455 compliant)
- ✅ Real-time audio streaming
- ✅ Voice Activity Detection (VAD)
- ✅ Asynchronous processing (non-blocking)
- ✅ Configurable WebSocket endpoints
- ✅ Support for 8kHz and 16kHz audio sampling rates
- ✅ LPCM audio format support

## Installation

### Prerequisites

- APR (Apache Portable Runtime) >= 1.2.x
- APR-util >= 1.2.x
- Sofia-SIP >= 1.12.6 (for server)
- Build tools: autoconf, automake, libtool, pkg-config

### Quick Start

```bash
# Clone the repository
git clone <repository-url>
cd unimrcp-websocket

# Bootstrap and configure
./bootstrap
./configure --enable-websocketrecog-plugin

# Build
make -j4

# Install
sudo make install
```

For detailed installation instructions, see [WSL Build & Deploy Guide](WSL_BUILD_DEPLOY_GUIDE.md).

## Configuration

Add the WebSocket plugin configuration to `unimrcpserver.xml`:

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

### Configuration Parameters

| Parameter | Description | Default | Example |
|-----------|-------------|---------|---------|
| `ws-host` | WebSocket server host address | `localhost` | `192.168.1.100` |
| `ws-port` | WebSocket server port | `8080` | `9000` |
| `ws-path` | WebSocket connection path | `/` | `/asr` |
| `audio-buffer-size` | Audio buffer size in bytes | Auto-calculated | `32000` |

## Usage

### WebSocket Protocol

The plugin uses standard WebSocket protocol (RFC 6455):

1. **Connection**: Established when a recognition request starts
2. **Audio Transmission**: Audio data sent as binary frames (PCM, 16-bit, 8kHz/16kHz)
3. **Result Reception**: Recognition results received as text frames (JSON format)

### Message Format

#### Sending Audio Data
- Frame type: Binary frame (opcode 0x2)
- Payload: PCM audio data (16-bit, 8kHz or 16kHz)

#### Receiving Recognition Results
- Frame type: Text frame (opcode 0x1)
- Payload: JSON format recognition result

Example JSON format:
```json
{
  "status": "success",
  "text": "Recognition result text",
  "confidence": 0.95
}
```

## Code Examples

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
        System.out.println("Result: " + message);
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
        # Send audio data
        audio_data = b'\x00\x01\x02...'  # PCM audio bytes
        await websocket.send(audio_data)
        
        # Receive result
        result = await websocket.recv()
        data = json.loads(result)
        print(f"Result: {data['text']}")

asyncio.run(asr_client())
```

### Node.js

```javascript
const WebSocket = require('ws');

const ws = new WebSocket('ws://localhost:8080/asr');

ws.on('open', () => {
  console.log('Connected to WebSocket server');
  
  // Send audio data
  const audioBuffer = Buffer.from([0x00, 0x01, 0x02, ...]);
  ws.send(audioBuffer);
});

ws.on('message', (data) => {
  const result = JSON.parse(data.toString());
  console.log('Recognition result:', result.text);
});

ws.on('error', (error) => {
  console.error('WebSocket error:', error);
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
        log.Fatal("Dial:", err)
    }
    defer conn.Close()
    
    // Send audio data
    audioData := []byte{0x00, 0x01, 0x02, ...}
    err = conn.WriteMessage(websocket.BinaryMessage, audioData)
    if err != nil {
        log.Println("Write:", err)
        return
    }
    
    // Receive result
    _, message, err := conn.ReadMessage()
    if err != nil {
        log.Println("Read:", err)
        return
    }
    
    var result RecognitionResult
    json.Unmarshal(message, &result)
    log.Printf("Result: %s", result.Text)
}
```

## Documentation

- [WSL Build & Deploy Guide](WSL_BUILD_DEPLOY_GUIDE.md) - Complete installation and deployment guide for WSL environment
- [Plugin Development Guide](docs/PLUGIN_DEVELOPMENT_GUIDE.md) - UniMRCP plugin development documentation
- [Plugin Specification](plugins/websocket-recog/PLUGIN_SPECIFICATION.md) - Detailed plugin specification and compliance report
- [WebSocket Plugin README](plugins/websocket-recog/README.md) - Plugin-specific documentation

## Building from Source

See [WSL_BUILD_DEPLOY_GUIDE.md](WSL_BUILD_DEPLOY_GUIDE.md) for detailed build instructions.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## License

This project is licensed under the Apache License 2.0 - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- [UniMRCP](https://www.unimrcp.org/) - Original UniMRCP project
- [Apache Portable Runtime (APR)](https://apr.apache.org/) - Cross-platform library
- [Sofia-SIP](https://github.com/freeswitch/sofia-sip) - SIP stack

## References

- [UniMRCP Official Website](https://www.unimrcp.org/)
- [MRCP Specification (RFC 6787)](https://tools.ietf.org/html/rfc6787)
- [WebSocket Protocol (RFC 6455)](https://tools.ietf.org/html/rfc6455)

## Support

For issues and questions:
- Check the [documentation](docs/)
- Review [troubleshooting guide](WSL_BUILD_DEPLOY_GUIDE.md#故障排除)
- Open an issue on GitHub

---

**Note**: This is a fork of UniMRCP with additional WebSocket plugin support. For the original UniMRCP project, visit [unimrcp.org](https://www.unimrcp.org/).

