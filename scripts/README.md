# UniMRCP 服务管理脚本

本目录包含用于管理 UniMRCP Server 的便捷脚本。

## 脚本列表

| 脚本 | 说明 |
|------|------|
| `start.sh` | 启动 UniMRCP Server |
| `stop.sh` | 停止 UniMRCP Server |
| `restart.sh` | 重启 UniMRCP Server |
| `status.sh` | 查看服务状态 |
| `install.sh` | 安装服务到系统目录 |

## 快速使用

### 启动服务

```bash
# 后台守护进程模式启动
./start.sh

# 前台运行（用于调试）
./start.sh -f

# 调试模式启动（详细日志）
./start.sh -d

# 指定根目录
./start.sh -r /path/to/unimrcp
```

### 停止服务

```bash
# 优雅停止
./stop.sh

# 强制停止
./stop.sh -f
```

### 重启服务

```bash
# 普通重启
./restart.sh

# 强制重启
./restart.sh --force

# 重启后前台运行
./restart.sh -f
```

### 查看状态

```bash
# 简单状态
./status.sh

# 详细信息
./status.sh -v
```

## 安装到系统

```bash
# 安装到默认目录 /usr/local/unimrcp
sudo ./install.sh

# 安装到自定义目录
sudo ./install.sh -p /opt/unimrcp
```

## 目录结构

安装后的目录结构：

```
/usr/local/unimrcp/
├── bin/                    # 可执行文件和管理脚本
│   ├── unimrcpserver       # 服务器程序
│   ├── start.sh
│   ├── stop.sh
│   ├── restart.sh
│   └── status.sh
├── conf/                   # 配置文件
│   ├── unimrcpserver.xml   # 主配置文件
│   └── logger.xml          # 日志配置
├── plugin/                 # 插件目录
│   ├── demosynth.so
│   ├── demorecog.so
│   ├── websocketsynth.so
│   └── ...
├── log/                    # 日志目录
├── var/                    # 运行时数据（PID 文件等）
└── data/                   # 数据文件
```

## 配置说明

### 服务器配置

主配置文件：`conf/unimrcpserver.xml`

```xml
<!-- WebSocket TTS 插件配置示例 -->
<engine id="WebSocket-Synth-1" name="websocketsynth" enable="true">
  <max-channel-count>100</max-channel-count>
  <param name="ws-host" value="localhost"/>
  <param name="ws-port" value="8080"/>
  <param name="ws-path" value="/tts"/>
</engine>
```

### 日志配置

日志配置文件：`conf/logger.xml`

```xml
<!-- 启用 WebSocket TTS 插件日志 -->
<source name="WEBSOCKET-SYNTH-PLUGIN" priority="DEBUG" masking="NONE"/>
```

## 常见问题

### Q: 服务启动失败

1. 检查端口是否被占用：
   ```bash
   netstat -tlnp | grep 8060
   ```

2. 检查日志文件：
   ```bash
   tail -100 /usr/local/unimrcp/log/unimrcpserver*.log
   ```

3. 以前台模式启动查看错误：
   ```bash
   ./start.sh -f
   ```

### Q: 找不到 unimrcpserver

确保已编译项目：

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Q: 权限问题

确保脚本有执行权限：

```bash
chmod +x scripts/*.sh
```

## 系统服务集成

### systemd 服务文件

创建 `/etc/systemd/system/unimrcp.service`:

```ini
[Unit]
Description=UniMRCP Server
After=network.target

[Service]
Type=forking
PIDFile=/usr/local/unimrcp/var/unimrcpserver.pid
ExecStart=/usr/local/unimrcp/bin/start.sh
ExecStop=/usr/local/unimrcp/bin/stop.sh
ExecReload=/usr/local/unimrcp/bin/restart.sh
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

启用服务：

```bash
sudo systemctl daemon-reload
sudo systemctl enable unimrcp
sudo systemctl start unimrcp
```

### SysVinit 服务脚本

创建 `/etc/init.d/unimrcp`:

```bash
#!/bin/bash
### BEGIN INIT INFO
# Provides:          unimrcp
# Required-Start:    $network $remote_fs
# Required-Stop:     $network $remote_fs
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Description:       UniMRCP Server
### END INIT INFO

UNIMRCP_DIR=/usr/local/unimrcp

case "$1" in
    start)
        $UNIMRCP_DIR/bin/start.sh
        ;;
    stop)
        $UNIMRCP_DIR/bin/stop.sh
        ;;
    restart)
        $UNIMRCP_DIR/bin/restart.sh
        ;;
    status)
        $UNIMRCP_DIR/bin/status.sh
        ;;
    *)
        echo "Usage: $0 {start|stop|restart|status}"
        exit 1
        ;;
esac

exit 0
```
