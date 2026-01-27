/**
 * UniMRCP WebSocket TTS/ASR 服务示例 (Node.js)
 * 
 * 依赖安装:
 *     npm install ws
 * 
 * 运行:
 *     node websocket_server.js
 * 
 * 说明:
 *     这是一个演示用的 WebSocket 服务器，实现了 TTS 和 ASR 的基本接口。
 *     实际使用时需要替换为真实的 TTS/ASR 引擎。
 */

const WebSocket = require('ws');
const http = require('http');
const url = require('url');

// 服务器配置
const HOST = '0.0.0.0';
const PORT = 8080;

/**
 * TTS 引擎类
 */
class TTSEngine {
    constructor() {
        this.voices = ['default', 'male', 'female', 'child'];
    }

    /**
     * 合成语音
     * @param {Object} options 合成选项
     * @param {Function} onAudioFrame 音频帧回调
     * @param {Function} onComplete 完成回调
     */
    async synthesize(options, onAudioFrame, onComplete) {
        const {
            text = '',
            voice = 'default',
            speed = 1.0,
            pitch = 1.0,
            volume = 1.0,
            sampleRate = 8000
        } = options;

        console.log(`TTS: text='${text}', voice=${voice}, speed=${speed}, sampleRate=${sampleRate}`);

        // 演示: 生成简单的正弦波音频
        // 实际应用中替换为真实 TTS 引擎的输出
        const durationMs = text.length * 200; // 每字符约 200ms
        const samplesPerFrame = Math.floor(sampleRate / 50); // 20ms 一帧
        const totalSamples = Math.floor(sampleRate * durationMs / 1000);
        
        const frequency = 440.0; // 基础频率
        let samplesGenerated = 0;

        const generateFrame = () => {
            if (samplesGenerated >= totalSamples) {
                onComplete();
                return;
            }

            const frameSamples = Math.min(samplesPerFrame, totalSamples - samplesGenerated);
            const frameBuffer = Buffer.alloc(frameSamples * 2); // 16-bit = 2 bytes

            for (let i = 0; i < frameSamples; i++) {
                const t = (samplesGenerated + i) / sampleRate;
                // 生成正弦波
                let sample = Math.floor(32767 * volume * 0.3 * Math.sin(2 * Math.PI * frequency * t * pitch));
                // 限幅
                sample = Math.max(-32768, Math.min(32767, sample));
                // Little-endian 16-bit
                frameBuffer.writeInt16LE(sample, i * 2);
            }

            samplesGenerated += frameSamples;
            onAudioFrame(frameBuffer);

            // 模拟处理延迟
            setTimeout(generateFrame, 10);
        };

        generateFrame();
    }
}

/**
 * ASR 引擎类
 */
class ASREngine {
    constructor() {}

    /**
     * 识别语音
     * @param {Buffer} audioData 音频数据
     * @param {number} sampleRate 采样率
     * @returns {Object} 识别结果
     */
    async recognize(audioData, sampleRate = 8000) {
        const audioDuration = audioData.length / (sampleRate * 2); // 16-bit
        console.log(`ASR: received ${audioData.length} bytes, duration=${audioDuration.toFixed(2)}s`);

        // 演示: 返回模拟识别结果
        // 实际应用中替换为真实 ASR 引擎的输出
        return {
            status: 'success',
            text: '这是一段测试语音',
            confidence: 0.95
        };
    }

    /**
     * 生成 NLSML 格式的识别结果
     */
    generateNLSML(text, confidence = 0.95) {
        return `<?xml version="1.0"?>
<result>
  <interpretation grammar="session:request" confidence="${confidence.toFixed(2)}">
    <instance>${text}</instance>
    <input mode="speech">${text}</input>
  </interpretation>
</result>`;
    }
}

// 全局引擎实例
const ttsEngine = new TTSEngine();
const asrEngine = new ASREngine();

/**
 * 处理 TTS 请求
 */
function handleTTS(ws) {
    console.log('TTS 客户端连接');

    ws.on('message', async (message) => {
        try {
            // 解析 JSON 请求
            const request = JSON.parse(message.toString());
            console.log('TTS 请求:', request);

            if (request.action !== 'tts') {
                ws.send(JSON.stringify({
                    status: 'error',
                    code: 'INVALID_REQUEST',
                    message: 'Invalid action'
                }));
                return;
            }

            const text = request.text || '';
            if (!text) {
                ws.send(JSON.stringify({
                    status: 'error',
                    code: 'TEXT_EMPTY',
                    message: 'Text is empty'
                }));
                return;
            }

            let frameCount = 0;

            // 流式合成并发送音频
            await ttsEngine.synthesize(
                {
                    text,
                    voice: request.voice || 'default',
                    speed: parseFloat(request.speed) || 1.0,
                    pitch: parseFloat(request.pitch) || 1.0,
                    volume: parseFloat(request.volume) || 1.0,
                    sampleRate: parseInt(request.sample_rate) || 8000
                },
                (audioFrame) => {
                    if (ws.readyState === WebSocket.OPEN) {
                        ws.send(audioFrame);
                        frameCount++;
                    }
                },
                () => {
                    console.log(`TTS 完成: 发送 ${frameCount} 帧`);
                    if (ws.readyState === WebSocket.OPEN) {
                        ws.send(JSON.stringify({ status: 'complete' }));
                    }
                }
            );

        } catch (e) {
            console.error('TTS 处理错误:', e);
            ws.send(JSON.stringify({
                status: 'error',
                code: 'INTERNAL_ERROR',
                message: e.message
            }));
        }
    });

    ws.on('close', () => {
        console.log('TTS 客户端断开');
    });

    ws.on('error', (error) => {
        console.error('TTS WebSocket 错误:', error);
    });
}

/**
 * 处理 ASR 请求
 */
function handleASR(ws) {
    console.log('ASR 客户端连接');
    
    const audioBuffer = [];

    ws.on('message', async (message) => {
        if (Buffer.isBuffer(message)) {
            // 接收音频数据
            audioBuffer.push(message);
            console.log(`ASR 收到音频: ${message.length} bytes`);
        } else {
            // 可能是控制消息
            try {
                const control = JSON.parse(message.toString());
                if (control.action === 'end') {
                    // 合并音频数据
                    const audioData = Buffer.concat(audioBuffer);
                    audioBuffer.length = 0;

                    // 识别
                    const result = await asrEngine.recognize(audioData);
                    
                    // 发送 NLSML 格式结果
                    const nlsml = asrEngine.generateNLSML(result.text, result.confidence);
                    ws.send(nlsml);
                }
            } catch (e) {
                // 忽略解析错误
            }
        }
    });

    ws.on('close', async () => {
        console.log('ASR 客户端断开');
        
        // 如果有未处理的音频，进行识别
        if (audioBuffer.length > 0) {
            try {
                const audioData = Buffer.concat(audioBuffer);
                const result = await asrEngine.recognize(audioData);
                const nlsml = asrEngine.generateNLSML(result.text, result.confidence);
                // 连接已关闭，无法发送结果
                console.log('ASR 结果 (连接已关闭):', result.text);
            } catch (e) {
                console.error('ASR 处理错误:', e);
            }
        }
    });

    ws.on('error', (error) => {
        console.error('ASR WebSocket 错误:', error);
    });
}

/**
 * 创建 HTTP 服务器用于 WebSocket 升级
 */
const server = http.createServer((req, res) => {
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.end('UniMRCP WebSocket Server\n');
});

/**
 * 创建 WebSocket 服务器
 */
const wss = new WebSocket.Server({ server });

wss.on('connection', (ws, req) => {
    const pathname = url.parse(req.url).pathname;
    console.log(`新连接: path=${pathname}`);

    if (pathname === '/tts') {
        handleTTS(ws);
    } else if (pathname === '/asr') {
        handleASR(ws);
    } else {
        console.log(`未知路径: ${pathname}`);
        ws.close(1008, 'Unknown path');
    }
});

/**
 * 启动服务器
 */
server.listen(PORT, HOST, () => {
    console.log(`启动 WebSocket 服务器: ws://${HOST}:${PORT}`);
    console.log('TTS 端点: /tts');
    console.log('ASR 端点: /asr');
});

// 优雅关闭
process.on('SIGINT', () => {
    console.log('\n服务器已停止');
    wss.close();
    server.close();
    process.exit(0);
});
