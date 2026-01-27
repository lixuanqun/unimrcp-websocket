#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
WebSocket 客户端测试工具

用于测试 TTS/ASR WebSocket 服务。

依赖安装:
    pip install websockets

使用方法:
    # 测试 TTS
    python test_client.py tts "你好世界"
    
    # 测试 ASR (使用测试音频文件)
    python test_client.py asr test.pcm
"""

import asyncio
import sys
import json
import struct

try:
    import websockets
except ImportError:
    print("请安装 websockets: pip install websockets")
    exit(1)

WS_HOST = "localhost"
WS_PORT = 8080


async def test_tts(text):
    """测试 TTS 服务"""
    uri = f"ws://{WS_HOST}:{WS_PORT}/tts"
    
    print(f"连接到 {uri}")
    async with websockets.connect(uri) as ws:
        # 发送 TTS 请求
        request = {
            "action": "tts",
            "text": text,
            "voice": "default",
            "speed": 1.0,
            "pitch": 1.0,
            "volume": 1.0,
            "sample_rate": 8000
        }
        await ws.send(json.dumps(request))
        print(f"发送请求: {text}")
        
        # 接收音频数据
        audio_data = bytearray()
        frame_count = 0
        
        while True:
            message = await ws.recv()
            
            if isinstance(message, bytes):
                audio_data.extend(message)
                frame_count += 1
                print(f"\r收到音频帧: {frame_count}, 总字节: {len(audio_data)}", end="")
            else:
                try:
                    response = json.loads(message)
                    print(f"\n收到响应: {response}")
                    if response.get("status") == "complete":
                        break
                    elif response.get("status") == "error":
                        print(f"错误: {response.get('message')}")
                        break
                except json.JSONDecodeError:
                    print(f"\n收到文本: {message}")
        
        # 保存音频文件
        output_file = "output.pcm"
        with open(output_file, "wb") as f:
            f.write(audio_data)
        print(f"\n音频已保存到 {output_file}")
        print(f"总帧数: {frame_count}")
        print(f"音频时长: {len(audio_data) / (8000 * 2):.2f} 秒")


async def test_asr(audio_file):
    """测试 ASR 服务"""
    uri = f"ws://{WS_HOST}:{WS_PORT}/asr"
    
    # 读取音频文件
    try:
        with open(audio_file, "rb") as f:
            audio_data = f.read()
        print(f"读取音频文件: {audio_file}, 大小: {len(audio_data)} bytes")
    except FileNotFoundError:
        # 如果没有音频文件，生成测试音频
        print("音频文件不存在，生成测试音频...")
        audio_data = generate_test_audio()
        print(f"生成测试音频: {len(audio_data)} bytes")
    
    print(f"连接到 {uri}")
    async with websockets.connect(uri) as ws:
        # 分帧发送音频
        frame_size = 320  # 20ms @ 8kHz
        frames_sent = 0
        
        for i in range(0, len(audio_data), frame_size):
            frame = audio_data[i:i+frame_size]
            await ws.send(frame)
            frames_sent += 1
            print(f"\r发送音频帧: {frames_sent}", end="")
            await asyncio.sleep(0.02)  # 模拟实时发送
        
        print(f"\n发送完成，等待识别结果...")
        
        # 发送结束信号
        await ws.send(json.dumps({"action": "end"}))
        
        # 接收识别结果
        try:
            result = await asyncio.wait_for(ws.recv(), timeout=10.0)
            print(f"识别结果:\n{result}")
        except asyncio.TimeoutError:
            print("等待识别结果超时")


def generate_test_audio():
    """生成测试音频 (1秒正弦波)"""
    import math
    
    sample_rate = 8000
    duration = 1.0
    frequency = 440.0
    
    samples = int(sample_rate * duration)
    audio_data = bytearray(samples * 2)
    
    for i in range(samples):
        t = i / sample_rate
        sample = int(32767 * 0.3 * math.sin(2 * math.pi * frequency * t))
        struct.pack_into('<h', audio_data, i * 2, sample)
    
    return bytes(audio_data)


def print_usage():
    print("用法:")
    print("  python test_client.py tts <文本>")
    print("  python test_client.py asr <音频文件.pcm>")
    print()
    print("示例:")
    print("  python test_client.py tts \"你好世界\"")
    print("  python test_client.py asr recording.pcm")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print_usage()
        sys.exit(1)
    
    mode = sys.argv[1].lower()
    
    if mode == "tts":
        text = sys.argv[2]
        asyncio.run(test_tts(text))
    elif mode == "asr":
        audio_file = sys.argv[2]
        asyncio.run(test_asr(audio_file))
    else:
        print(f"未知模式: {mode}")
        print_usage()
        sys.exit(1)
