# P1 和 P2 级别改进说明

## 改进目标

### P1（必须修复）- 非阻塞 WebSocket I/O
1. 将 WebSocket I/O 操作移到后台任务，避免阻塞 `stream_write()` 回调
2. 设置 WebSocket socket 为非阻塞模式

### P2（应该改进）- 功能完善
1. 实现 WebSocket 接收功能
2. 完善 WebSocket 握手实现（SHA1 + Base64）

## 实现计划

由于代码改动较大，将分步骤实现：

### 步骤 1: 添加消息类型和结构（P1）
- 添加 `WEBSOCKET_RECOG_MSG_SEND_AUDIO` 消息类型
- 修改消息结构以支持音频数据发送

### 步骤 2: 设置非阻塞 socket（P1）
- 在 `websocket_client_connect()` 中设置 `APR_SO_NONBLOCK`

### 步骤 3: 修改 stream_write（P1）
- 移除直接调用 `websocket_client_send()`
- 使用消息队列发送音频数据发送请求

### 步骤 4: 后台任务处理（P1）
- 在 `websocket_recog_msg_process()` 中处理音频发送

### 步骤 5: WebSocket 握手改进（P2）
- 生成随机 key
- 实现 SHA1 + Base64（如果需要）

### 步骤 6: WebSocket 接收实现（P2）
- 实现帧解析
- 在后台任务中接收数据

## 注意事项

1. 消息结构大小固定，音频数据保持在 channel 的 buffer 中
2. 需要使用互斥锁保护共享数据（音频 buffer）
3. WebSocket socket 需要设置为非阻塞模式
4. 需要处理 EAGAIN/EWOULDBLOCK 错误

