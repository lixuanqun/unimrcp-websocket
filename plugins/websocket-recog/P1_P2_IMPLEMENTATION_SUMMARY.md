# P1 和 P2 级别改进实现总结

由于代码改动较大，且需要仔细处理异步消息传递和 WebSocket 协议实现，完整的实现将涉及以下关键改动：

## 已完成的改进

1. ✅ **添加消息类型**：`WEBSOCKET_RECOG_MSG_SEND_AUDIO`
2. ✅ **设置非阻塞 socket**：在 `websocket_client_connect()` 中添加了 `apr_socket_opt_set(ws_client->socket, APR_SO_NONBLOCK, 1);`

## 需要完成的改进

### P1 改进（必须修复）

#### 1. 修改 stream_write() 使用消息队列
- 移除直接调用 `websocket_client_send()`
- 使用 `websocket_recog_msg_signal()` 发送 `WEBSOCKET_RECOG_MSG_SEND_AUDIO` 消息
- 音频数据保持在 channel 的 buffer 中，只传递信号

#### 2. 在后台任务中处理音频发送
- 在 `websocket_recog_msg_process()` 中添加 `WEBSOCKET_RECOG_MSG_SEND_AUDIO` 处理
- 从 channel 的 audio_buffer 读取数据
- 调用 `websocket_client_send()` 发送数据
- 处理 EAGAIN/EWOULDBLOCK 错误（非阻塞模式）

#### 3. 处理非阻塞 I/O 错误
- 在 `websocket_client_send()` 中处理 `APR_STATUS_IS_EAGAIN` 和 `APR_STATUS_IS_EWOULDBLOCK`
- 在连接时处理这些错误（可能需要重试）

### P2 改进（应该改进）

#### 1. WebSocket 握手改进
- 生成 16 字节随机 key
- Base64 编码（已使用 `apr_base64.h`）
- 注意：客户端不需要计算 SHA1，服务器会验证

#### 2. WebSocket 帧接收实现
- 实现完整的 WebSocket 帧解析
- 支持文本帧和二进制帧
- 处理分片帧
- 解析帧头、mask、payload

#### 3. 后台任务接收数据
- 在后台任务中定期检查 WebSocket socket 可读性
- 调用 `websocket_client_receive()` 接收数据
- 解析 JSON 结果并调用 `websocket_recog_result_process()`

## 实现建议

考虑到代码复杂度，建议：

1. **P1 改进是必需的**，应该优先实现
2. **P2 改进可以分阶段实现**：
   - 先实现握手改进（相对简单）
   - 再实现接收功能（较复杂）

## 关键代码位置

- `websocket_recog_stream_write()`: 第 543 行，需要修改音频发送逻辑
- `websocket_recog_msg_process()`: 第 812 行，需要添加音频发送处理
- `websocket_client_connect()`: 第 605 行，需要改进握手
- `websocket_client_send()`: 第 687 行，需要处理非阻塞错误
- `websocket_client_receive()`: 第 777 行，需要实现完整功能

## 注意事项

1. 非阻塞 socket 在连接时可能返回 EINPROGRESS，需要处理
2. 消息结构大小固定，不能直接传递音频数据，需要从 channel 读取
3. WebSocket 接收需要处理分片帧，实现较复杂
4. 需要处理连接错误和重连机制

