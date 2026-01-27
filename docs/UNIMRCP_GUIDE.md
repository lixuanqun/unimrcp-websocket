# UniMRCP 配置与插件开发指南

本文档基于 UniMRCP 官方文档和项目源码，总结模块配置方式和插件开发规范。

## 目录

1. [项目概述](#1-项目概述)
2. [系统架构](#2-系统架构)
3. [安装与构建](#3-安装与构建)
4. [服务器配置](#4-服务器配置)
5. [插件开发指南](#5-插件开发指南)
6. [API 参考](#6-api-参考)

---

## 1. 项目概述

UniMRCP 是一个开源的 MRCP (Media Resource Control Protocol) 实现，符合以下标准：

- **MRCPv2**: IETF RFC 6787
- **MRCPv1**: IETF RFC 4463

### 支持的资源类型

| 资源 | 说明 | 资源标识符 |
|------|------|-----------|
| speechsynth | 语音合成 (TTS) | `MRCP_SYNTHESIZER_RESOURCE` |
| speechrecog | 语音识别 (ASR) | `MRCP_RECOGNIZER_RESOURCE` |
| recorder | 录音 | `MRCP_RECORDER_RESOURCE` |
| speakverify | 说话人验证 (SVI) | `MRCP_VERIFIER_RESOURCE` |

### 依赖库

| 库 | 版本要求 | 用途 |
|-----|---------|------|
| APR | >= 1.2.x | Apache 可移植运行时 |
| APR-util | >= 1.2.x | APR 工具库 |
| Sofia-SIP | >= 1.12.6 | SIP/SDP 协议栈 |

---

## 2. 系统架构

### 2.1 整体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                      UniMRCP Server                              │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │   Profiles  │  │   Settings  │  │      Components         │  │
│  │  (uni1/2)   │  │ (RTP设置)   │  │                         │  │
│  └─────────────┘  └─────────────┘  │  ┌─────────────────┐   │  │
│                                     │  │ Resource Factory │   │  │
│                                     │  └─────────────────┘   │  │
│  ┌──────────────────────────────────┤  ┌─────────────────┐   │  │
│  │     Signaling Layer              │  │  Plugin Factory  │   │  │
│  │  ┌────────────┐ ┌────────────┐  │  │  ┌───────────┐  │   │  │
│  │  │  SIP-UAS   │ │  RTSP-UAS  │  │  │  │  Engine   │  │   │  │
│  │  │ (MRCPv2)   │ │  (MRCPv1)  │  │  │  │ (Plugin)  │  │   │  │
│  │  └────────────┘ └────────────┘  │  │  └───────────┘  │   │  │
│  └──────────────────────────────────┤  └─────────────────┘   │  │
│                                     │  ┌─────────────────┐   │  │
│  ┌──────────────────────────────────┤  │  Media Engine   │   │  │
│  │     Transport Layer              │  └─────────────────┘   │  │
│  │  ┌────────────┐ ┌────────────┐  │  ┌─────────────────┐   │  │
│  │  │ MRCPv2-UAS │ │RTP Factory │  │  │                 │   │  │
│  │  │ (TCP连接)  │ │ (RTP终端)  │  │  └─────────────────┘   │  │
│  │  └────────────┘ └────────────┘  │                         │  │
│  └──────────────────────────────────┴─────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 插件架构

```
Plugin Factory
    └── Engine (插件主体)
        ├── destroy()
        ├── open()
        ├── close()
        └── create_channel()
            └── Channel (会话通道)
                ├── destroy()
                ├── open()
                ├── close()
                └── process_request()
                    └── Audio Stream (音频流)
                        ├── Source Stream (TTS 输出)
                        │   └── read() → 输出音频帧
                        └── Sink Stream (ASR 输入)
                            └── write() ← 接收音频帧
```

---

## 3. 安装与构建

### 3.1 GNU/Linux 构建

```bash
# 1. 安装依赖
sudo apt-get install build-essential autoconf automake libtool pkg-config
sudo apt-get install libapr1-dev libaprutil1-dev

# 2. 编译 Sofia-SIP (如未安装)
wget https://github.com/freeswitch/sofia-sip/archive/refs/tags/v1.13.6.tar.gz
tar xzf v1.13.6.tar.gz && cd sofia-sip-1.13.6
./bootstrap.sh && ./configure && make && sudo make install

# 3. 编译 UniMRCP
./bootstrap
./configure --with-sofia-sip=/usr/local
make
sudo make install
```

### 3.2 CMake 构建

```bash
mkdir build && cd build
cmake .. \
    -DCMAKE_INSTALL_PREFIX=/usr/local/unimrcp \
    -DENABLE_WEBSOCKETSYNTH_PLUGIN=ON \
    -DENABLE_WEBSOCKETRECOG_PLUGIN=ON
make -j$(nproc)
sudo make install
```

### 3.3 安装目录结构

```
/usr/local/unimrcp/
├── bin/          # 可执行文件
├── conf/         # 配置文件
├── data/         # 数据文件
├── include/      # 头文件
├── lib/          # 库文件
├── log/          # 日志文件
├── plugin/       # 插件 (.so/.dll)
└── var/          # 运行时数据
```

---

## 4. 服务器配置

### 4.1 配置文件结构

主配置文件: `conf/unimrcpserver.xml`

```xml
<?xml version="1.0" encoding="UTF-8"?>
<unimrcpserver version="1.0">
  <properties>...</properties>      <!-- 全局属性 -->
  <components>...</components>      <!-- 组件配置 -->
  <settings>...</settings>          <!-- 设置 -->
  <profiles>...</profiles>          <!-- 协议配置文件 -->
</unimrcpserver>
```

### 4.2 全局属性 (Properties)

```xml
<properties>
  <!-- IP 地址配置 -->
  <ip type="auto"/>              <!-- 自动检测 -->
  <!-- <ip type="iface">eth0</ip> -->  <!-- 指定网卡 -->
  <!-- <ip>192.168.1.100</ip> -->      <!-- 显式指定 -->
  
  <!-- 外部 IP (NAT 环境) -->
  <!-- <ext-ip>公网IP</ext-ip> -->
</properties>
```

### 4.3 组件配置 (Components)

#### 4.3.1 资源工厂

```xml
<resource-factory>
  <resource id="speechsynth" enable="true"/>   <!-- TTS -->
  <resource id="speechrecog" enable="true"/>   <!-- ASR -->
  <resource id="recorder" enable="true"/>      <!-- 录音 -->
  <resource id="speakverify" enable="true"/>   <!-- 说话人验证 -->
</resource-factory>
```

#### 4.3.2 SIP 信令代理 (MRCPv2)

```xml
<sip-uas id="SIP-Agent-1" type="SofiaSIP">
  <sip-port>8060</sip-port>
  <sip-transport>udp,tcp</sip-transport>
  <ua-name>UniMRCP SofiaSIP</ua-name>
  <sdp-origin>UniMRCPServer</sdp-origin>
  <sip-session-expires>600</sip-session-expires>
  <sip-min-session-expires>120</sip-min-session-expires>
  <!-- 可选配置 -->
  <!-- <sip-ip>10.10.0.1</sip-ip> -->
  <!-- <force-destination>true</force-destination> -->
  <!-- <sip-message-dump>sofia-sip.log</sip-message-dump> -->
</sip-uas>
```

#### 4.3.3 RTSP 信令代理 (MRCPv1)

```xml
<rtsp-uas id="RTSP-Agent-1" type="UniRTSP">
  <rtsp-port>1554</rtsp-port>
  <resource-map>
    <param name="speechsynth" value="speechsynthesizer"/>
    <param name="speechrecog" value="speechrecognizer"/>
  </resource-map>
  <max-connection-count>100</max-connection-count>
  <inactivity-timeout>600</inactivity-timeout>
</rtsp-uas>
```

#### 4.3.4 MRCPv2 连接代理

```xml
<mrcpv2-uas id="MRCPv2-Agent-1">
  <mrcp-port>1544</mrcp-port>
  <max-connection-count>100</max-connection-count>
  <max-shared-use-count>100</max-shared-use-count>
  <force-new-connection>false</force-new-connection>
  <rx-buffer-size>1024</rx-buffer-size>
  <tx-buffer-size>1024</tx-buffer-size>
  <inactivity-timeout>600</inactivity-timeout>
  <termination-timeout>3</termination-timeout>
</mrcpv2-uas>
```

#### 4.3.5 媒体引擎

```xml
<media-engine id="Media-Engine-1">
  <realtime-rate>1</realtime-rate>  <!-- 实时处理倍率 -->
</media-engine>
```

#### 4.3.6 RTP 工厂

```xml
<rtp-factory id="RTP-Factory-1">
  <rtp-port-min>5000</rtp-port-min>
  <rtp-port-max>6000</rtp-port-max>
  <!-- <rtp-ip>10.10.0.1</rtp-ip> -->
  <!-- <rtp-ext-ip>公网IP</rtp-ext-ip> -->
</rtp-factory>
```

#### 4.3.7 插件工厂 (最重要)

```xml
<plugin-factory>
  <!-- 演示 TTS 插件 -->
  <engine id="Demo-Synth-1" name="demosynth" enable="true"/>
  
  <!-- 演示 ASR 插件 -->
  <engine id="Demo-Recog-1" name="demorecog" enable="true"/>
  
  <!-- WebSocket TTS 插件 (带参数) -->
  <engine id="WebSocket-Synth-1" name="websocketsynth" enable="true">
    <max-channel-count>100</max-channel-count>
    <param name="ws-host" value="localhost"/>
    <param name="ws-port" value="8080"/>
    <param name="ws-path" value="/tts"/>
  </engine>
  
  <!-- WebSocket ASR 插件 -->
  <engine id="WebSocket-Recog-1" name="websocketrecog" enable="true">
    <max-channel-count>100</max-channel-count>
    <param name="ws-host" value="localhost"/>
    <param name="ws-port" value="8080"/>
    <param name="ws-path" value="/asr"/>
  </engine>
</plugin-factory>
```

**插件配置属性说明:**

| 属性 | 说明 |
|------|------|
| `id` | 引擎唯一标识符 |
| `name` | 插件文件名 (不含 .so/.dll 后缀) |
| `enable` | 是否启用 |
| `max-channel-count` | 最大并发通道数 |
| `param` | 自定义参数 (name/value 对) |

### 4.4 设置 (Settings)

#### RTP/RTCP 设置

```xml
<settings>
  <rtp-settings id="RTP-Settings-1">
    <!-- 抖动缓冲区 -->
    <jitter-buffer>
      <adaptive>1</adaptive>
      <playout-delay>50</playout-delay>
      <max-playout-delay>600</max-playout-delay>
      <time-skew-detection>1</time-skew-detection>
    </jitter-buffer>
    
    <!-- 分组时间 (毫秒) -->
    <ptime>20</ptime>
    
    <!-- 支持的编解码器 -->
    <codecs own-preference="false">
      PCMU PCMA G722 L16/96/8000 telephone-event/101/8000
    </codecs>
    
    <!-- RTCP 配置 -->
    <rtcp enable="false">
      <rtcp-bye>1</rtcp-bye>
      <tx-interval>5000</tx-interval>
      <rx-resolution>1000</rx-resolution>
    </rtcp>
  </rtp-settings>
</settings>
```

### 4.5 配置文件 (Profiles)

#### MRCPv2 配置文件

```xml
<profiles>
  <mrcpv2-profile id="uni2">
    <sip-uas>SIP-Agent-1</sip-uas>
    <mrcpv2-uas>MRCPv2-Agent-1</mrcpv2-uas>
    <media-engine>Media-Engine-1</media-engine>
    <rtp-factory>RTP-Factory-1</rtp-factory>
    <rtp-settings>RTP-Settings-1</rtp-settings>
    
    <!-- 可选: 资源到引擎的映射 -->
    <resource-engine-map>
      <resource id="speechsynth" engine="WebSocket-Synth-1"/>
      <resource id="speechrecog" engine="WebSocket-Recog-1">
        <attrib name="custom-attr" value="value"/>
      </resource>
    </resource-engine-map>
  </mrcpv2-profile>
  
  <mrcpv1-profile id="uni1">
    <rtsp-uas>RTSP-Agent-1</rtsp-uas>
    <media-engine>Media-Engine-1</media-engine>
    <rtp-factory>RTP-Factory-1</rtp-factory>
    <rtp-settings>RTP-Settings-1</rtp-settings>
  </mrcpv1-profile>
</profiles>
```

### 4.6 日志配置

文件: `conf/logger.xml`

```xml
<?xml version="1.0" encoding="UTF-8"?>
<aptlogger>
  <!-- 输出模式: CONSOLE, FILE, SYSLOG, CONSOLE,FILE -->
  <output>CONSOLE,FILE</output>
  
  <!-- 日志格式 -->
  <headers>DATE,TIME,PRIORITY,MARK</headers>
  
  <!-- 全局日志级别 -->
  <priority>INFO</priority>
  
  <!-- 日志脱敏模式: NONE, COMPLETE, ENCRYPTED -->
  <masking>NONE</masking>
  
  <!-- 各模块日志级别 -->
  <sources>
    <source name="MPF" priority="INFO" masking="NONE"/>
    <source name="SOFIASIP" priority="INFO" masking="NONE"/>
    <source name="WEBSOCKET-SYNTH-PLUGIN" priority="DEBUG" masking="NONE"/>
    <source name="WEBSOCKET-RECOG-PLUGIN" priority="DEBUG" masking="NONE"/>
  </sources>
</aptlogger>
```

**日志级别:**

| 级别 | 值 | 说明 |
|------|-----|------|
| EMERGENCY | 0 | 系统不可用 |
| ALERT | 1 | 需要立即处理 |
| CRITICAL | 2 | 严重错误 |
| ERROR | 3 | 错误 |
| WARNING | 4 | 警告 |
| NOTICE | 5 | 重要通知 |
| INFO | 6 | 一般信息 |
| DEBUG | 7 | 调试信息 |

---

## 5. 插件开发指南

### 5.1 插件开发必须遵循的五条规则

1. **入口函数**: 必须实现 `mrcp_plugin_create(apr_pool_t *pool)` 函数
2. **版本声明**: 必须使用 `MRCP_PLUGIN_VERSION_DECLARE` 宏
3. **响应规则**: 每个请求必须且只能发送一个响应
4. **非阻塞**: MRCP 引擎通道回调方法不能阻塞
5. **流非阻塞**: MPF 引擎流回调方法不能阻塞

### 5.2 插件目录结构

```
plugins/your-plugin/
├── src/
│   └── your_plugin_engine.c    # 主实现文件
├── include/                    # 头文件 (可选)
├── CMakeLists.txt              # CMake 构建
├── Makefile.am                 # Autotools 构建
└── README.md                   # 文档
```

### 5.3 插件代码模板

#### 5.3.1 基本框架

```c
/* 必需的头文件 */
#include "mrcp_synth_engine.h"  /* TTS 插件 */
/* 或 */
#include "mrcp_recog_engine.h"  /* ASR 插件 */

#include "apt_consumer_task.h"
#include "apt_log.h"

/* 版本声明 (必需) */
MRCP_PLUGIN_VERSION_DECLARE

/* 日志源声明 */
MRCP_PLUGIN_LOG_SOURCE_IMPLEMENT(MY_PLUGIN, "MY-PLUGIN")
#define MY_LOG_MARK APT_LOG_MARK_DECLARE(MY_PLUGIN)

/* 引擎结构体 */
typedef struct my_engine_t {
    apt_consumer_task_t *task;
    /* 自定义字段 */
} my_engine_t;

/* 通道结构体 */
typedef struct my_channel_t {
    my_engine_t           *engine;
    mrcp_engine_channel_t *channel;
    mrcp_message_t        *request;
    /* 自定义字段 */
} my_channel_t;

/* 插件入口函数 (必需) */
MRCP_PLUGIN_DECLARE(mrcp_engine_t*) mrcp_plugin_create(apr_pool_t *pool)
{
    my_engine_t *engine = apr_palloc(pool, sizeof(my_engine_t));
    
    /* 初始化引擎 */
    /* ... */
    
    return mrcp_engine_create(
        MRCP_SYNTHESIZER_RESOURCE,  /* 或 MRCP_RECOGNIZER_RESOURCE */
        engine,
        &engine_vtable,
        pool
    );
}
```

#### 5.3.2 Engine 方法表

```c
/* 方法声明 */
static apt_bool_t my_engine_destroy(mrcp_engine_t *engine);
static apt_bool_t my_engine_open(mrcp_engine_t *engine);
static apt_bool_t my_engine_close(mrcp_engine_t *engine);
static mrcp_engine_channel_t* my_engine_channel_create(
    mrcp_engine_t *engine, apr_pool_t *pool);

/* 方法表 */
static const struct mrcp_engine_method_vtable_t engine_vtable = {
    my_engine_destroy,
    my_engine_open,
    my_engine_close,
    my_engine_channel_create
};

/* 实现 */
static apt_bool_t my_engine_open(mrcp_engine_t *engine)
{
    /* 初始化资源 */
    return mrcp_engine_open_respond(engine, TRUE);
}

static apt_bool_t my_engine_close(mrcp_engine_t *engine)
{
    /* 清理资源 */
    return mrcp_engine_close_respond(engine);
}
```

#### 5.3.3 Channel 方法表

```c
/* 方法声明 */
static apt_bool_t my_channel_destroy(mrcp_engine_channel_t *channel);
static apt_bool_t my_channel_open(mrcp_engine_channel_t *channel);
static apt_bool_t my_channel_close(mrcp_engine_channel_t *channel);
static apt_bool_t my_channel_request_process(
    mrcp_engine_channel_t *channel, mrcp_message_t *request);

/* 方法表 */
static const struct mrcp_engine_channel_method_vtable_t channel_vtable = {
    my_channel_destroy,
    my_channel_open,
    my_channel_close,
    my_channel_request_process
};
```

#### 5.3.4 Audio Stream 方法表

**TTS 插件 (Source Stream - 输出音频):**

```c
static apt_bool_t my_stream_read(mpf_audio_stream_t *stream, mpf_frame_t *frame);

static const mpf_audio_stream_vtable_t audio_stream_vtable = {
    my_stream_destroy,
    my_stream_open,
    my_stream_close,
    my_stream_read,    /* Source: 输出音频帧 */
    NULL, NULL, NULL, NULL
};
```

**ASR 插件 (Sink Stream - 接收音频):**

```c
static apt_bool_t my_stream_write(mpf_audio_stream_t *stream, const mpf_frame_t *frame);

static const mpf_audio_stream_vtable_t audio_stream_vtable = {
    my_stream_destroy,
    NULL, NULL, NULL,
    my_stream_open,
    my_stream_close,
    my_stream_write,   /* Sink: 接收音频帧 */
    NULL
};
```

#### 5.3.5 创建 Channel 和 Audio Termination

```c
static mrcp_engine_channel_t* my_engine_channel_create(
    mrcp_engine_t *engine, apr_pool_t *pool)
{
    mpf_stream_capabilities_t *capabilities;
    mpf_termination_t *termination;
    my_channel_t *my_channel;
    
    /* 分配通道 */
    my_channel = apr_palloc(pool, sizeof(my_channel_t));
    my_channel->engine = engine->obj;
    
    /* 创建流能力 */
    /* TTS: mpf_source_stream_capabilities_create */
    /* ASR: mpf_sink_stream_capabilities_create */
    capabilities = mpf_source_stream_capabilities_create(pool);
    mpf_codec_capabilities_add(
        &capabilities->codecs,
        MPF_SAMPLE_RATE_8000 | MPF_SAMPLE_RATE_16000,
        "LPCM"
    );
    
    /* 创建媒体终端 */
    termination = mrcp_engine_audio_termination_create(
        my_channel,
        &audio_stream_vtable,
        capabilities,
        pool
    );
    
    /* 创建引擎通道 */
    my_channel->channel = mrcp_engine_channel_create(
        engine,
        &channel_vtable,
        my_channel,
        termination,
        pool
    );
    
    return my_channel->channel;
}
```

### 5.4 异步处理模式

由于回调方法不能阻塞，耗时操作需要使用后台任务:

```c
/* 消息类型 */
typedef enum {
    MSG_OPEN_CHANNEL,
    MSG_CLOSE_CHANNEL,
    MSG_REQUEST_PROCESS
} msg_type_e;

/* 消息结构 */
typedef struct my_msg_t {
    msg_type_e             type;
    mrcp_engine_channel_t *channel;
    mrcp_message_t        *request;
} my_msg_t;

/* 发送消息到后台任务 */
static apt_bool_t my_msg_signal(
    msg_type_e type,
    mrcp_engine_channel_t *channel,
    mrcp_message_t *request)
{
    my_channel_t *my_channel = channel->method_obj;
    apt_task_t *task = apt_consumer_task_base_get(my_channel->engine->task);
    apt_task_msg_t *msg = apt_task_msg_get(task);
    
    if (msg) {
        my_msg_t *my_msg = (my_msg_t*)msg->data;
        my_msg->type = type;
        my_msg->channel = channel;
        my_msg->request = request;
        return apt_task_msg_signal(task, msg);
    }
    return FALSE;
}

/* 后台任务处理 */
static apt_bool_t my_msg_process(apt_task_t *task, apt_task_msg_t *msg)
{
    my_msg_t *my_msg = (my_msg_t*)msg->data;
    
    switch (my_msg->type) {
        case MSG_OPEN_CHANNEL:
            /* 执行打开操作 */
            mrcp_engine_channel_open_respond(my_msg->channel, TRUE);
            break;
        case MSG_CLOSE_CHANNEL:
            /* 执行关闭操作 */
            mrcp_engine_channel_close_respond(my_msg->channel);
            break;
        case MSG_REQUEST_PROCESS:
            /* 处理 MRCP 请求 */
            my_request_dispatch(my_msg->channel, my_msg->request);
            break;
    }
    return TRUE;
}
```

### 5.5 发送响应和事件

```c
/* 发送响应 */
mrcp_message_t *response = mrcp_response_create(request, request->pool);
response->start_line.request_state = MRCP_REQUEST_STATE_INPROGRESS;
mrcp_engine_channel_message_send(channel, response);

/* 发送事件 (如 SPEAK-COMPLETE) */
mrcp_message_t *event = mrcp_event_create(
    request,
    SYNTHESIZER_SPEAK_COMPLETE,
    request->pool
);

mrcp_synth_header_t *synth_header = mrcp_resource_header_prepare(event);
if (synth_header) {
    synth_header->completion_cause = SYNTHESIZER_COMPLETION_CAUSE_NORMAL;
    mrcp_resource_header_property_add(event, SYNTHESIZER_HEADER_COMPLETION_CAUSE);
}
event->start_line.request_state = MRCP_REQUEST_STATE_COMPLETE;

mrcp_engine_channel_message_send(channel, event);
```

### 5.6 获取配置参数

```c
/* 在 create_channel 中获取配置参数 */
const char *param1 = mrcp_engine_param_get(engine, "param-name");
if (param1) {
    apt_log(MY_LOG_MARK, APT_PRIO_INFO, "Param: %s", param1);
}
```

### 5.7 构建配置

#### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 2.8)
project(myplugin)

set(PLUGIN_SOURCES src/my_plugin_engine.c)

add_library(${PROJECT_NAME} MODULE ${PLUGIN_SOURCES}
    $<TARGET_OBJECTS:mrcpengine>
    $<TARGET_OBJECTS:mrcp>
    $<TARGET_OBJECTS:mpf>
    $<TARGET_OBJECTS:aprtoolkit>
)

target_link_libraries(${PROJECT_NAME}
    ${APU_LIBRARIES}
    ${APR_LIBRARIES}
)

include_directories(
    ${MRCP_ENGINE_INCLUDE_DIRS}
    ${MRCP_INCLUDE_DIRS}
    ${MPF_INCLUDE_DIRS}
    ${APR_TOOLKIT_INCLUDE_DIRS}
    ${APR_INCLUDE_DIRS}
)

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION plugin)
```

#### Makefile.am

```makefile
AM_CPPFLAGS          = $(UNIMRCP_PLUGIN_INCLUDES)
plugin_LTLIBRARIES   = myplugin.la
myplugin_la_SOURCES  = src/my_plugin_engine.c
myplugin_la_LDFLAGS  = $(UNIMRCP_PLUGIN_OPTS)
include $(top_srcdir)/build/rules/uniplugin.am
```

---

## 6. API 参考

### 6.1 Engine 创建 API

| 函数 | 说明 |
|------|------|
| `mrcp_engine_create()` | 创建 MRCP 引擎 |
| `mrcp_engine_open_respond()` | 发送引擎打开响应 |
| `mrcp_engine_close_respond()` | 发送引擎关闭响应 |
| `mrcp_engine_param_get()` | 获取引擎配置参数 |
| `mrcp_engine_config_get()` | 获取引擎配置 |

### 6.2 Channel 创建 API

| 函数 | 说明 |
|------|------|
| `mrcp_engine_channel_create()` | 创建引擎通道 |
| `mrcp_engine_audio_termination_create()` | 创建音频终端 |
| `mrcp_engine_channel_open_respond()` | 发送通道打开响应 |
| `mrcp_engine_channel_close_respond()` | 发送通道关闭响应 |
| `mrcp_engine_channel_message_send()` | 发送消息 (响应/事件) |

### 6.3 Message API

| 函数 | 说明 |
|------|------|
| `mrcp_response_create()` | 创建响应消息 |
| `mrcp_event_create()` | 创建事件消息 |
| `mrcp_resource_header_get()` | 获取资源头 |
| `mrcp_resource_header_prepare()` | 准备资源头 |
| `mrcp_resource_header_property_add()` | 添加头属性 |
| `mrcp_generic_header_get()` | 获取通用头 |
| `mrcp_generic_header_prepare()` | 准备通用头 |

### 6.4 Stream Capabilities API

| 函数 | 说明 |
|------|------|
| `mpf_source_stream_capabilities_create()` | 创建源流能力 (TTS) |
| `mpf_sink_stream_capabilities_create()` | 创建接收流能力 (ASR) |
| `mpf_codec_capabilities_add()` | 添加编解码器能力 |

### 6.5 采样率常量

| 常量 | 值 |
|------|-----|
| `MPF_SAMPLE_RATE_8000` | 8000 Hz |
| `MPF_SAMPLE_RATE_16000` | 16000 Hz |
| `MPF_SAMPLE_RATE_32000` | 32000 Hz |
| `MPF_SAMPLE_RATE_48000` | 48000 Hz |

### 6.6 请求状态

| 常量 | 说明 |
|------|------|
| `MRCP_REQUEST_STATE_PENDING` | 待处理 |
| `MRCP_REQUEST_STATE_INPROGRESS` | 处理中 |
| `MRCP_REQUEST_STATE_COMPLETE` | 已完成 |

---

## 附录: 端口配置汇总

| 服务 | 默认端口 | 配置项 |
|------|----------|--------|
| SIP (MRCPv2 信令) | 8060 | `sip-port` |
| RTSP (MRCPv1 信令) | 1554 | `rtsp-port` |
| MRCP (MRCPv2 连接) | 1544 | `mrcp-port` |
| RTP (媒体流) | 5000-6000 | `rtp-port-min`, `rtp-port-max` |

---

**文档版本**: 1.0  
**最后更新**: 2024
