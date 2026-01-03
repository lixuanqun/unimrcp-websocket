# WebSocket 插件设计评估报告

## 评估日期
2024-01-02

## 评估依据
基于 `docs/PLUGIN_DEVELOPMENT_GUIDE.md` 中的插件开发指南

## 发现的问题

### 1. WebSocket 帧编码问题（严重）

**问题描述**：
- WebSocket 客户端发送数据时必须使用 mask（RFC 6455 要求）
- 当前实现缺少 mask 字段和 masking key
- 这会导致 WebSocket 服务器拒绝连接

**位置**：`websocket_client_send()` 函数

**影响**：插件无法正常工作

**修复方案**：
- 添加 4 字节 masking key 生成
- 在帧头设置 MASK 位
- 对 payload 应用 mask

### 2. 阻塞调用问题（中等）

**问题描述**：
- `websocket_client_send()` 在 `stream_write()` 回调中被调用
- `stream_write()` 必须非阻塞
- `apr_socket_send()` 可能阻塞

**位置**：`websocket_recog_stream_write()` 函数

**影响**：可能阻塞音频处理线程

**修复方案**：
- 将音频数据发送移到后台任务的消息队列
- 使用异步消息传递

### 3. 接收功能未实现（中等）

**问题描述**：
- `websocket_client_receive()` 函数未实现
- 无法接收识别结果

**位置**：`websocket_client_receive()` 函数

**影响**：无法获取识别结果

**修复方案**：
- 实现 WebSocket 帧解析
- 在后台任务中异步接收
- 解析 JSON 结果并发送 MRCP 事件

### 4. 内存管理问题（轻微）

**问题描述**：
- `audio_buffer` 使用 `apr_palloc` 分配
- 在 `destroy` 中使用 `apr_pfree` 释放
- APR 内存池会自动管理，不需要手动释放

**位置**：`websocket_recog_channel_destroy()` 函数

**影响**：可能导致双重释放

**修复方案**：
- 移除 `apr_pfree` 调用
- 依赖内存池自动管理

### 5. WebSocket 握手简化（可接受）

**问题描述**：
- WebSocket 握手使用固定的 key
- 未实现 SHA1 哈希计算
- 未验证服务器响应

**位置**：`websocket_client_connect()` 函数

**影响**：可能不被所有服务器接受，但对于测试可以接受

**修复方案**：暂时保留，后续优化

### 6. 连接管理（可改进）

**问题描述**：
- 每个 channel 创建独立的 WebSocket 连接
- 没有连接复用机制
- 连接错误处理不完善

**位置**：`websocket_recog_engine_channel_create()` 函数

**影响**：资源使用效率较低

**修复方案**：可以接受，后续优化

## 修复优先级

1. **P0（必须修复）**：
   - WebSocket 帧 mask 问题

2. **P1（应该修复）**：
   - 阻塞调用问题
   - 内存管理问题

3. **P2（可以改进）**：
   - 接收功能实现
   - WebSocket 握手优化
   - 连接管理优化

## 修复计划

### 阶段 1：关键问题修复
- [x] 修复 WebSocket 帧 mask
- [x] 修复内存管理
- [ ] 修复阻塞调用（简化处理）

### 阶段 2：功能完善
- [ ] 实现接收功能
- [ ] 完善错误处理
- [ ] 添加重连机制

### 阶段 3：优化
- [ ] 连接复用
- [ ] WebSocket 握手优化
- [ ] 性能优化

## 结论

当前实现存在一些关键问题，但整体架构是合理的。通过修复 P0 和 P1 级别的问题，插件可以基本运行。P2 级别的问题可以在后续版本中优化。

主要优点：
- 正确使用了后台任务处理异步操作
- 正确实现了 MRCP 接口
- 代码结构清晰

主要缺点：
- WebSocket 协议实现不完整
- 缺少错误处理
- 阻塞调用问题

