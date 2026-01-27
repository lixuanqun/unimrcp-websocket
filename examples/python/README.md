# Python WebSocket TTS/ASR 服务示例

## 安装依赖

```bash
pip install -r requirements.txt
```

## 运行

```bash
python websocket_server.py
```

## 说明

服务启动后监听:
- TTS: `ws://localhost:8080/tts`
- ASR: `ws://localhost:8080/asr`

## 集成真实 TTS/ASR 引擎

修改 `TTSEngine.synthesize()` 和 `ASREngine.recognize()` 方法，替换为真实引擎调用。

### 阿里云 TTS 示例

```python
from alibabacloud_nls import NlsSpeechSynthesizer

async def synthesize(self, text, voice, ...):
    synthesizer = NlsSpeechSynthesizer(...)
    synthesizer.set_text(text)
    synthesizer.set_voice(voice)
    
    for audio_chunk in synthesizer.stream():
        yield audio_chunk
```

### 百度 ASR 示例

```python
from aip import AipSpeech

async def recognize(self, audio_data, sample_rate):
    client = AipSpeech(APP_ID, API_KEY, SECRET_KEY)
    result = client.asr(audio_data, 'pcm', sample_rate, {'dev_pid': 1537})
    return {
        "status": "success",
        "text": result['result'][0],
        "confidence": 0.95
    }
```
