# UniMRCP WebSocket 插件评估分析报告

**版本**: 1.0  
**评估日期**: 2026-01-27  
**评估插件**: websocket-synth, websocket-recog

---

## 目录

1. [总体评估](#1-总体评估)
2. [架构设计分析](#2-架构设计分析)
3. [代码质量分析](#3-代码质量分析)
4. [功能完整性评估](#4-功能完整性评估)
5. [性能问题分析](#5-性能问题分析)
6. [安全性评估](#6-安全性评估)
7. [错误处理评估](#7-错误处理评估)
8. [详细问题清单](#8-详细问题清单)
9. [优化建议](#9-优化建议)
10. [优先级排序](#10-优先级排序)

---

## 1. 总体评估

### 1.1 评分概览

| 维度 | websocket-synth | websocket-recog | 说明 |
|------|-----------------|-----------------|------|
| 架构设计 | ★★★★☆ (80%) | ★★★★☆ (80%) | 基础架构合理 |
| 代码质量 | ★★★☆☆ (70%) | ★★★☆☆ (65%) | 存在改进空间 |
| 功能完整性 | ★★★★☆ (75%) | ★★★☆☆ (60%) | 核心功能基本完成 |
| 性能 | ★★☆☆☆ (50%) | ★★☆☆☆ (45%) | 存在阻塞问题 |
| 安全性 | ★★★☆☆ (60%) | ★★☆☆☆ (55%) | 需要加强 |
| 错误处理 | ★★★☆☆ (65%) | ★★★☆☆ (60%) | 部分场景缺失 |
| 可维护性 | ★★★★☆ (75%) | ★★★☆☆ (70%) | 代码结构清晰 |

### 1.2 总体评价

两个插件实现了 UniMRCP 插件的基本框架，遵循了五大强制规则，但在以下方面需要改进：

- **性能**: WebSocket 接收操作存在阻塞风险
- **健壮性**: 错误恢复和边界条件处理不足
- **功能**: ASR 插件缺少真正的流式识别支持
- **安全性**: 缺少输入验证和资源限制

---

## 2. 架构设计分析

### 2.1 整体架构

```
┌──────────────────────────────────────────────────────────────┐
│                     UniMRCP Server                            │
├──────────────────────────────────────────────────────────────┤
│  ┌─────────────────┐           ┌─────────────────┐           │
│  │ websocket-synth │           │ websocket-recog │           │
│  ├─────────────────┤           ├─────────────────┤           │
│  │ Engine Layer    │           │ Engine Layer    │           │
│  │  └ vtable       │           │  └ vtable       │           │
│  ├─────────────────┤           ├─────────────────┤           │
│  │ Channel Layer   │           │ Channel Layer   │           │
│  │  └ vtable       │           │  └ vtable       │           │
│  │  └ ws_client    │           │  └ ws_client    │           │
│  │  └ audio_buffer │           │  └ audio_buffer │           │
│  ├─────────────────┤           ├─────────────────┤           │
│  │ Stream Layer    │           │ Stream Layer    │           │
│  │  └ read (TTS)   │           │  └ write (ASR)  │           │
│  ├─────────────────┤           ├─────────────────┤           │
│  │ Background Task │           │ Background Task │           │
│  │  └ apt_consumer │           │  └ apt_consumer │           │
│  └─────────────────┘           └─────────────────┘           │
└──────────────────────────────────────────────────────────────┘
                    ↓ WebSocket                ↓ WebSocket
          ┌─────────────────┐          ┌─────────────────┐
          │   TTS Service   │          │   ASR Service   │
          └─────────────────┘          └─────────────────┘
```

### 2.2 架构优点

1. **遵循 UniMRCP 规范**: 正确使用了 Engine/Channel/Stream 三层架构
2. **异步任务设计**: 使用 `apt_consumer_task` 处理后台操作
3. **线程安全考虑**: TTS 插件使用互斥锁保护共享缓冲区
4. **配置灵活性**: 支持通过 XML 配置 WebSocket 连接参数

### 2.3 架构问题

| 问题 | 插件 | 严重性 | 说明 |
|------|------|--------|------|
| 单一 WebSocket 连接 | 两者 | 中 | 每个通道一个连接，不支持连接池 |
| 缺少重连机制 | 两者 | 高 | 连接断开后无法自动恢复 |
| 阻塞式接收 | synth | 高 | `websocket_client_receive_audio` 可能阻塞任务 |
| 无流式识别 | recog | 高 | 只在语音结束后批量发送 |

---

## 3. 代码质量分析

### 3.1 代码规范遵循

| 规范 | websocket-synth | websocket-recog | 说明 |
|------|-----------------|-----------------|------|
| 命名规范 | ✅ | ✅ | 遵循 UniMRCP 风格 |
| 注释完整性 | ⚠️ | ⚠️ | 关键函数缺少详细注释 |
| 代码复用 | ⚠️ | ⚠️ | WebSocket 客户端代码重复 |
| 错误日志 | ✅ | ✅ | 使用统一日志系统 |
| 内存管理 | ✅ | ✅ | 使用 APR 池管理 |

### 3.2 代码重复问题

两个插件的 WebSocket 客户端实现几乎相同（约 300 行），应该抽取为共享库：

```c
// websocket-synth/src/websocket_synth_engine.c: 852-1240
// websocket-recog/src/websocket_recog_engine.c: 606-899

// 重复代码包括:
// - websocket_client_connect()
// - websocket_client_send() / websocket_client_send_text()
// - WebSocket 帧解析逻辑
// - websocket_client_destroy()
```

**建议**: 创建 `libs/websocket-client/` 共享模块。

### 3.3 代码复杂度

| 函数 | 圈复杂度 | 建议 |
|------|----------|------|
| `websocket_client_receive_audio` | 15+ | 拆分为多个函数 |
| `websocket_synth_stream_read` | 10 | 可接受，考虑简化 |
| `websocket_recog_stream_write` | 12 | 拆分事件处理 |

---

## 4. 功能完整性评估

### 4.1 websocket-synth (TTS)

| 功能 | 状态 | 说明 |
|------|------|------|
| SPEAK 请求 | ✅ | 完整实现 |
| STOP 请求 | ✅ | 正确处理 |
| PAUSE/RESUME | ✅ | 基本实现 |
| SET-PARAMS | ⚠️ | 仅日志，未真正应用 |
| GET-PARAMS | ⚠️ | 返回硬编码值 |
| BARGE-IN | ✅ | 映射到 STOP |
| 流式输出 | ✅ | 支持边接收边播放 |
| JSON 转义 | ✅ | 已实现安全转义 |

**缺失功能**:
- SSML 解析和支持
- DEFINE-LEXICON 实现
- CONTROL 请求处理
- 语音标记 (Speech Markers)

### 4.2 websocket-recog (ASR)

| 功能 | 状态 | 说明 |
|------|------|------|
| RECOGNIZE 请求 | ⚠️ | 仅批量模式 |
| STOP 请求 | ✅ | 正确处理 |
| START-INPUT-TIMERS | ✅ | 正确实现 |
| SET-PARAMS | ❌ | 未实现 |
| GET-PARAMS | ❌ | 未实现 |
| DEFINE-GRAMMAR | ❌ | 未实现 |
| GET-RESULT | ❌ | 未实现 |
| VAD 检测 | ✅ | 使用 mpf_activity_detector |
| 流式识别 | ❌ | 不支持实时流式 |
| NLSML 响应 | ⚠️ | 依赖外部服务格式 |

**缺失功能**:
- 真正的流式识别（边说边识别）
- 语法加载和验证
- N-Best 结果支持
- 置信度阈值处理
- 热词 (Hotword) 支持

---

## 5. 性能问题分析

### 5.1 严重性能问题

#### 问题 1: TTS 接收阻塞 (Critical)

**位置**: `websocket_synth_engine.c:1110-1213`

```c
static apt_bool_t websocket_client_receive_audio(...)
{
    while (1) {  // 无限循环!
        header_len = 2;
        rv = apr_socket_recv(ws_client->socket, (char*)header, &header_len);
        if (rv != APR_SUCCESS || header_len < 2) {
            if (APR_STATUS_IS_TIMEUP(rv) || APR_STATUS_IS_EAGAIN(rv)) {
                continue;  // 超时后继续循环，可能永不退出
            }
            ...
        }
        ...
    }
}
```

**问题**:
- 在后台任务中无限循环接收数据
- 阻塞了整个 `apt_consumer_task`
- 无法处理其他消息（如 STOP 请求）
- 如果 TTS 服务不发送完成消息，将永久阻塞

**影响**: 服务可能无响应，无法正常停止合成

#### 问题 2: ASR 无流式发送 (High)

**位置**: `websocket_recog_engine.c:568-576`

```c
case MPF_DETECTOR_EVENT_INACTIVITY:
    // 只在语音结束后发送
    if(recog_channel->audio_buffer_pos > 0) {
        websocket_recog_msg_signal(WEBSOCKET_RECOG_MSG_SEND_AUDIO, ...);
    }
    break;
```

**问题**:
- 等待完整语音后才发送
- 增加识别延迟
- 不支持实时流式识别

#### 问题 3: 内存分配策略 (Medium)

```c
// 每次发送都从池中分配
masked_data = apr_palloc(ws_client->pool, len);

// 每次接收帧都分配
payload = apr_palloc(ws_client->pool, payload_len);
```

**问题**:
- APR 池不会释放单个分配
- 长时间运行可能导致内存增长
- 建议使用子池或固定缓冲区

### 5.2 性能指标预估

| 场景 | 当前性能 | 预期优化后 |
|------|----------|-----------|
| TTS 首字延迟 | 500-2000ms | 100-300ms |
| ASR 识别延迟 | 语音结束后 1-3s | 实时 200-500ms |
| 内存占用/通道 | ~2MB | ~500KB |
| 最大并发通道 | 50-100 | 200-500 |

---

## 6. 安全性评估

### 6.1 安全问题

| 问题 | 严重性 | 位置 | 说明 |
|------|--------|------|------|
| 无 TLS 支持 | 高 | 两者 | 明文传输敏感数据 |
| 缓冲区大小固定 | 中 | synth:44 | 1MB 固定，无动态调整 |
| 无输入验证 | 中 | recog | 未验证音频数据大小 |
| 无连接认证 | 中 | 两者 | WebSocket 无认证机制 |
| 资源无限制 | 中 | 两者 | 无最大通道数限制 |

### 6.2 潜在攻击向量

1. **DoS 攻击**: 恶意 TTS 服务发送无限数据导致内存耗尽
2. **中间人攻击**: 无 TLS，可窃听或篡改数据
3. **资源耗尽**: 创建大量通道导致服务崩溃

### 6.3 安全建议

```c
// 1. 添加数据大小限制
#define MAX_AUDIO_RECEIVE_SIZE (10 * 1024 * 1024)  // 10MB

// 2. 添加超时保护
#define MAX_SPEAK_DURATION (300 * 1000000)  // 5分钟

// 3. 添加 TLS 支持 (需要引入 OpenSSL 或类似库)
typedef struct websocket_client_t {
    // ... existing fields
    SSL *ssl;
    SSL_CTX *ssl_ctx;
    apt_bool_t use_tls;
} websocket_client_t;
```

---

## 7. 错误处理评估

### 7.1 错误处理覆盖率

| 错误场景 | websocket-synth | websocket-recog |
|----------|-----------------|-----------------|
| 连接失败 | ✅ 返回错误 | ✅ 返回错误 |
| 发送失败 | ✅ 返回错误 | ✅ 返回错误 |
| 接收超时 | ⚠️ 继续循环 | ✅ 返回错误 |
| 缓冲区溢出 | ⚠️ 仅日志警告 | ⚠️ 静默截断 |
| 连接断开 | ⚠️ 无重连 | ⚠️ 无重连 |
| JSON 解析失败 | N/A | N/A (二进制) |
| 无效帧格式 | ⚠️ 部分处理 | ⚠️ 部分处理 |
| 资源分配失败 | ❌ 未检查 | ❌ 未检查 |

### 7.2 缺失的错误处理

```c
// 问题: apr_palloc 返回值未检查
masked_data = apr_palloc(ws_client->pool, len);
// 应该检查:
if (!masked_data) {
    apt_log(LOG_MARK, APT_PRIO_ERROR, "Memory allocation failed");
    return FALSE;
}

// 问题: 互斥锁操作未检查
apr_thread_mutex_lock(synth_channel->mutex);
// 应该检查返回值

// 问题: 未处理部分发送
sent = len;
rv = apr_socket_send(ws_client->socket, masked_data, &sent);
if (rv != APR_SUCCESS || sent != len) {  // 这里只是返回错误
    // 应该尝试发送剩余数据或正确清理
}
```

---

## 8. 详细问题清单

### 8.1 Critical (必须修复)

| ID | 问题 | 插件 | 行号 | 描述 |
|----|------|------|------|------|
| C1 | 无限循环阻塞 | synth | 1110 | `websocket_client_receive_audio` 可能永不返回 |
| C2 | 任务阻塞 | synth | 1330 | 接收音频阻塞后台任务线程 |
| C3 | 无退出条件 | synth | 1207 | `while(1)` 缺少可靠的退出机制 |

### 8.2 High (应该修复)

| ID | 问题 | 插件 | 行号 | 描述 |
|----|------|------|------|------|
| H1 | 无重连机制 | 两者 | - | WebSocket 断开后无法自动恢复 |
| H2 | 非流式识别 | recog | 568 | 只在语音结束后发送音频 |
| H3 | 无 TLS 支持 | 两者 | - | 数据明文传输 |
| H4 | 缓冲区溢出 | synth | 1164 | 仅日志警告，可能丢失数据 |
| H5 | 套接字非阻塞问题 | recog | 629 | 设置非阻塞但后续操作假设阻塞 |

### 8.3 Medium (建议修复)

| ID | 问题 | 插件 | 行号 | 描述 |
|----|------|------|------|------|
| M1 | 内存分配未检查 | 两者 | 多处 | `apr_palloc` 返回值未验证 |
| M2 | 代码重复 | 两者 | - | WebSocket 客户端代码重复 |
| M3 | 硬编码常量 | 两者 | 44,879 | 缓冲区大小、超时时间等 |
| M4 | SET-PARAMS 未实现 | recog | 416 | 空函数体 |
| M5 | 变量命名 | recog | 904-911 | `demo_channel`, `demo_engine` 应重命名 |
| M6 | 魔数 | 两者 | 多处 | 如 `0x81`, `0x82` 应定义常量 |

### 8.4 Low (可选修复)

| ID | 问题 | 插件 | 行号 | 描述 |
|----|------|------|------|------|
| L1 | 日志级别 | 两者 | 多处 | 部分 DEBUG 日志应为 TRACE |
| L2 | 注释不足 | 两者 | 多处 | 复杂逻辑缺少注释 |
| L3 | 函数过长 | synth | 1094 | `websocket_client_receive_audio` 超过 100 行 |

---

## 9. 优化建议

### 9.1 架构优化

#### 9.1.1 提取共享 WebSocket 客户端库

```
libs/
└── websocket-client/
    ├── CMakeLists.txt
    ├── include/
    │   └── websocket_client.h
    └── src/
        └── websocket_client.c
```

```c
// websocket_client.h
typedef struct ws_client_t ws_client_t;

typedef struct ws_client_config_t {
    const char *host;
    int port;
    const char *path;
    apt_bool_t use_tls;
    apr_interval_time_t connect_timeout;
    apr_interval_time_t recv_timeout;
} ws_client_config_t;

typedef struct ws_client_callbacks_t {
    void (*on_connected)(ws_client_t *client, void *user_data);
    void (*on_message)(ws_client_t *client, const char *data, apr_size_t len, 
                       apt_bool_t is_binary, void *user_data);
    void (*on_error)(ws_client_t *client, apr_status_t error, void *user_data);
    void (*on_closed)(ws_client_t *client, void *user_data);
} ws_client_callbacks_t;

ws_client_t* ws_client_create(apr_pool_t *pool, const ws_client_config_t *config,
                               const ws_client_callbacks_t *callbacks, void *user_data);
apt_bool_t ws_client_connect(ws_client_t *client);
apt_bool_t ws_client_send_text(ws_client_t *client, const char *data, apr_size_t len);
apt_bool_t ws_client_send_binary(ws_client_t *client, const char *data, apr_size_t len);
apt_bool_t ws_client_poll(ws_client_t *client, apr_interval_time_t timeout);
void ws_client_close(ws_client_t *client);
void ws_client_destroy(ws_client_t *client);
```

#### 9.1.2 非阻塞接收模式

```c
// 替代 while(1) 无限循环
// 使用定时器 + 轮询模式

static apt_bool_t websocket_synth_msg_process(apt_task_t *task, apt_task_msg_t *msg)
{
    switch (synth_msg->type) {
        case WEBSOCKET_SYNTH_MSG_SPEAK_REQUEST:
            // 发送 TTS 请求
            websocket_client_send_text(...);
            // 设置接收定时器，定期检查数据
            apt_timer_set(synth_channel->recv_timer, RECV_POLL_INTERVAL);
            break;
            
        case WEBSOCKET_SYNTH_MSG_RECV_POLL:
            // 非阻塞接收
            if (websocket_client_poll(ws_client, 0)) {
                // 处理接收到的数据
            }
            if (!synth_channel->audio_complete) {
                // 继续轮询
                apt_timer_set(synth_channel->recv_timer, RECV_POLL_INTERVAL);
            }
            break;
    }
}
```

### 9.2 性能优化

#### 9.2.1 流式识别支持

```c
// websocket_recog_stream_write 修改
static apt_bool_t websocket_recog_stream_write(mpf_audio_stream_t *stream, const mpf_frame_t *frame)
{
    // ...
    
    // 实时发送音频帧
    if ((frame->type & MEDIA_FRAME_TYPE_AUDIO) == MEDIA_FRAME_TYPE_AUDIO) {
        // 直接通过 WebSocket 发送 (非阻塞)
        if (recog_channel->streaming_enabled) {
            websocket_recog_msg_signal(WEBSOCKET_RECOG_MSG_SEND_FRAME, 
                                       recog_channel->channel, frame);
        }
        
        // 同时缓存用于 VAD 处理
        memcpy(recog_channel->audio_buffer + recog_channel->audio_buffer_pos,
               frame->codec_frame.buffer, frame->codec_frame.size);
        recog_channel->audio_buffer_pos += frame->codec_frame.size;
    }
}
```

#### 9.2.2 环形缓冲区

```c
// 替换固定缓冲区
typedef struct ring_buffer_t {
    char *data;
    apr_size_t size;
    apr_size_t read_pos;
    apr_size_t write_pos;
    apr_thread_mutex_t *mutex;
} ring_buffer_t;

// TTS 通道使用环形缓冲区
struct websocket_synth_channel_t {
    ring_buffer_t *audio_ring;
    // ...
};
```

#### 9.2.3 连接池

```c
// 引入连接池管理
typedef struct ws_connection_pool_t {
    apr_pool_t *pool;
    apr_thread_mutex_t *mutex;
    ws_client_t **connections;
    apr_size_t size;
    apr_size_t capacity;
    const ws_client_config_t *config;
} ws_connection_pool_t;

ws_client_t* ws_pool_acquire(ws_connection_pool_t *pool);
void ws_pool_release(ws_connection_pool_t *pool, ws_client_t *client);
```

### 9.3 安全优化

#### 9.3.1 TLS 支持

```c
// 添加 TLS 配置
<engine id="WebSocket-Synth-1" name="websocketsynth" enable="true">
    <param name="ws-host" value="tts.example.com"/>
    <param name="ws-port" value="443"/>
    <param name="ws-path" value="/tts"/>
    <param name="ws-tls" value="true"/>
    <param name="ws-ca-file" value="/etc/ssl/certs/ca-bundle.crt"/>
</engine>

// 代码实现
#ifdef WITH_OPENSSL
apt_bool_t ws_client_connect_tls(ws_client_t *client);
#endif
```

#### 9.3.2 资源限制

```c
// 添加配置参数
<engine id="WebSocket-Synth-1" name="websocketsynth" enable="true">
    <max-channel-count>100</max-channel-count>
    <param name="max-audio-size" value="10485760"/>  <!-- 10MB -->
    <param name="max-speak-duration" value="300000"/> <!-- 5 minutes -->
    <param name="connect-timeout" value="30000"/>     <!-- 30 seconds -->
</engine>

// 代码检查
if (synth_channel->audio_buffer_write_pos + payload_len > MAX_AUDIO_SIZE) {
    apt_log(LOG_MARK, APT_PRIO_ERROR, "Audio data exceeds maximum size limit");
    websocket_synth_speak_complete(synth_channel, SYNTHESIZER_COMPLETION_CAUSE_ERROR);
    return FALSE;
}
```

### 9.4 错误处理优化

```c
// 添加重连机制
static apt_bool_t websocket_client_ensure_connected(websocket_client_t *ws_client)
{
    int retry_count = 0;
    const int max_retries = 3;
    const apr_interval_time_t retry_delay = 1000000; // 1 second
    
    while (!ws_client->connected && retry_count < max_retries) {
        if (websocket_client_connect(ws_client)) {
            return TRUE;
        }
        retry_count++;
        apr_sleep(retry_delay);
    }
    
    return ws_client->connected;
}

// 添加心跳检测
static apt_bool_t websocket_client_send_ping(websocket_client_t *ws_client)
{
    unsigned char ping[6];
    apr_size_t len = 6;
    
    ping[0] = 0x89;  // FIN + Ping opcode
    ping[1] = 0x80;  // Mask + 0 length
    // ... mask bytes
    
    return apr_socket_send(ws_client->socket, (char*)ping, &len) == APR_SUCCESS;
}
```

---

## 10. 优先级排序

### 10.1 立即修复 (P0)

| 优先级 | 问题 | 工作量 | 影响 |
|--------|------|--------|------|
| P0-1 | 修复 TTS 接收阻塞问题 | 高 | 服务可用性 |
| P0-2 | 添加接收超时和退出条件 | 中 | 服务可用性 |

### 10.2 短期修复 (P1)

| 优先级 | 问题 | 工作量 | 影响 |
|--------|------|--------|------|
| P1-1 | 添加 WebSocket 重连机制 | 中 | 可靠性 |
| P1-2 | 修复内存分配检查 | 低 | 稳定性 |
| P1-3 | 添加资源限制 | 中 | 安全性 |
| P1-4 | 修复 ASR 非阻塞套接字问题 | 中 | 正确性 |

### 10.3 中期优化 (P2)

| 优先级 | 问题 | 工作量 | 影响 |
|--------|------|--------|------|
| P2-1 | 提取共享 WebSocket 库 | 高 | 可维护性 |
| P2-2 | 实现流式 ASR | 高 | 性能 |
| P2-3 | 添加 TLS 支持 | 高 | 安全性 |
| P2-4 | 使用环形缓冲区 | 中 | 性能 |

### 10.4 长期改进 (P3)

| 优先级 | 问题 | 工作量 | 影响 |
|--------|------|--------|------|
| P3-1 | 连接池支持 | 高 | 性能 |
| P3-2 | 完善 MRCP 方法实现 | 中 | 功能 |
| P3-3 | 添加监控指标 | 中 | 运维 |
| P3-4 | 单元测试覆盖 | 高 | 质量 |

---

## 附录 A: 推荐修复代码

### A.1 修复 TTS 接收阻塞

```c
// 新增: 单次接收函数 (非阻塞)
static apt_bool_t websocket_client_receive_once(
    websocket_client_t *ws_client,
    websocket_synth_channel_t *synth_channel,
    apt_bool_t *complete)
{
    apr_status_t rv;
    unsigned char header[2];
    apr_size_t header_len = 2;
    
    *complete = FALSE;
    
    // 设置短超时
    apr_socket_timeout_set(ws_client->socket, 100000); // 100ms
    
    rv = apr_socket_recv(ws_client->socket, (char*)header, &header_len);
    if (APR_STATUS_IS_TIMEUP(rv) || APR_STATUS_IS_EAGAIN(rv)) {
        return TRUE;  // 无数据，但不是错误
    }
    if (rv != APR_SUCCESS || header_len < 2) {
        return FALSE;  // 真正的错误
    }
    
    // 处理帧...
    // 如果是完成帧，设置 *complete = TRUE
    
    return TRUE;
}

// 修改后台任务消息处理
case WEBSOCKET_SYNTH_MSG_SPEAK_REQUEST:
{
    // 发送请求
    websocket_client_send_text(...);
    
    // 设置定时器开始轮询
    websocket_synth_msg_signal(WEBSOCKET_SYNTH_MSG_RECV_POLL, channel, NULL);
    break;
}

case WEBSOCKET_SYNTH_MSG_RECV_POLL:
{
    apt_bool_t complete = FALSE;
    
    // 非阻塞接收
    if (!websocket_client_receive_once(ws_client, synth_channel, &complete)) {
        websocket_synth_speak_complete(synth_channel, SYNTHESIZER_COMPLETION_CAUSE_ERROR);
        break;
    }
    
    if (complete) {
        // 接收完成，由 stream_read 发送 SPEAK-COMPLETE
        break;
    }
    
    // 检查是否有 STOP 请求
    if (synth_channel->stop_response) {
        break;
    }
    
    // 继续轮询
    websocket_synth_msg_signal(WEBSOCKET_SYNTH_MSG_RECV_POLL, channel, NULL);
    break;
}
```

### A.2 WebSocket 帧常量定义

```c
// websocket_constants.h
#define WS_OPCODE_CONTINUATION  0x00
#define WS_OPCODE_TEXT          0x01
#define WS_OPCODE_BINARY        0x02
#define WS_OPCODE_CLOSE         0x08
#define WS_OPCODE_PING          0x09
#define WS_OPCODE_PONG          0x0A

#define WS_FIN_BIT              0x80
#define WS_MASK_BIT             0x80
#define WS_PAYLOAD_LEN_MASK     0x7F
#define WS_PAYLOAD_LEN_16BIT    126
#define WS_PAYLOAD_LEN_64BIT    127
```

---

## 附录 B: 测试建议

### B.1 单元测试用例

```
1. WebSocket 连接测试
   - 正常连接
   - 连接超时
   - 主机不可达
   - 握手失败

2. TTS 功能测试
   - 正常 SPEAK 请求
   - 空文本处理
   - 特殊字符转义
   - STOP 中断
   - 大文本处理

3. ASR 功能测试
   - 正常 RECOGNIZE 请求
   - 无输入超时
   - 长语音处理
   - VAD 检测准确性

4. 错误恢复测试
   - 连接断开重连
   - 服务端错误响应
   - 超时处理
```

### B.2 性能测试用例

```
1. 并发测试
   - 100 并发 TTS 请求
   - 100 并发 ASR 请求
   - 混合请求负载

2. 长时间运行测试
   - 24 小时连续运行
   - 内存泄漏检测
   - 连接稳定性

3. 边界测试
   - 最大音频大小
   - 最长合成时间
   - 最多并发通道
```

---

**报告结束**
