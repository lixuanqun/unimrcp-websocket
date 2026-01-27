# Java WebSocket TTS/ASR 服务示例

## 使用 Maven 构建

```bash
mvn clean package
```

## 运行

```bash
java -jar target/websocket-server-1.0.0.jar
```

## 说明

服务启动后监听:
- TTS: `ws://localhost:8080/tts`
- ASR: `ws://localhost:8080/asr`

## 集成真实 TTS/ASR 引擎

### 阿里云 TTS 示例

```java
import com.alibaba.nls.client.protocol.tts.SpeechSynthesizer;
import com.alibaba.nls.client.protocol.tts.SpeechSynthesizerListener;

public void synthesize(String text, ...) {
    SpeechSynthesizer synthesizer = new SpeechSynthesizer(client, new SpeechSynthesizerListener() {
        @Override
        public void onMessage(ByteBuffer message) {
            onFrame.onFrame(message);
        }
        
        @Override
        public void onComplete(SpeechSynthesizerResponse response) {
            onComplete.onComplete();
        }
    });
    
    synthesizer.setText(text);
    synthesizer.setVoice(voice);
    synthesizer.start();
}
```

### 百度 ASR 示例

```java
import com.baidu.aip.speech.AipSpeech;

public String recognize(byte[] audioData) {
    AipSpeech client = new AipSpeech(APP_ID, API_KEY, SECRET_KEY);
    JSONObject result = client.asr(audioData, "pcm", 8000, null);
    
    if (result.getInt("err_no") == 0) {
        String text = result.getJSONArray("result").getString(0);
        return generateNLSML(text, 0.95);
    }
    return null;
}
```

## Spring Boot 集成

如果使用 Spring Boot，可以使用 `spring-boot-starter-websocket`:

```java
@Configuration
@EnableWebSocket
public class WebSocketConfig implements WebSocketConfigurer {
    
    @Override
    public void registerWebSocketHandlers(WebSocketHandlerRegistry registry) {
        registry.addHandler(new TTSHandler(), "/tts");
        registry.addHandler(new ASRHandler(), "/asr");
    }
}
```
