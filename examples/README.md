# UniMRCP WebSocket 对接示例代码

本目录包含用于对接 UniMRCP WebSocket 插件的各语言示例代码。

## 目录结构

```
examples/
├── README.md           # 本文件
├── python/             # Python 示例
│   ├── websocket_server.py
│   ├── requirements.txt
│   └── README.md
├── nodejs/             # Node.js 示例
│   ├── websocket_server.js
│   ├── package.json
│   └── README.md
├── java/               # Java 示例
│   ├── src/WebSocketServer.java
│   ├── pom.xml
│   └── README.md
└── go/                 # Go 示例
    ├── main.go
    ├── go.mod
    └── README.md
```

## 快速开始

### Python

```bash
cd python
pip install -r requirements.txt
python websocket_server.py
```

### Node.js

```bash
cd nodejs
npm install
npm start
```

### Java

```bash
cd java
mvn clean package
java -jar target/websocket-server-1.0.0.jar
```

### Go

```bash
cd go
go mod tidy
go run main.go
```

## 接口协议

详见 [WEBSOCKET_API.md](../docs/WEBSOCKET_API.md)

## 功能说明

所有示例实现相同的功能:

1. **TTS 端点** (`/tts`)
   - 接收 JSON 格式的 TTS 请求
   - 流式返回 PCM 音频数据
   - 发送完成消息

2. **ASR 端点** (`/asr`)
   - 接收 PCM 音频数据
   - 返回 NLSML 格式的识别结果

## 演示模式

示例代码使用演示模式:

- TTS: 生成简单的正弦波音频
- ASR: 返回固定的识别文本

## 生产部署

在生产环境中，需要:

1. 替换 TTS 引擎为真实服务 (阿里云、百度、讯飞等)
2. 替换 ASR 引擎为真实服务
3. 添加错误处理和日志
4. 添加监控和告警
5. 配置负载均衡

## 常见云服务商 SDK

| 服务商 | TTS SDK | ASR SDK |
|--------|---------|---------|
| 阿里云 | alibabacloud-nls | alibabacloud-nls |
| 百度 | baidu-aip | baidu-aip |
| 讯飞 | xfyun-tts-sdk | xfyun-iat-sdk |
| 腾讯云 | tencentcloud-tts | tencentcloud-asr |
| 微软 Azure | azure-cognitiveservices-speech | azure-cognitiveservices-speech |
| Google Cloud | google-cloud-texttospeech | google-cloud-speech |
