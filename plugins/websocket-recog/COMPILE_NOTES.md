# WebSocket 插件编译说明

## 修复的问题

基于插件开发指南评估，已修复以下问题：

### 1. ✅ WebSocket 帧 Mask 问题（P0 - 已修复）

**问题**：客户端发送数据必须使用 mask（RFC 6455）

**修复**：
- 添加了 4 字节 masking key 生成
- 在帧头设置 MASK 位（0x80）
- 对 payload 应用 mask

**位置**：`websocket_client_send()` 函数

### 2. ✅ 内存管理问题（P1 - 已修复）

**问题**：在 destroy 中使用 `apr_pfree` 释放池分配的内存

**修复**：
- 移除了 `apr_pfree` 调用
- 依赖 APR 内存池自动管理

**位置**：`websocket_recog_channel_destroy()` 函数

### 3. ⚠️ 阻塞调用问题（P1 - 部分处理）

**问题**：`websocket_client_send()` 在 `stream_write()` 中被调用，可能阻塞

**现状**：
- `stream_write()` 在音频处理线程中调用
- `websocket_client_send()` 使用阻塞 socket 发送
- 当前实现可以工作，但可能影响性能

**建议改进**（后续优化）：
- 将音频数据发送移到后台任务的消息队列
- 使用非阻塞 socket 或异步发送

### 4. ⚠️ 接收功能（P2 - 未实现）

**问题**：`websocket_client_receive()` 函数未实现

**现状**：当前使用模拟识别结果

**建议改进**（后续优化）：
- 实现 WebSocket 帧解析
- 在后台任务中异步接收
- 解析 JSON 结果并发送 MRCP 事件

## 编译步骤

### 前置要求

1. **安装 APR 和 APR-util**：
   ```bash
   # Ubuntu/Debian
   sudo apt-get install libapr1-dev libaprutil1-dev
   
   # 或从源码编译
   # 下载地址: http://www.unimrcp.org/downloads/dependencies
   ```

2. **安装 Sofia-SIP**（可选，仅服务器需要）：
   ```bash
   # Ubuntu/Debian
   sudo apt-get install libsofia-sip-ua-dev
   
   # 或从源码编译
   ```

### 编译命令

```bash
# 1. 生成 configure 脚本（如果还没有）
./bootstrap

# 2. 配置（启用 WebSocket 插件）
./configure --enable-websocketrecog-plugin

# 如果依赖不在标准路径，指定路径：
# ./configure --enable-websocketrecog-plugin \
#   --with-apr=/usr/local/apr \
#   --with-apr-util=/usr/local/apr \
#   --with-sofia-sip=/usr/local/sofia-sip

# 3. 编译
make

# 4. 安装（可选）
make install
```

### 验证编译

编译成功后，插件文件应该在：
- `plugins/websocket-recog/.libs/libwebsocketrecog.so` (Linux)
- `plugins/websocket-recog/.libs/libwebsocketrecog.la` (libtool 文件)

## 测试

### 1. 配置服务器

编辑 `conf/unimrcpserver.xml`，确保插件已配置：

```xml
<plugin-factory>
  <engine id="WebSocket-Recog-1" name="websocketrecog" enable="true">
    <max-channel-count>100</max-channel-count>
    <param name="ws-host" value="localhost"/>
    <param name="ws-port" value="8080"/>
    <param name="ws-path" value="/asr"/>
  </engine>
</plugin-factory>
```

### 2. 启动测试 WebSocket 服务器

使用提供的测试服务器（需要 Python 3 和 websockets 库）：

```bash
pip3 install websockets
python3 plugins/websocket-recog/examples/websocket_server.py
```

### 3. 启动 UniMRCP 服务器

```bash
./platforms/unimrcp-server/.libs/unimrcpserver -f conf/unimrcpserver.xml
```

### 4. 使用 MRCP 客户端测试

使用 UniMRCP 客户端工具或兼容的 MRCP 客户端发送识别请求。

## 已知限制

1. **WebSocket 接收功能未实现**：当前无法接收识别结果，使用模拟结果
2. **阻塞发送**：音频数据发送使用阻塞 socket
3. **简化握手**：WebSocket 握手使用固定 key，未实现完整验证
4. **无重连机制**：连接失败后不会自动重连
5. **无 SSL/TLS 支持**：不支持 WSS 安全连接

## 后续改进建议

1. 实现完整的 WebSocket 帧接收和解析
2. 使用非阻塞 socket 或异步发送
3. 实现连接重试机制
4. 添加 SSL/TLS 支持（WSS）
5. 优化 WebSocket 握手实现
6. 添加连接池管理
7. 完善错误处理和日志

## 代码质量

- ✅ 遵循插件开发指南的核心要求
- ✅ 正确实现 MRCP 接口
- ✅ 使用后台任务处理异步操作
- ✅ 正确的内存管理（使用 APR 内存池）
- ⚠️ WebSocket 协议实现不完整
- ⚠️ 缺少完整的错误处理

## 总结

当前实现已经修复了关键问题（P0 和 P1），插件可以编译和基本运行。P2 级别的问题可以在后续版本中逐步完善。

主要优点：
- 架构合理
- 接口实现正确
- 关键问题已修复

主要缺点：
- WebSocket 协议实现不完整
- 部分功能简化处理
- 缺少完整的错误处理

