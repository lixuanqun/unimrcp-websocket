#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
UniMRCP WebSocket TTS/ASR 服务示例 (Python)

依赖安装:
    pip install websockets

运行:
    python websocket_server.py

说明:
    这是一个演示用的 WebSocket 服务器，实现了 TTS 和 ASR 的基本接口。
    实际使用时需要替换为真实的 TTS/ASR 引擎。
"""

import asyncio
import json
import struct
import logging
import math
from typing import Optional

try:
    import websockets
    from websockets.server import serve
except ImportError:
    print("请安装 websockets: pip install websockets")
    exit(1)

# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# 服务器配置
HOST = "0.0.0.0"
PORT = 8080


class TTSEngine:
    """TTS 引擎演示类"""
    
    def __init__(self):
        self.voices = ["default", "male", "female", "child"]
    
    async def synthesize(
        self,
        text: str,
        voice: str = "default",
        speed: float = 1.0,
        pitch: float = 1.0,
        volume: float = 1.0,
        sample_rate: int = 8000
    ):
        """
        合成语音 (生成器模式，边合成边返回)
        
        实际应用中这里调用真实的 TTS 引擎，如:
        - 阿里云 TTS
        - 百度 TTS
        - 讯飞 TTS
        - Azure TTS
        """
        logger.info(f"TTS: text='{text}', voice={voice}, speed={speed}, sample_rate={sample_rate}")
        
        # 演示: 生成简单的正弦波音频
        # 实际应用中替换为真实 TTS 引擎的输出
        duration_ms = len(text) * 200  # 每字符约 200ms
        samples_per_frame = sample_rate // 50  # 20ms 一帧
        total_samples = int(sample_rate * duration_ms / 1000)
        
        frequency = 440.0  # 基础频率
        samples_generated = 0
        
        while samples_generated < total_samples:
            # 生成一帧音频
            frame_samples = min(samples_per_frame, total_samples - samples_generated)
            frame_data = bytearray(frame_samples * 2)  # 16-bit = 2 bytes
            
            for i in range(frame_samples):
                t = (samples_generated + i) / sample_rate
                # 生成正弦波
                sample = int(32767 * volume * 0.3 * math.sin(2 * math.pi * frequency * t * pitch))
                # 限幅
                sample = max(-32768, min(32767, sample))
                # Little-endian 16-bit
                struct.pack_into('<h', frame_data, i * 2, sample)
            
            samples_generated += frame_samples
            
            yield bytes(frame_data)
            
            # 模拟处理延迟
            await asyncio.sleep(0.01)


class ASREngine:
    """ASR 引擎演示类"""
    
    def __init__(self):
        pass
    
    async def recognize(self, audio_data: bytes, sample_rate: int = 8000) -> dict:
        """
        识别语音
        
        实际应用中这里调用真实的 ASR 引擎，如:
        - 阿里云 ASR
        - 百度 ASR
        - 讯飞 ASR
        - Azure ASR
        """
        audio_duration = len(audio_data) / (sample_rate * 2)  # 16-bit
        logger.info(f"ASR: received {len(audio_data)} bytes, duration={audio_duration:.2f}s")
        
        # 演示: 返回模拟识别结果
        # 实际应用中替换为真实 ASR 引擎的输出
        return {
            "status": "success",
            "text": "这是一段测试语音",
            "confidence": 0.95
        }
    
    def generate_nlsml(self, text: str, confidence: float = 0.95) -> str:
        """生成 NLSML 格式的识别结果"""
        return f'''<?xml version="1.0"?>
<result>
  <interpretation grammar="session:request" confidence="{confidence:.2f}">
    <instance>{text}</instance>
    <input mode="speech">{text}</input>
  </interpretation>
</result>'''


# 全局引擎实例
tts_engine = TTSEngine()
asr_engine = ASREngine()


async def handle_tts(websocket, path):
    """处理 TTS 请求"""
    client_addr = websocket.remote_address
    logger.info(f"TTS 客户端连接: {client_addr}")
    
    try:
        async for message in websocket:
            try:
                # 解析 JSON 请求
                request = json.loads(message)
                logger.info(f"TTS 请求: {request}")
                
                if request.get("action") != "tts":
                    await websocket.send(json.dumps({
                        "status": "error",
                        "code": "INVALID_REQUEST",
                        "message": "Invalid action"
                    }))
                    continue
                
                text = request.get("text", "")
                if not text:
                    await websocket.send(json.dumps({
                        "status": "error",
                        "code": "TEXT_EMPTY",
                        "message": "Text is empty"
                    }))
                    continue
                
                # 获取参数
                voice = request.get("voice", "default")
                speed = float(request.get("speed", 1.0))
                pitch = float(request.get("pitch", 1.0))
                volume = float(request.get("volume", 1.0))
                sample_rate = int(request.get("sample_rate", 8000))
                
                # 流式合成并发送音频
                frame_count = 0
                async for audio_frame in tts_engine.synthesize(
                    text, voice, speed, pitch, volume, sample_rate
                ):
                    await websocket.send(audio_frame)
                    frame_count += 1
                
                logger.info(f"TTS 完成: 发送 {frame_count} 帧")
                
                # 发送完成消息
                await websocket.send(json.dumps({"status": "complete"}))
                
            except json.JSONDecodeError as e:
                logger.error(f"JSON 解析错误: {e}")
                await websocket.send(json.dumps({
                    "status": "error",
                    "code": "INVALID_REQUEST",
                    "message": str(e)
                }))
                
    except websockets.exceptions.ConnectionClosed:
        logger.info(f"TTS 客户端断开: {client_addr}")
    except Exception as e:
        logger.error(f"TTS 处理错误: {e}")


async def handle_asr(websocket, path):
    """处理 ASR 请求"""
    client_addr = websocket.remote_address
    logger.info(f"ASR 客户端连接: {client_addr}")
    
    audio_buffer = bytearray()
    
    try:
        async for message in websocket:
            if isinstance(message, bytes):
                # 接收音频数据
                audio_buffer.extend(message)
                logger.debug(f"ASR 收到音频: {len(message)} bytes, 总计: {len(audio_buffer)} bytes")
                
                # 这里可以实现流式识别
                # 演示中我们等待接收完成后再识别
                
            elif isinstance(message, str):
                # 可能是控制消息
                try:
                    control = json.loads(message)
                    if control.get("action") == "end":
                        # 开始识别
                        result = await asr_engine.recognize(bytes(audio_buffer))
                        
                        # 发送 NLSML 格式结果
                        nlsml = asr_engine.generate_nlsml(
                            result["text"],
                            result.get("confidence", 0.95)
                        )
                        await websocket.send(nlsml)
                        
                        # 清空缓冲区
                        audio_buffer.clear()
                except json.JSONDecodeError:
                    pass
        
        # 连接关闭时，如果有未处理的音频，进行识别
        if audio_buffer:
            result = await asr_engine.recognize(bytes(audio_buffer))
            nlsml = asr_engine.generate_nlsml(
                result["text"],
                result.get("confidence", 0.95)
            )
            await websocket.send(nlsml)
            
    except websockets.exceptions.ConnectionClosed:
        logger.info(f"ASR 客户端断开: {client_addr}")
    except Exception as e:
        logger.error(f"ASR 处理错误: {e}")


async def router(websocket, path):
    """路由请求到对应处理器"""
    logger.info(f"新连接: path={path}")
    
    if path == "/tts":
        await handle_tts(websocket, path)
    elif path == "/asr":
        await handle_asr(websocket, path)
    else:
        logger.warning(f"未知路径: {path}")
        await websocket.close(1008, "Unknown path")


async def main():
    """主函数"""
    logger.info(f"启动 WebSocket 服务器: ws://{HOST}:{PORT}")
    logger.info("TTS 端点: /tts")
    logger.info("ASR 端点: /asr")
    
    async with serve(router, HOST, PORT):
        await asyncio.Future()  # 永久运行


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("服务器已停止")
