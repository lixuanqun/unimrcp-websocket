/**
 * UniMRCP WebSocket TTS/ASR 服务示例 (Go)
 *
 * 依赖安装:
 *     go mod init websocket-server
 *     go get github.com/gorilla/websocket
 *
 * 运行:
 *     go run main.go
 *
 * 说明:
 *     这是一个演示用的 WebSocket 服务器，实现了 TTS 和 ASR 的基本接口。
 *     实际使用时需要替换为真实的 TTS/ASR 引擎。
 */

package main

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"log"
	"math"
	"net/http"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

const (
	HOST = "0.0.0.0"
	PORT = 8080
)

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool {
		return true // 允许所有来源
	},
}

// TTSRequest TTS 请求结构
type TTSRequest struct {
	Action     string  `json:"action"`
	Text       string  `json:"text"`
	Voice      string  `json:"voice"`
	Speed      float64 `json:"speed"`
	Pitch      float64 `json:"pitch"`
	Volume     float64 `json:"volume"`
	SampleRate int     `json:"sample_rate"`
	SessionID  string  `json:"session_id"`
}

// ErrorResponse 错误响应结构
type ErrorResponse struct {
	Status  string `json:"status"`
	Code    string `json:"code"`
	Message string `json:"message"`
}

// CompleteResponse 完成响应结构
type CompleteResponse struct {
	Status string `json:"status"`
}

// TTSEngine TTS 引擎
type TTSEngine struct{}

// Synthesize 合成语音
func (e *TTSEngine) Synthesize(req TTSRequest, sendFrame func([]byte), onComplete func()) {
	log.Printf("TTS: text='%s', voice=%s, speed=%.1f, sampleRate=%d",
		req.Text, req.Voice, req.Speed, req.SampleRate)

	// 设置默认值
	if req.SampleRate == 0 {
		req.SampleRate = 8000
	}
	if req.Speed == 0 {
		req.Speed = 1.0
	}
	if req.Pitch == 0 {
		req.Pitch = 1.0
	}
	if req.Volume == 0 {
		req.Volume = 1.0
	}

	// 演示: 生成简单的正弦波音频
	// 实际应用中替换为真实 TTS 引擎的输出
	durationMs := len([]rune(req.Text)) * 200 // 每字符约 200ms
	samplesPerFrame := req.SampleRate / 50    // 20ms 一帧
	totalSamples := req.SampleRate * durationMs / 1000

	frequency := 440.0
	samplesGenerated := 0
	frameCount := 0

	for samplesGenerated < totalSamples {
		frameSamples := samplesPerFrame
		if totalSamples-samplesGenerated < frameSamples {
			frameSamples = totalSamples - samplesGenerated
		}

		frameBuffer := new(bytes.Buffer)

		for i := 0; i < frameSamples; i++ {
			t := float64(samplesGenerated+i) / float64(req.SampleRate)
			// 生成正弦波
			sample := int16(32767 * req.Volume * 0.3 *
				math.Sin(2*math.Pi*frequency*t*req.Pitch))

			binary.Write(frameBuffer, binary.LittleEndian, sample)
		}

		samplesGenerated += frameSamples
		sendFrame(frameBuffer.Bytes())
		frameCount++

		time.Sleep(10 * time.Millisecond)
	}

	log.Printf("TTS 完成: 发送 %d 帧", frameCount)
	onComplete()
}

// ASREngine ASR 引擎
type ASREngine struct{}

// Recognize 识别语音
func (e *ASREngine) Recognize(audioData []byte, sampleRate int) string {
	if sampleRate == 0 {
		sampleRate = 8000
	}
	duration := float64(len(audioData)) / float64(sampleRate*2) // 16-bit
	log.Printf("ASR: received %d bytes, duration=%.2fs", len(audioData), duration)

	// 演示: 返回模拟识别结果
	// 实际应用中替换为真实 ASR 引擎的输出
	text := "这是一段测试语音"
	confidence := 0.95

	return e.GenerateNLSML(text, confidence)
}

// GenerateNLSML 生成 NLSML 格式的识别结果
func (e *ASREngine) GenerateNLSML(text string, confidence float64) string {
	return fmt.Sprintf(`<?xml version="1.0"?>
<result>
  <interpretation grammar="session:request" confidence="%.2f">
    <instance>%s</instance>
    <input mode="speech">%s</input>
  </interpretation>
</result>`, confidence, text, text)
}

var ttsEngine = &TTSEngine{}
var asrEngine = &ASREngine{}

// handleTTS 处理 TTS 请求
func handleTTS(w http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("WebSocket 升级失败: %v", err)
		return
	}
	defer conn.Close()

	log.Println("TTS 客户端连接")

	var writeMu sync.Mutex

	for {
		_, message, err := conn.ReadMessage()
		if err != nil {
			if websocket.IsUnexpectedCloseError(err,
				websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
				log.Printf("TTS 读取错误: %v", err)
			}
			break
		}

		var req TTSRequest
		if err := json.Unmarshal(message, &req); err != nil {
			sendJSONError(conn, &writeMu, "INVALID_REQUEST", "JSON parse error")
			continue
		}

		log.Printf("TTS 请求: %+v", req)

		if req.Action != "tts" {
			sendJSONError(conn, &writeMu, "INVALID_REQUEST", "Invalid action")
			continue
		}

		if req.Text == "" {
			sendJSONError(conn, &writeMu, "TEXT_EMPTY", "Text is empty")
			continue
		}

		// 合成并发送音频
		ttsEngine.Synthesize(req,
			func(frame []byte) {
				writeMu.Lock()
				defer writeMu.Unlock()
				if err := conn.WriteMessage(websocket.BinaryMessage, frame); err != nil {
					log.Printf("发送音频帧失败: %v", err)
				}
			},
			func() {
				writeMu.Lock()
				defer writeMu.Unlock()
				resp := CompleteResponse{Status: "complete"}
				data, _ := json.Marshal(resp)
				conn.WriteMessage(websocket.TextMessage, data)
			},
		)
	}

	log.Println("TTS 客户端断开")
}

// handleASR 处理 ASR 请求
func handleASR(w http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("WebSocket 升级失败: %v", err)
		return
	}
	defer conn.Close()

	log.Println("ASR 客户端连接")

	var audioBuffer bytes.Buffer
	var bufferMu sync.Mutex
	var writeMu sync.Mutex

	for {
		messageType, message, err := conn.ReadMessage()
		if err != nil {
			if websocket.IsUnexpectedCloseError(err,
				websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
				log.Printf("ASR 读取错误: %v", err)
			}
			break
		}

		if messageType == websocket.BinaryMessage {
			// 音频数据
			bufferMu.Lock()
			audioBuffer.Write(message)
			bufferMu.Unlock()
			log.Printf("ASR 收到音频: %d bytes", len(message))

		} else if messageType == websocket.TextMessage {
			// 控制消息
			var control map[string]string
			if err := json.Unmarshal(message, &control); err == nil {
				if control["action"] == "end" {
					bufferMu.Lock()
					audioData := audioBuffer.Bytes()
					audioBuffer.Reset()
					bufferMu.Unlock()

					if len(audioData) > 0 {
						result := asrEngine.Recognize(audioData, 8000)
						writeMu.Lock()
						conn.WriteMessage(websocket.TextMessage, []byte(result))
						writeMu.Unlock()
					}
				}
			}
		}
	}

	// 处理剩余音频
	bufferMu.Lock()
	audioData := audioBuffer.Bytes()
	bufferMu.Unlock()

	if len(audioData) > 0 {
		result := asrEngine.Recognize(audioData, 8000)
		log.Printf("ASR 结果 (连接已关闭): %s", result)
	}

	log.Println("ASR 客户端断开")
}

func sendJSONError(conn *websocket.Conn, mu *sync.Mutex, code, message string) {
	mu.Lock()
	defer mu.Unlock()
	resp := ErrorResponse{
		Status:  "error",
		Code:    code,
		Message: message,
	}
	data, _ := json.Marshal(resp)
	conn.WriteMessage(websocket.TextMessage, data)
}

func main() {
	addr := fmt.Sprintf("%s:%d", HOST, PORT)

	http.HandleFunc("/tts", handleTTS)
	http.HandleFunc("/asr", handleASR)

	log.Printf("启动 WebSocket 服务器: ws://%s", addr)
	log.Println("TTS 端点: /tts")
	log.Println("ASR 端点: /asr")

	if err := http.ListenAndServe(addr, nil); err != nil {
		log.Fatal("服务器启动失败:", err)
	}
}
