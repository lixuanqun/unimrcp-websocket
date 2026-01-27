# Go WebSocket TTS/ASR 服务示例

## 安装依赖

```bash
go mod tidy
```

## 运行

```bash
go run main.go
```

## 编译

```bash
go build -o websocket-server
./websocket-server
```

## 说明

服务启动后监听:
- TTS: `ws://localhost:8080/tts`
- ASR: `ws://localhost:8080/asr`

## 集成真实 TTS/ASR 引擎

### 阿里云 TTS 示例

```go
import (
    nls "github.com/aliyun/alibabacloud-nls-go-sdk"
)

func (e *TTSEngine) Synthesize(req TTSRequest, sendFrame func([]byte), onComplete func()) {
    synthesizer, _ := nls.NewSpeechSynthesizer(config, &nls.SpeechSynthesizerListener{
        OnMessage: func(data []byte) {
            sendFrame(data)
        },
        OnComplete: func(response *nls.SpeechSynthesizerResponse) {
            onComplete()
        },
    })
    
    synthesizer.SetText(req.Text)
    synthesizer.SetVoice(req.Voice)
    synthesizer.Start()
}
```

### 讯飞 ASR 示例

```go
import (
    iat "github.com/xfyun/iat-golang-sdk"
)

func (e *ASREngine) Recognize(audioData []byte, sampleRate int) string {
    client := iat.NewClient(appID, apiKey, apiSecret)
    
    result, err := client.Recognize(audioData, iat.Options{
        Language: "zh_cn",
        Format:   "audio/L16;rate=8000",
    })
    
    if err != nil {
        return e.GenerateNLSML("", 0)
    }
    
    return e.GenerateNLSML(result.Text, result.Confidence)
}
```

## Docker 部署

```dockerfile
FROM golang:1.21-alpine AS builder
WORKDIR /app
COPY go.mod go.sum ./
RUN go mod download
COPY . .
RUN go build -o websocket-server

FROM alpine:latest
WORKDIR /app
COPY --from=builder /app/websocket-server .
EXPOSE 8080
CMD ["./websocket-server"]
```

构建运行:

```bash
docker build -t websocket-server .
docker run -p 8080:8080 websocket-server
```
