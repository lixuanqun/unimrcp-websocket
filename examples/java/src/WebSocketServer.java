/**
 * UniMRCP WebSocket TTS/ASR 服务示例 (Java)
 * 
 * 依赖:
 *     Java-WebSocket: https://github.com/TooTallNate/Java-WebSocket
 *     Gson: https://github.com/google/gson
 * 
 * 编译运行:
 *     javac -cp "lib/*" src/WebSocketServer.java -d out
 *     java -cp "out:lib/*" WebSocketServer
 * 
 * 说明:
 *     这是一个演示用的 WebSocket 服务器，实现了 TTS 和 ASR 的基本接口。
 *     实际使用时需要替换为真实的 TTS/ASR 引擎。
 */

import org.java_websocket.WebSocket;
import org.java_websocket.handshake.ClientHandshake;
import org.java_websocket.server.WebSocketServer;
import com.google.gson.Gson;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.io.ByteArrayOutputStream;

public class WebSocketServer extends org.java_websocket.server.WebSocketServer {
    
    private static final String HOST = "0.0.0.0";
    private static final int PORT = 8080;
    
    private final Gson gson = new Gson();
    private final TTSEngine ttsEngine = new TTSEngine();
    private final ASREngine asrEngine = new ASREngine();
    
    // 存储每个连接的音频缓冲区
    private final Map<WebSocket, ByteArrayOutputStream> asrBuffers = new ConcurrentHashMap<>();
    
    public WebSocketServer(InetSocketAddress address) {
        super(address);
    }
    
    @Override
    public void onOpen(WebSocket conn, ClientHandshake handshake) {
        String path = handshake.getResourceDescriptor();
        System.out.println("新连接: path=" + path);
        
        if ("/asr".equals(path)) {
            asrBuffers.put(conn, new ByteArrayOutputStream());
        }
    }
    
    @Override
    public void onClose(WebSocket conn, int code, String reason, boolean remote) {
        System.out.println("连接关闭: " + reason);
        
        // 清理 ASR 缓冲区
        ByteArrayOutputStream buffer = asrBuffers.remove(conn);
        if (buffer != null && buffer.size() > 0) {
            // 处理剩余音频
            try {
                String result = asrEngine.recognize(buffer.toByteArray());
                System.out.println("ASR 结果 (连接已关闭): " + result);
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }
    
    @Override
    public void onMessage(WebSocket conn, String message) {
        String path = conn.getResourceDescriptor();
        
        if ("/tts".equals(path)) {
            handleTTSRequest(conn, message);
        } else if ("/asr".equals(path)) {
            // ASR 文本消息 (控制命令)
            try {
                JsonObject control = JsonParser.parseString(message).getAsJsonObject();
                if ("end".equals(control.get("action").getAsString())) {
                    ByteArrayOutputStream buffer = asrBuffers.get(conn);
                    if (buffer != null && buffer.size() > 0) {
                        String result = asrEngine.recognize(buffer.toByteArray());
                        conn.send(result);
                        buffer.reset();
                    }
                }
            } catch (Exception e) {
                // 忽略解析错误
            }
        }
    }
    
    @Override
    public void onMessage(WebSocket conn, ByteBuffer message) {
        String path = conn.getResourceDescriptor();
        
        if ("/asr".equals(path)) {
            // ASR 二进制消息 (音频数据)
            ByteArrayOutputStream buffer = asrBuffers.get(conn);
            if (buffer != null) {
                byte[] data = new byte[message.remaining()];
                message.get(data);
                try {
                    buffer.write(data);
                    System.out.println("ASR 收到音频: " + data.length + " bytes");
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }
        }
    }
    
    @Override
    public void onError(WebSocket conn, Exception ex) {
        System.err.println("WebSocket 错误: " + ex.getMessage());
        ex.printStackTrace();
    }
    
    @Override
    public void onStart() {
        System.out.println("启动 WebSocket 服务器: ws://" + HOST + ":" + PORT);
        System.out.println("TTS 端点: /tts");
        System.out.println("ASR 端点: /asr");
    }
    
    /**
     * 处理 TTS 请求
     */
    private void handleTTSRequest(WebSocket conn, String message) {
        try {
            JsonObject request = JsonParser.parseString(message).getAsJsonObject();
            System.out.println("TTS 请求: " + request);
            
            String action = request.has("action") ? request.get("action").getAsString() : "";
            if (!"tts".equals(action)) {
                sendError(conn, "INVALID_REQUEST", "Invalid action");
                return;
            }
            
            String text = request.has("text") ? request.get("text").getAsString() : "";
            if (text.isEmpty()) {
                sendError(conn, "TEXT_EMPTY", "Text is empty");
                return;
            }
            
            // 获取参数
            String voice = request.has("voice") ? request.get("voice").getAsString() : "default";
            double speed = request.has("speed") ? request.get("speed").getAsDouble() : 1.0;
            double pitch = request.has("pitch") ? request.get("pitch").getAsDouble() : 1.0;
            double volume = request.has("volume") ? request.get("volume").getAsDouble() : 1.0;
            int sampleRate = request.has("sample_rate") ? request.get("sample_rate").getAsInt() : 8000;
            
            // 合成并发送音频
            ttsEngine.synthesize(text, voice, speed, pitch, volume, sampleRate,
                (audioFrame) -> {
                    if (conn.isOpen()) {
                        conn.send(audioFrame);
                    }
                },
                () -> {
                    if (conn.isOpen()) {
                        conn.send("{\"status\":\"complete\"}");
                    }
                }
            );
            
        } catch (Exception e) {
            System.err.println("TTS 处理错误: " + e.getMessage());
            sendError(conn, "INTERNAL_ERROR", e.getMessage());
        }
    }
    
    private void sendError(WebSocket conn, String code, String message) {
        JsonObject error = new JsonObject();
        error.addProperty("status", "error");
        error.addProperty("code", code);
        error.addProperty("message", message);
        conn.send(gson.toJson(error));
    }
    
    public static void main(String[] args) {
        WebSocketServer server = new WebSocketServer(new InetSocketAddress(HOST, PORT));
        server.start();
        
        // 优雅关闭
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            System.out.println("\n服务器已停止");
            try {
                server.stop();
            } catch (Exception e) {
                e.printStackTrace();
            }
        }));
    }
}

/**
 * TTS 引擎类
 */
class TTSEngine {
    
    public interface AudioFrameCallback {
        void onFrame(ByteBuffer frame);
    }
    
    public interface CompleteCallback {
        void onComplete();
    }
    
    /**
     * 合成语音
     */
    public void synthesize(String text, String voice, double speed, double pitch, 
                          double volume, int sampleRate,
                          AudioFrameCallback onFrame, CompleteCallback onComplete) {
        
        System.out.println(String.format("TTS: text='%s', voice=%s, speed=%.1f, sampleRate=%d",
            text, voice, speed, sampleRate));
        
        // 在新线程中执行
        new Thread(() -> {
            try {
                // 演示: 生成简单的正弦波音频
                int durationMs = text.length() * 200; // 每字符约 200ms
                int samplesPerFrame = sampleRate / 50; // 20ms 一帧
                int totalSamples = sampleRate * durationMs / 1000;
                
                double frequency = 440.0;
                int samplesGenerated = 0;
                int frameCount = 0;
                
                while (samplesGenerated < totalSamples) {
                    int frameSamples = Math.min(samplesPerFrame, totalSamples - samplesGenerated);
                    ByteBuffer frameBuffer = ByteBuffer.allocate(frameSamples * 2);
                    frameBuffer.order(ByteOrder.LITTLE_ENDIAN);
                    
                    for (int i = 0; i < frameSamples; i++) {
                        double t = (double)(samplesGenerated + i) / sampleRate;
                        // 生成正弦波
                        int sample = (int)(32767 * volume * 0.3 * 
                            Math.sin(2 * Math.PI * frequency * t * pitch));
                        // 限幅
                        sample = Math.max(-32768, Math.min(32767, sample));
                        frameBuffer.putShort((short)sample);
                    }
                    
                    samplesGenerated += frameSamples;
                    frameBuffer.flip();
                    onFrame.onFrame(frameBuffer);
                    frameCount++;
                    
                    Thread.sleep(10);
                }
                
                System.out.println("TTS 完成: 发送 " + frameCount + " 帧");
                onComplete.onComplete();
                
            } catch (Exception e) {
                e.printStackTrace();
            }
        }).start();
    }
}

/**
 * ASR 引擎类
 */
class ASREngine {
    
    /**
     * 识别语音
     */
    public String recognize(byte[] audioData) {
        double duration = (double)audioData.length / (8000 * 2); // 假设 8kHz 16-bit
        System.out.println(String.format("ASR: received %d bytes, duration=%.2fs", 
            audioData.length, duration));
        
        // 演示: 返回模拟识别结果
        // 实际应用中替换为真实 ASR 引擎的输出
        String text = "这是一段测试语音";
        double confidence = 0.95;
        
        return generateNLSML(text, confidence);
    }
    
    /**
     * 生成 NLSML 格式的识别结果
     */
    private String generateNLSML(String text, double confidence) {
        return String.format(
            "<?xml version=\"1.0\"?>\n" +
            "<result>\n" +
            "  <interpretation grammar=\"session:request\" confidence=\"%.2f\">\n" +
            "    <instance>%s</instance>\n" +
            "    <input mode=\"speech\">%s</input>\n" +
            "  </interpretation>\n" +
            "</result>",
            confidence, text, text
        );
    }
}
