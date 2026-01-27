# Node.js WebSocket TTS/ASR 服务示例

## 安装依赖

```bash
npm install
```

## 运行

```bash
npm start
# 或
node websocket_server.js
```

## 说明

服务启动后监听:
- TTS: `ws://localhost:8080/tts`
- ASR: `ws://localhost:8080/asr`

## 集成真实 TTS/ASR 引擎

### 阿里云 TTS 示例

```javascript
const RPCClient = require('@alicloud/pop-core').RPCClient;

async synthesize(options, onAudioFrame, onComplete) {
    const client = new RPCClient({...});
    const result = await client.request('SynthesizeSpeech', {
        Text: options.text,
        Voice: options.voice,
        ...
    });
    
    // 处理音频流
    for (const chunk of result.audioStream) {
        onAudioFrame(chunk);
    }
    onComplete();
}
```

### Google Cloud ASR 示例

```javascript
const speech = require('@google-cloud/speech');

async recognize(audioData, sampleRate) {
    const client = new speech.SpeechClient();
    const [response] = await client.recognize({
        audio: { content: audioData.toString('base64') },
        config: {
            encoding: 'LINEAR16',
            sampleRateHertz: sampleRate,
            languageCode: 'zh-CN',
        },
    });
    
    return {
        status: 'success',
        text: response.results[0].alternatives[0].transcript,
        confidence: response.results[0].alternatives[0].confidence
    };
}
```
