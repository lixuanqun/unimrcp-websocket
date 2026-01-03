# UniMRCP WebSocket 插件 - WSL 环境编译和部署指南

## 目录

1. [环境准备](#环境准备)
2. [依赖安装](#依赖安装)
3. [源码编译](#源码编译)
4. [安装部署](#安装部署)
5. [配置文件](#配置文件)
6. [启动服务](#启动服务)
7. [测试验证](#测试验证)
8. [故障排除](#故障排除)

---

## 环境准备

### 系统要求

- **操作系统**: WSL (Windows Subsystem for Linux) - Ubuntu/CentOS/Debian
- **WSL 版本**: WSL 2 推荐
- **磁盘空间**: 至少 500MB 可用空间

### 检查 WSL 环境

```bash
# 检查 WSL 版本
wsl --list --verbose

# 检查 Linux 发行版
cat /etc/os-release

# 检查系统架构
uname -m
```

### 必要工具

确保已安装以下基本工具：

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y build-essential git wget curl

# CentOS/RHEL
sudo yum install -y gcc gcc-c++ make git wget curl
sudo yum groupinstall -y "Development Tools"
```

---

## 依赖安装

### 1. 构建工具

#### Ubuntu/Debian
```bash
sudo apt-get install -y \
    autoconf \
    automake \
    libtool \
    pkg-config \
    gettext \
    m4
```

#### CentOS/RHEL
```bash
sudo yum install -y \
    autoconf \
    automake \
    libtool \
    pkg-config \
    gettext \
    m4
```

### 2. APR (Apache Portable Runtime)

#### Ubuntu/Debian
```bash
sudo apt-get install -y \
    libapr1-dev \
    libaprutil1-dev
```

#### CentOS/RHEL
```bash
sudo yum install -y \
    apr-devel \
    apr-util-devel
```

**验证安装**:
```bash
pkg-config --modversion apr-1
pkg-config --modversion apr-util-1
```

### 3. Sofia-SIP

Sofia-SIP 通常不在标准仓库中，需要从源码编译安装：

```bash
# 创建临时目录
cd /tmp

# 下载 Sofia-SIP 源码
wget https://github.com/freeswitch/sofia-sip/archive/refs/tags/v1.13.15.tar.gz
# 或使用 curl
# curl -L https://github.com/freeswitch/sofia-sip/archive/refs/tags/v1.13.15.tar.gz -o sofia-sip.tar.gz

# 解压
tar -xzf v1.13.15.tar.gz
cd sofia-sip-1.13.15

# 生成配置脚本
./bootstrap.sh

# 配置
./configure --prefix=/usr/local

# 编译（使用 4 个并行任务）
make -j4

# 安装（需要 root 权限）
sudo make install

# 更新动态链接库缓存
sudo ldconfig

# 验证安装
pkg-config --exists libsofia-sip-ua && echo "Sofia-SIP installed successfully" || echo "Sofia-SIP not found"
```

**注意**: 如果 `pkg-config` 找不到 Sofia-SIP，可能需要设置环境变量：
```bash
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
```

---

## 源码编译

### 1. 获取源码

如果源码在 Windows 文件系统中（通过 `/mnt/d/` 访问），直接进入目录：

```bash
cd /mnt/d/git_repo/unimrcp-websocket
```

如果从 Git 仓库克隆：
```bash
git clone <repository-url>
cd unimrcp-websocket
```

### 2. 准备构建环境

```bash
# 进入项目根目录
cd unimrcp-websocket

# 确保脚本有执行权限（修复 Windows 行结束符问题）
find . -name "*.sh" -type f -exec chmod +x {} \;
find . -name "*.sh" -type f -exec sed -i 's/\r$//' {} \;

# 生成配置脚本
./bootstrap
```

**如果遇到 "bad interpreter" 错误**:
```bash
# 修复行结束符
sed -i 's/\r$//' bootstrap
sed -i 's/\r$//' build/get-version.sh
chmod +x bootstrap
chmod +x build/get-version.sh
```

### 3. 配置构建选项

```bash
# 基本配置（启用 WebSocket 插件）
./configure --enable-websocketrecog-plugin

# 完整配置选项示例
./configure \
    --enable-websocketrecog-plugin \
    --disable-server-app \
    --disable-client-app \
    --prefix=/usr/local/unimrcp

# 配置选项说明：
# --enable-websocketrecog-plugin  : 启用 WebSocket Recognizer 插件
# --disable-server-app            : 不构建服务器应用程序（可选）
# --disable-client-app            : 不构建客户端应用程序（可选）
# --prefix=PATH                   : 安装路径（默认：/usr/local/unimrcp）
```

**查看所有配置选项**:
```bash
./configure --help
```

**配置输出检查**:
确保看到以下输出：
```
WebSocket recognizer plugin... : yes
```

### 4. 编译

```bash
# 编译（使用 4 个并行任务，根据 CPU 核心数调整）
make -j4

# 如果遇到错误，使用单线程编译以便查看详细错误信息
# make
```

**编译时间**: 通常需要 5-10 分钟，取决于系统性能

**验证编译结果**:
```bash
# 检查插件库文件
ls -lh plugins/websocket-recog/.libs/websocketrecog.so

# 或
find . -name "websocketrecog.so" -o -name "websocketrecog.la"
```

---

## 安装部署

### 1. 安装到系统

```bash
# 安装（需要 root 权限）
sudo make install

# 安装目录结构（默认：/usr/local/unimrcp）
# /usr/local/unimrcp/
#   ├── bin/           # 可执行文件
#   ├── lib/           # 库文件
#   ├── plugin/        # 插件目录
#   ├── conf/          # 配置文件
#   ├── log/           # 日志目录
#   └── data/          # 数据目录
```

**验证安装**:
```bash
# 检查插件文件
ls -lh /usr/local/unimrcp/plugin/websocketrecog.so

# 检查可执行文件
ls -lh /usr/local/unimrcp/bin/
```

### 2. 设置环境变量（可选）

如果不想使用完整路径运行命令，可以添加到 PATH：

```bash
# 临时设置（当前会话有效）
export PATH=/usr/local/unimrcp/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/unimrcp/lib:$LD_LIBRARY_PATH

# 永久设置（添加到 ~/.bashrc）
echo 'export PATH=/usr/local/unimrcp/bin:$PATH' >> ~/.bashrc
echo 'export LD_LIBRARY_PATH=/usr/local/unimrcp/lib:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc
```

---

## 配置文件

### 1. 服务器配置文件

编辑服务器配置文件：

```bash
# 配置文件路径
/usr/local/unimrcp/conf/unimrcpserver.xml

# 或使用编辑器
sudo nano /usr/local/unimrcp/conf/unimrcpserver.xml
# 或
sudo vim /usr/local/unimrcp/conf/unimrcpserver.xml
```

### 2. 添加 WebSocket 插件配置

在 `<plugin-factory>` 部分添加 WebSocket 插件配置：

```xml
<plugin-factory>
    <!-- 其他插件配置 -->
    <engine id="Demo-Synth-1" name="demosynth" enable="true"/>
    <engine id="Demo-Recog-1" name="demorecog" enable="true"/>
    
    <!-- WebSocket Recognizer 插件配置 -->
    <engine id="WebSocket-Recog-1" name="websocketrecog" enable="true">
        <param name="ws-host" value="localhost"/>
        <param name="ws-port" value="8080"/>
        <param name="ws-path" value="/asr"/>
        <param name="audio-buffer-size" value="32000"/>
    </engine>
</plugin-factory>
```

**配置参数说明**:

| 参数 | 说明 | 默认值 | 示例 |
|------|------|--------|------|
| `ws-host` | WebSocket 服务器主机地址 | `localhost` | `192.168.1.100` |
| `ws-port` | WebSocket 服务器端口 | `8080` | `9000` |
| `ws-path` | WebSocket 连接路径 | `/` | `/asr` |
| `audio-buffer-size` | 音频缓冲区大小（字节） | 根据采样率自动计算 | `32000` |

### 3. 日志配置（可选）

在 `logger.xml` 中配置插件日志级别：

```xml
<source name="WEBSOCKET-RECOG-PLUGIN" priority="DEBUG" masking="NONE"/>
```

**日志级别**: `DEBUG`, `INFO`, `NOTICE`, `WARNING`, `ERROR`

### 4. 验证配置文件

```bash
# 检查 XML 语法（如果安装了 xmllint）
xmllint --noout /usr/local/unimrcp/conf/unimrcpserver.xml

# 或手动检查配置文件格式
cat /usr/local/unimrcp/conf/unimrcpserver.xml | grep -A 5 "websocketrecog"
```

---

## 启动服务

### 1. 创建系统服务（systemd，可选）

创建服务文件：

```bash
sudo nano /etc/systemd/system/unimrcpserver.service
```

服务文件内容：

```ini
[Unit]
Description=UniMRCP Server
After=network.target

[Service]
Type=simple
User=root
WorkingDirectory=/usr/local/unimrcp
ExecStart=/usr/local/unimrcp/bin/unimrcpserver
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

启用和启动服务：

```bash
# 重新加载 systemd
sudo systemctl daemon-reload

# 启用服务（开机自启）
sudo systemctl enable unimrcpserver

# 启动服务
sudo systemctl start unimrcpserver

# 查看状态
sudo systemctl status unimrcpserver

# 查看日志
sudo journalctl -u unimrcpserver -f
```

### 2. 手动启动

```bash
# 前台运行（用于调试）
/usr/local/unimrcp/bin/unimrcpserver

# 后台运行
/usr/local/unimrcp/bin/unimrcpserver &

# 使用 nohup 运行
nohup /usr/local/unimrcp/bin/unimrcpserver > /tmp/unimrcp.log 2>&1 &
```

### 3. 检查服务状态

```bash
# 检查进程
ps aux | grep unimrcpserver

# 检查端口监听
netstat -tlnp | grep unimrcp
# 或
ss -tlnp | grep unimrcp

# 查看日志
tail -f /usr/local/unimrcp/log/unimrcpserver.log
```

---

## 测试验证

### 1. 验证插件加载

查看服务器日志，确认插件加载成功：

```bash
grep -i "websocket\|websocketrecog" /usr/local/unimrcp/log/unimrcpserver.log
```

应该看到类似输出：
```
[INFO] WebSocket Recognizer plugin loaded
[INFO] WebSocket Connected to [localhost:8080/asr]
```

### 2. 功能测试

#### 测试 WebSocket 服务器连接

使用 `wscat` 或 `websocat` 测试 WebSocket 连接：

```bash
# 安装 wscat（Node.js 工具）
npm install -g wscat

# 测试连接
wscat -c ws://localhost:8080/asr
```

#### 测试 MRCP 客户端连接

参考插件示例代码进行测试：

```bash
# 查看示例代码
ls -lh plugins/websocket-recog/examples/
```

---

## 故障排除

### 1. 编译错误

#### 错误: "APR not found"
```bash
# 安装 APR 开发包
sudo apt-get install libapr1-dev libaprutil1-dev  # Ubuntu/Debian
sudo yum install apr-devel apr-util-devel        # CentOS/RHEL
```

#### 错误: "Sofia-SIP not found"
```bash
# 编译安装 Sofia-SIP（参考依赖安装章节）
# 确保 PKG_CONFIG_PATH 包含 /usr/local/lib/pkgconfig
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
```

#### 错误: "bad interpreter: No such file or directory"
```bash
# 修复行结束符
find . -name "*.sh" -exec sed -i 's/\r$//' {} \;
find . -name "*.sh" -exec chmod +x {} \;
```

#### 错误: "cannot find input file: `.in'"
```bash
# 清理并重新生成配置
rm -f config.status config.log
./bootstrap
./configure --enable-websocketrecog-plugin
```

### 2. 运行时错误

#### 插件未加载
- 检查插件文件是否存在: `ls -lh /usr/local/unimrcp/plugin/websocketrecog.so`
- 检查配置文件中的插件配置是否正确
- 查看日志文件获取详细错误信息

#### WebSocket 连接失败
- 检查 WebSocket 服务器是否运行
- 检查防火墙设置
- 检查配置参数（host, port, path）
- 查看日志文件

#### 权限错误
```bash
# 确保有执行权限
chmod +x /usr/local/unimrcp/bin/unimrcpserver

# 确保日志目录可写
sudo chmod 755 /usr/local/unimrcp/log
```

### 3. 调试技巧

#### 启用详细日志
在 `logger.xml` 中设置日志级别为 `DEBUG`:

```xml
<source name="WEBSOCKET-RECOG-PLUGIN" priority="DEBUG" masking="NONE"/>
```

#### 使用 gdb 调试
```bash
# 编译时保留调试信息
./configure --enable-debug CFLAGS="-g -O0"

# 使用 gdb 运行
gdb /usr/local/unimrcp/bin/unimrcpserver
```

#### 查看共享库依赖
```bash
# 检查插件依赖
ldd /usr/local/unimrcp/plugin/websocketrecog.so

# 检查缺失的库
ldd /usr/local/unimrcp/plugin/websocketrecog.so | grep "not found"
```

---

## 快速参考

### 常用命令

```bash
# 编译
./bootstrap && ./configure --enable-websocketrecog-plugin && make -j4

# 安装
sudo make install

# 启动
/usr/local/unimrcp/bin/unimrcpserver

# 查看日志
tail -f /usr/local/unimrcp/log/unimrcpserver.log

# 检查状态
ps aux | grep unimrcpserver
```

### 重要路径

| 路径 | 说明 |
|------|------|
| `/usr/local/unimrcp/bin/unimrcpserver` | 服务器可执行文件 |
| `/usr/local/unimrcp/plugin/websocketrecog.so` | WebSocket 插件库 |
| `/usr/local/unimrcp/conf/unimrcpserver.xml` | 服务器配置文件 |
| `/usr/local/unimrcp/log/unimrcpserver.log` | 服务器日志文件 |

---

## 附录

### A. 完整安装脚本示例

```bash
#!/bin/bash
set -e

echo "=== UniMRCP WebSocket Plugin Installation Script ==="

# 1. 安装依赖
echo "Installing dependencies..."
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    autoconf automake libtool pkg-config \
    libapr1-dev libaprutil1-dev \
    git wget

# 2. 安装 Sofia-SIP
echo "Installing Sofia-SIP..."
cd /tmp
wget https://github.com/freeswitch/sofia-sip/archive/refs/tags/v1.13.15.tar.gz
tar -xzf v1.13.15.tar.gz
cd sofia-sip-1.13.15
./bootstrap.sh
./configure --prefix=/usr/local
make -j4
sudo make install
sudo ldconfig

# 3. 编译 UniMRCP
echo "Compiling UniMRCP..."
cd /mnt/d/git_repo/unimrcp-websocket
find . -name "*.sh" -exec sed -i 's/\r$//' {} \;
find . -name "*.sh" -exec chmod +x {} \;
./bootstrap
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
./configure --enable-websocketrecog-plugin
make -j4

# 4. 安装
echo "Installing..."
sudo make install

echo "=== Installation completed ==="
```

### B. 卸载

```bash
# 停止服务
sudo systemctl stop unimrcpserver  # 如果使用 systemd
# 或
killall unimrcpserver

# 删除安装文件
sudo rm -rf /usr/local/unimrcp

# 删除系统服务（如果使用 systemd）
sudo systemctl disable unimrcpserver
sudo rm /etc/systemd/system/unimrcpserver.service
sudo systemctl daemon-reload
```

---

## 相关文档

- [插件开发指南](docs/PLUGIN_DEVELOPMENT_GUIDE.md)
- [WebSocket 插件说明](plugins/websocket-recog/README.md)
- [编译注意事项](plugins/websocket-recog/COMPILE_NOTES.md)
- [P1/P2 改进说明](plugins/websocket-recog/P1_P2_COMPLETION_SUMMARY.md)

---

**文档版本**: 1.0  
**最后更新**: 2024-01-03  
**维护者**: UniMRCP WebSocket Plugin Development Team

