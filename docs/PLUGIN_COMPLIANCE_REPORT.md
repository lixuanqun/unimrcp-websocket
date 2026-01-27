# WebSocket 插件合规性检查报告

## 检查项目

基于 UniMRCP 插件开发规范的五条强制规则进行检查。

---

## 1. 入口函数检查

### 规范要求
每个插件必须实现 `MRCP_PLUGIN_DECLARE(mrcp_engine_t*) mrcp_plugin_create(apr_pool_t *pool)`

### 检查结果

| 插件 | 状态 | 位置 |
|------|------|------|
| websocket-synth | ✅ 通过 | 第180行 |
| websocket-recog | ✅ 通过 | 第167行 |

---

## 2. 版本声明检查

### 规范要求
每个插件必须声明 `MRCP_PLUGIN_VERSION_DECLARE`

### 检查结果

| 插件 | 状态 | 位置 |
|------|------|------|
| websocket-synth | ✅ 通过 | 第167行 |
| websocket-recog | ✅ 通过 | 第154行 |

---

## 3. 响应规则检查

### 规范要求
每个请求必须且只能发送一个响应

### websocket-synth 检查

| 请求类型 | 响应处理 | 状态 |
|----------|----------|------|
| SPEAK | IN_PROGRESS 响应 + SPEAK-COMPLETE 事件 | ✅ 正确 |
| STOP | 存储后在 stream_read 中发送 | ✅ 正确 |
| PAUSE | 直接发送响应 | ✅ 正确 |
| RESUME | 直接发送响应 | ✅ 正确 |
| SET-PARAMS | 直接发送响应 | ✅ 正确 |
| GET-PARAMS | 直接发送响应 | ✅ 正确 |
| 未处理请求 | 发送默认响应 | ✅ 正确 |

### websocket-recog 检查

| 请求类型 | 响应处理 | 状态 |
|----------|----------|------|
| RECOGNIZE | IN_PROGRESS 响应 + RECOGNITION-COMPLETE 事件 | ✅ 正确 |
| STOP | 存储后在 stream_write 中发送 | ✅ 正确 |
| START-INPUT-TIMERS | 直接发送响应 | ✅ 正确 |
| 未处理请求 | 发送默认响应 | ✅ 正确 |

---

## 4. Channel 回调非阻塞检查

### 规范要求
MRCP 引擎通道的回调方法不能阻塞

### 检查结果

| 插件 | 方法 | 实现方式 | 状态 |
|------|------|----------|------|
| websocket-synth | channel_open | 消息信号到后台任务 | ✅ 非阻塞 |
| websocket-synth | channel_close | 消息信号到后台任务 | ✅ 非阻塞 |
| websocket-synth | request_process | 消息信号到后台任务 | ✅ 非阻塞 |
| websocket-recog | channel_open | 消息信号到后台任务 | ✅ 非阻塞 |
| websocket-recog | channel_close | 消息信号到后台任务 | ✅ 非阻塞 |
| websocket-recog | request_process | 消息信号到后台任务 | ✅ 非阻塞 |

---

## 5. Stream 回调非阻塞检查

### 规范要求
MPF 引擎流的回调方法不能阻塞

### 检查结果

| 插件 | 方法 | 实现方式 | 状态 |
|------|------|----------|------|
| websocket-synth | stream_read | 内存缓冲区读取 + mutex | ✅ 非阻塞 |
| websocket-recog | stream_write | 内存缓冲区写入 + 消息信号 | ✅ 非阻塞 |

---

## 发现的问题

### 问题 1: websocket-recog 字段未初始化 ⚠️

**位置**: `websocket_recog_engine_channel_create()` 函数

**问题**: `timers_started` 字段未初始化

**影响**: 可能导致未定义行为

**修复建议**:
```c
recog_channel->timers_started = FALSE;
```

### 问题 2: JSON 字符串未转义 ⚠️

**位置**: `websocket_synth_build_request_json()` 函数

**问题**: 使用 `apr_psprintf` 直接构建 JSON，未对特殊字符转义

**影响**: 如果文本包含引号、换行等字符，JSON 格式会损坏

**修复建议**: 添加 JSON 转义函数

### 问题 3: websocket-synth 后台任务阻塞 ⚠️

**位置**: `WEBSOCKET_SYNTH_MSG_SPEAK_REQUEST` 处理

**问题**: `websocket_client_receive_audio()` 在后台任务中阻塞接收

**影响**: 阻塞期间无法处理其他消息（如 STOP 请求）

**修复建议**: 
1. 使用非阻塞 socket 或超时机制
2. 或使用专用接收线程

### 问题 4: websocket-recog 接收函数未实现 ⚠️

**位置**: `websocket_client_receive()` 函数

**问题**: 函数只返回 FALSE，未实现

**影响**: 无法接收 WebSocket 响应

**修复建议**: 实现完整的 WebSocket 帧解析

### 问题 5: 变量声明位置 (C89 兼容性) ⚠️

**位置**: websocket-recog 第645-655行

**问题**: 变量声明在代码块中间

**影响**: 某些旧版 C 编译器可能不兼容

---

## 功能正确性检查

### websocket-synth (TTS)

| 功能 | 实现状态 | 说明 |
|------|----------|------|
| SPEAK 请求处理 | ✅ 完整 | 支持 voice, speed, pitch, volume |
| STOP 请求处理 | ✅ 完整 | - |
| PAUSE/RESUME | ✅ 完整 | - |
| WebSocket 连接 | ✅ 完整 | 支持握手和帧处理 |
| 音频缓冲区 | ✅ 完整 | 线程安全，1MB 缓冲 |
| SPEAK-COMPLETE | ✅ 完整 | 正确设置 completion_cause |
| 错误处理 | ⚠️ 基本 | 需要更多错误恢复 |

### websocket-recog (ASR)

| 功能 | 实现状态 | 说明 |
|------|----------|------|
| RECOGNIZE 请求处理 | ✅ 完整 | 支持超时参数 |
| STOP 请求处理 | ✅ 完整 | - |
| START-INPUT-TIMERS | ✅ 完整 | - |
| VAD (语音活动检测) | ✅ 完整 | 使用 mpf_activity_detector |
| WebSocket 连接 | ✅ 完整 | - |
| 音频缓冲 | ✅ 完整 | - |
| START-OF-INPUT 事件 | ✅ 完整 | - |
| RECOGNITION-COMPLETE | ✅ 完整 | 支持多种 completion_cause |
| 接收识别结果 | ❌ 未实现 | `websocket_client_receive` 未实现 |

---

## 总结

### 规范合规性: ✅ 基本符合

两个插件都符合 UniMRCP 的五条核心规范:
1. ✅ 入口函数正确实现
2. ✅ 版本声明正确
3. ✅ 响应规则正确
4. ✅ Channel 回调非阻塞
5. ✅ Stream 回调非阻塞

### 需要改进的问题

| 优先级 | 问题 | 影响 |
|--------|------|------|
| 高 | websocket-recog 接收功能未实现 | 无法获取识别结果 |
| 中 | JSON 字符串转义 | 特殊字符导致错误 |
| 中 | websocket-recog 初始化问题 | 潜在未定义行为 |
| 低 | 后台任务阻塞 | 响应延迟 |
| 低 | C89 兼容性 | 编译器兼容 |

---

## 建议修复

建议按优先级修复上述问题，确保插件的稳定性和功能完整性。
