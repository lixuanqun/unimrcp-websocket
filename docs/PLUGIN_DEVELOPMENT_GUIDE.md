# UniMRCP 插件开发指南

## 目录

1. [概述](#概述)
2. [插件架构](#插件架构)
3. [开发环境准备](#开发环境准备)
4. [插件开发步骤](#插件开发步骤)
5. [核心接口实现](#核心接口实现)
6. [音频流处理](#音频流处理)
7. [消息处理](#消息处理)
8. [配置和构建](#配置和构建)
9. [调试和测试](#调试和测试)
10. [最佳实践](#最佳实践)
11. [常见问题](#常见问题)

## 概述

UniMRCP 插件是一个动态共享库（DSO），实现了 MRCP 引擎接口，用于提供语音识别、语音合成、录音或说话人验证等功能。插件在运行时由 UniMRCP 服务器加载和管理。

### 插件类型

UniMRCP 支持四种类型的插件：

- **Synthesizer (TTS)**: 语音合成插件，实现 `MRCP_SYNTHESIZER_RESOURCE`
- **Recognizer (ASR)**: 语音识别插件，实现 `MRCP_RECOGNIZER_RESOURCE`
- **Recorder**: 录音插件，实现 `MRCP_RECORDER_RESOURCE`
- **Verifier (SVI)**: 说话人验证插件，实现 `MRCP_VERIFIER_RESOURCE`

### 插件开发必须遵循的规则

1. **入口函数**: 每个插件必须实现 `mrcp_plugin_create` 函数作为主入口点
2. **版本声明**: 每个插件必须声明版本号 `MRCP_PLUGIN_VERSION_DECLARE`
3. **响应规则**: 每个请求必须发送且仅发送一个响应
4. **非阻塞**: MRCP 引擎通道的回调方法不能阻塞（异步响应可以从其他线程发送）
5. **流非阻塞**: MPF 引擎流的回调方法不能阻塞

## 插件架构

### 插件层次结构

```
UniMRCP Server
    └── Plugin Factory
        └── Engine (插件)
            └── Channel (通道，每个会话一个)
                └── Audio Stream (音频流)
```

### 关键组件

#### 1. Engine (引擎)

引擎是插件的顶层对象，代表整个插件实例。每个插件在配置文件中可以配置多个引擎实例（通过不同的 id）。

**职责**:
- 管理插件的生命周期（创建、打开、关闭、销毁）
- 创建和管理 Channel
- 管理共享资源（如线程池、连接池等）

#### 2. Channel (通道)

通道代表一个 MRCP 会话。每个通道与一个 MRCP 客户端会话相关联。

**职责**:
- 处理 MRCP 请求（如 RECOGNIZE、SPEAK 等）
- 管理会话状态
- 关联音频流处理

#### 3. Audio Stream (音频流)

音频流处理实际的音频数据（RTP 流）。

**类型**:
- **Source Stream**: 输出音频流（TTS 使用）
- **Sink Stream**: 输入音频流（ASR、Recorder 使用）

## 开发环境准备

### 必需的依赖库

1. **APR (Apache Portable Runtime)**: >= 1.2.x
2. **APR-util**: >= 1.2.x
3. **Sofia-SIP**: >= 1.12.6（仅服务器需要）

### 构建工具

- **autoconf**: >= 2.59
- **automake**
- **libtool**: >= 1.4
- **gcc** 或其他 C 编译器
- **pkg-config**

### 头文件路径

插件开发需要包含以下头文件目录：

```
libs/mrcp-engine/include
libs/mrcp/include
libs/mrcp/message/include
libs/mrcp/resources/include
libs/mpf/include
libs/apr-toolkit/include
```

## 插件开发步骤

### 步骤 1: 创建插件目录结构

```
plugins/your-plugin/
├── src/
│   └── your_plugin_engine.c    # 插件主实现文件
├── CMakeLists.txt               # CMake 构建文件
├── Makefile.am                  # Autotools 构建文件
└── README.md                    # 插件文档（可选）
```

### 步骤 2: 实现插件入口函数

```c
#include "mrcp_engine_plugin.h"
#include "mrcp_synth_engine.h"  // 或 mrcp_recog_engine.h 等

/* 声明版本号 */
MRCP_PLUGIN_VERSION_DECLARE

/* 插件创建函数 */
MRCP_PLUGIN_DECLARE(mrcp_engine_t*) mrcp_plugin_create(apr_pool_t *pool)
{
    /* 创建引擎对象 */
    your_engine_t *engine = apr_palloc(pool, sizeof(your_engine_t));
    
    /* 初始化引擎对象 */
    /* ... */
    
    /* 创建并返回 MRCP 引擎 */
    return mrcp_engine_create(
        MRCP_SYNTHESIZER_RESOURCE,  /* 资源类型 */
        engine,                      /* 关联对象 */
        &engine_vtable,             /* 方法表 */
        pool                        /* 内存池 */
    );
}
```

### 步骤 3: 实现 Engine 方法

```c
/* Engine 方法声明 */
static apt_bool_t your_engine_destroy(mrcp_engine_t *engine);
static apt_bool_t your_engine_open(mrcp_engine_t *engine);
static apt_bool_t your_engine_close(mrcp_engine_t *engine);
static mrcp_engine_channel_t* your_engine_channel_create(
    mrcp_engine_t *engine, 
    apr_pool_t *pool
);

/* Engine 方法表 */
static const struct mrcp_engine_method_vtable_t engine_vtable = {
    your_engine_destroy,
    your_engine_open,
    your_engine_close,
    your_engine_channel_create
};
```

### 步骤 4: 实现 Channel 方法

```c
/* Channel 方法声明 */
static apt_bool_t your_channel_destroy(mrcp_engine_channel_t *channel);
static apt_bool_t your_channel_open(mrcp_engine_channel_t *channel);
static apt_bool_t your_channel_close(mrcp_engine_channel_t *channel);
static apt_bool_t your_channel_request_process(
    mrcp_engine_channel_t *channel, 
    mrcp_message_t *request
);

/* Channel 方法表 */
static const struct mrcp_engine_channel_method_vtable_t channel_vtable = {
    your_channel_destroy,
    your_channel_open,
    your_channel_close,
    your_channel_request_process
};
```

### 步骤 5: 实现音频流方法（如需要）

```c
/* Source Stream (TTS 使用) */
static apt_bool_t your_stream_read(mpf_audio_stream_t *stream, mpf_frame_t *frame);

static const mpf_audio_stream_vtable_t audio_stream_vtable = {
    your_stream_destroy,
    your_stream_open,
    your_stream_close,
    your_stream_read,      /* Source stream */
    NULL,                  /* Sink stream write */
    NULL,                  /* Source stream trace */
    NULL,                  /* Sink stream trace */
    NULL                   /* Event handler */
};

/* Sink Stream (ASR/Recorder 使用) */
static apt_bool_t your_stream_write(
    mpf_audio_stream_t *stream, 
    const mpf_frame_t *frame
);

static const mpf_audio_stream_vtable_t audio_stream_vtable = {
    your_stream_destroy,
    NULL,                  /* Source stream open */
    NULL,                  /* Source stream close */
    NULL,                  /* Source stream read */
    your_stream_open,      /* Sink stream open */
    your_stream_close,     /* Sink stream close */
    your_stream_write,     /* Sink stream */
    NULL
};
```

## 核心接口实现

### Engine 接口详解

#### destroy()

```c
static apt_bool_t your_engine_destroy(mrcp_engine_t *engine)
{
    your_engine_t *your_engine = engine->obj;
    
    /* 清理资源 */
    if (your_engine->task) {
        apt_task_t *task = apt_consumer_task_base_get(your_engine->task);
        apt_task_destroy(task);
        your_engine->task = NULL;
    }
    
    return TRUE;
}
```

#### open()

```c
static apt_bool_t your_engine_open(mrcp_engine_t *engine)
{
    your_engine_t *your_engine = engine->obj;
    
    /* 启动后台任务（如果需要） */
    if (your_engine->task) {
        apt_task_t *task = apt_consumer_task_base_get(your_engine->task);
        apt_task_start(task);
    }
    
    /* 发送打开响应 */
    return mrcp_engine_open_respond(engine, TRUE);
}
```

#### close()

```c
static apt_bool_t your_engine_close(mrcp_engine_t *engine)
{
    your_engine_t *your_engine = engine->obj;
    
    /* 停止后台任务 */
    if (your_engine->task) {
        apt_task_t *task = apt_consumer_task_base_get(your_engine->task);
        apt_task_terminate(task, TRUE);
    }
    
    /* 发送关闭响应 */
    return mrcp_engine_close_respond(engine);
}
```

#### create_channel()

```c
static mrcp_engine_channel_t* your_engine_channel_create(
    mrcp_engine_t *engine, 
    apr_pool_t *pool
)
{
    mpf_stream_capabilities_t *capabilities;
    mpf_termination_t *termination;
    
    /* 创建通道对象 */
    your_channel_t *channel = apr_palloc(pool, sizeof(your_channel_t));
    channel->your_engine = engine->obj;
    
    /* 设置流能力 */
    capabilities = mpf_source_stream_capabilities_create(pool);  /* 或 mpf_sink_stream_capabilities_create */
    mpf_codec_capabilities_add(
        &capabilities->codecs,
        MPF_SAMPLE_RATE_8000 | MPF_SAMPLE_RATE_16000,
        "LPCM"
    );
    
    /* 创建媒体终端 */
    termination = mrcp_engine_audio_termination_create(
        channel,                /* 关联对象 */
        &audio_stream_vtable,   /* 音频流方法表 */
        capabilities,           /* 流能力 */
        pool                   /* 内存池 */
    );
    
    /* 创建引擎通道 */
    channel->channel = mrcp_engine_channel_create(
        engine,                /* 引擎 */
        &channel_vtable,       /* 通道方法表 */
        channel,               /* 关联对象 */
        termination,           /* 媒体终端 */
        pool                   /* 内存池 */
    );
    
    return channel->channel;
}
```

### Channel 接口详解

#### open()

通道打开是异步操作，必须发送异步响应。

```c
static apt_bool_t your_channel_open(mrcp_engine_channel_t *channel)
{
    /* 使用消息队列发送到后台任务处理 */
    return your_msg_signal(YOUR_MSG_OPEN_CHANNEL, channel, NULL);
}

/* 在后台任务中处理 */
static apt_bool_t your_msg_process(apt_task_t *task, apt_task_msg_t *msg)
{
    your_msg_t *your_msg = (your_msg_t*)msg->data;
    switch (your_msg->type) {
        case YOUR_MSG_OPEN_CHANNEL:
            /* 执行打开操作 */
            /* ... */
            /* 发送异步响应 */
            mrcp_engine_channel_open_respond(your_msg->channel, TRUE);
            break;
        /* ... */
    }
    return TRUE;
}
```

#### close()

通道关闭也是异步操作。

```c
static apt_bool_t your_channel_close(mrcp_engine_channel_t *channel)
{
    return your_msg_signal(YOUR_MSG_CLOSE_CHANNEL, channel, NULL);
}
```

#### request_process()

处理 MRCP 请求，必须异步响应。

```c
static apt_bool_t your_channel_request_process(
    mrcp_engine_channel_t *channel, 
    mrcp_message_t *request
)
{
    return your_msg_signal(YOUR_MSG_REQUEST_PROCESS, channel, request);
}

/* 请求分发 */
static apt_bool_t your_channel_request_dispatch(
    mrcp_engine_channel_t *channel, 
    mrcp_message_t *request
)
{
    mrcp_message_t *response = mrcp_response_create(request, request->pool);
    
    switch (request->start_line.method_id) {
        case SYNTHESIZER_SPEAK:
            your_channel_speak(channel, request, response);
            break;
        case SYNTHESIZER_STOP:
            your_channel_stop(channel, request, response);
            break;
        /* ... */
        default:
            /* 发送默认响应 */
            mrcp_engine_channel_message_send(channel, response);
            break;
    }
    return TRUE;
}
```

## 音频流处理

### Source Stream (TTS)

Source Stream 用于输出音频数据。

```c
static apt_bool_t your_stream_read(mpf_audio_stream_t *stream, mpf_frame_t *frame)
{
    your_channel_t *channel = stream->obj;
    
    /* 检查是否有活动的 SPEAK 请求 */
    if (channel->speak_request) {
        /* 生成或读取音频数据 */
        /* frame->codec_frame.buffer 是输出缓冲区 */
        /* frame->codec_frame.size 是缓冲区大小 */
        
        apr_size_t size = frame->codec_frame.size;
        /* 填充音频数据 */
        /* ... */
        
        frame->type |= MEDIA_FRAME_TYPE_AUDIO;
        
        /* 如果音频播放完成，发送 SPEAK-COMPLETE 事件 */
        if (audio_completed) {
            mrcp_message_t *event = mrcp_event_create(
                channel->speak_request,
                SYNTHESIZER_SPEAK_COMPLETE,
                channel->speak_request->pool
            );
            /* 设置事件参数 */
            /* ... */
            mrcp_engine_channel_message_send(channel->channel, event);
            channel->speak_request = NULL;
        }
    }
    
    return TRUE;
}
```

### Sink Stream (ASR/Recorder)

Sink Stream 用于接收音频数据。

```c
static apt_bool_t your_stream_write(
    mpf_audio_stream_t *stream, 
    const mpf_frame_t *frame
)
{
    your_channel_t *channel = stream->obj;
    
    /* 检查是否有活动的识别请求 */
    if (channel->recog_request) {
        /* 处理音频帧 */
        if ((frame->type & MEDIA_FRAME_TYPE_AUDIO) == MEDIA_FRAME_TYPE_AUDIO) {
            /* 处理音频数据 */
            /* frame->codec_frame.buffer 包含音频数据 */
            /* frame->codec_frame.size 是数据大小 */
            
            /* 执行识别处理 */
            /* ... */
        }
        
        /* 检测到语音活动 */
        mpf_detector_event_e det_event = mpf_activity_detector_process(
            channel->detector, 
            frame
        );
        
        switch (det_event) {
            case MPF_DETECTOR_EVENT_ACTIVITY:
                /* 发送 START-OF-INPUT 事件 */
                break;
            case MPF_DETECTOR_EVENT_INACTIVITY:
                /* 发送 RECOGNITION-COMPLETE 事件 */
                break;
            case MPF_DETECTOR_EVENT_NOINPUT:
                /* 发送 RECOGNITION-COMPLETE 事件（无输入超时） */
                break;
        }
    }
    
    return TRUE;
}
```

## 消息处理

### 发送响应

```c
/* 同步响应（不推荐，除非是立即完成的简单操作） */
mrcp_message_t *response = mrcp_response_create(request, request->pool);
response->start_line.status_code = MRCP_STATUS_CODE_SUCCESS;
mrcp_engine_channel_message_send(channel, response);
```

### 发送事件

```c
/* 创建事件 */
mrcp_message_t *event = mrcp_event_create(
    request,                    /* 原始请求 */
    SYNTHESIZER_SPEAK_COMPLETE, /* 事件类型 */
    request->pool              /* 内存池 */
);

/* 设置事件参数 */
mrcp_synth_header_t *synth_header = mrcp_resource_header_prepare(event);
if (synth_header) {
    synth_header->completion_cause = SYNTHESIZER_COMPLETION_CAUSE_NORMAL;
    mrcp_resource_header_property_add(event, SYNTHESIZER_HEADER_COMPLETION_CAUSE);
}

event->start_line.request_state = MRCP_REQUEST_STATE_COMPLETE;

/* 发送事件 */
mrcp_engine_channel_message_send(channel, event);
```

### 请求状态

请求状态有四种：

- `MRCP_REQUEST_STATE_PENDING`: 待处理
- `MRCP_REQUEST_STATE_INPROGRESS`: 处理中
- `MRCP_REQUEST_STATE_COMPLETE`: 已完成
- `MRCP_REQUEST_STATE_INCOMPLETE`: 未完成（错误）

## 配置和构建

### 创建 Makefile.am

```makefile
AM_CPPFLAGS                = $(UNIMRCP_PLUGIN_INCLUDES)

plugin_LTLIBRARIES         = yourplugin.la

yourplugin_la_SOURCES      = src/your_plugin_engine.c
yourplugin_la_LDFLAGS      = $(UNIMRCP_PLUGIN_OPTS)

include $(top_srcdir)/build/rules/uniplugin.am
```

### 创建 CMakeLists.txt

```cmake
cmake_minimum_required (VERSION 2.8)
project (yourplugin)

# 设置源文件
set (YOUR_PLUGIN_SOURCES
    src/your_plugin_engine.c
)
source_group ("src" FILES ${YOUR_PLUGIN_SOURCES})

# 插件声明
add_library (${PROJECT_NAME} MODULE ${YOUR_PLUGIN_SOURCES}
    $<TARGET_OBJECTS:mrcpengine>
    $<TARGET_OBJECTS:mrcp>
    $<TARGET_OBJECTS:mpf>
    $<TARGET_OBJECTS:aprtoolkit>
)
set_target_properties (${PROJECT_NAME} PROPERTIES FOLDER "plugins")

# 链接库
target_link_libraries(${PROJECT_NAME}
    ${APU_LIBRARIES}
    ${APR_LIBRARIES}
)

# 系统库
if (WIN32)
    target_link_libraries(${PROJECT_NAME} ws2_32 winmm)
elseif (UNIX)
    target_link_libraries(${PROJECT_NAME} m)
endif ()

# 预处理器定义
add_definitions (
    ${MRCP_DEFINES}
    ${MPF_DEFINES}
    ${APR_TOOLKIT_DEFINES}
    ${APR_DEFINES}
    ${APU_DEFINES}
)

# 包含目录
include_directories (
    ${PROJECT_SOURCE_DIR}/include
    ${MRCP_ENGINE_INCLUDE_DIRS}
    ${MRCP_INCLUDE_DIRS}
    ${MPF_INCLUDE_DIRS}
    ${APR_TOOLKIT_INCLUDE_DIRS}
    ${APR_INCLUDE_DIRS}
    ${APU_INCLUDE_DIRS}
)

# 安装
install (TARGETS ${PROJECT_NAME} LIBRARY DESTINATION plugin)
```

### 更新 configure.ac

```m4
dnl Your plugin.
UNI_PLUGIN_ENABLED(yourplugin)

AM_CONDITIONAL([YOURPLUGIN_PLUGIN],[test "${enable_yourplugin_plugin}" = "yes"])
```

在输出部分添加：

```m4
echo Your plugin............... : $enable_yourplugin_plugin
```

### 更新主 Makefile.am

在 `plugins/Makefile.am` 中添加：

```makefile
if YOURPLUGIN_PLUGIN
SUBDIRS               += your-plugin
endif
```

### 更新 CMakeLists.txt

在主 `CMakeLists.txt` 中添加选项：

```cmake
option (ENABLE_YOURPLUGIN_PLUGIN "Enable your plugin" ON)
```

在插件部分添加：

```cmake
if (ENABLE_YOURPLUGIN_PLUGIN)
add_subdirectory (plugins/your-plugin)
endif ()
```

### 配置文件

在 `conf/unimrcpserver.xml` 中添加：

```xml
<plugin-factory>
  <engine id="Your-Plugin-1" name="yourplugin" enable="true">
    <max-channel-count>100</max-channel-count>
    <param name="param1" value="value1"/>
    <param name="param2" value="value2"/>
  </engine>
</plugin-factory>
```

### 读取配置参数

```c
static mrcp_engine_channel_t* your_engine_channel_create(
    mrcp_engine_t *engine, 
    apr_pool_t *pool
)
{
    const char *param1 = mrcp_engine_param_get(engine, "param1");
    const char *param2 = mrcp_engine_param_get(engine, "param2");
    
    /* 使用参数 */
    /* ... */
}
```

## 调试和测试

### 日志

使用插件日志系统：

```c
/* 声明日志源 */
MRCP_PLUGIN_LOG_SOURCE_IMPLEMENT(YOUR_PLUGIN,"YOUR-PLUGIN")

/* 定义日志标记 */
#define YOUR_LOG_MARK   APT_LOG_MARK_DECLARE(YOUR_PLUGIN)

/* 使用日志 */
apt_log(YOUR_LOG_MARK, APT_PRIO_INFO, "Plugin initialized");
apt_log(YOUR_LOG_MARK, APT_PRIO_DEBUG, "Processing request");
apt_log(YOUR_LOG_MARK, APT_PRIO_ERROR, "Error occurred: %s", error_msg);
```

日志级别：

- `APT_PRIO_DEBUG`: 调试信息
- `APT_PRIO_INFO`: 一般信息
- `APT_PRIO_NOTICE`: 通知
- `APT_PRIO_WARNING`: 警告
- `APT_PRIO_ERROR`: 错误

在 `conf/logger.xml` 中配置日志源：

```xml
<source name="YOUR-PLUGIN" priority="DEBUG" masking="NONE"/>
```

### 调试技巧

1. **使用 GDB**:
   ```bash
   gdb unimrcpserver
   (gdb) set args -f /path/to/unimrcpserver.xml
   (gdb) break your_function
   (gdb) run
   ```

2. **检查插件加载**:
   - 查看服务器日志确认插件是否成功加载
   - 使用 `ldd` (Linux) 或 `otool` (macOS) 检查依赖

3. **内存调试**:
   - 使用 Valgrind 检测内存泄漏
   - 确保正确使用 APR 内存池

### 测试

1. **单元测试**: 测试各个函数的功能
2. **集成测试**: 使用 UniMRCP 客户端工具测试
3. **压力测试**: 测试并发连接和负载

## 最佳实践

### 1. 内存管理

- **使用 APR 内存池**: 所有内存分配应该使用 `apr_palloc()` 而不是 `malloc()`
- **不要释放池内存**: APR 内存池会在池销毁时自动释放所有内存
- **生命周期管理**: 确保对象的生命周期与内存池一致

```c
/* 正确 */
apr_pool_t *pool = apr_palloc(global_pool, sizeof(apr_pool_t));
apr_pool_create(&pool, global_pool);
char *str = apr_pstrdup(pool, "test");
/* 池销毁时自动释放 str */

/* 错误 */
char *str = malloc(100);
/* 必须手动释放 */
```

### 2. 异步处理

- **不阻塞回调**: Channel 和 Stream 的回调方法不能阻塞
- **使用后台任务**: 长时间操作应该使用 `apt_consumer_task`
- **消息队列**: 使用消息队列在线程间通信

```c
/* 创建后台任务 */
apt_task_msg_pool_t *msg_pool = apt_task_msg_pool_create_dynamic(
    sizeof(your_msg_t), 
    pool
);
your_engine->task = apt_consumer_task_create(your_engine, msg_pool, pool);

/* 发送消息 */
apt_task_t *task = apt_consumer_task_base_get(your_engine->task);
apt_task_msg_t *msg = apt_task_msg_get(task);
your_msg_t *your_msg = (your_msg_t*)msg->data;
your_msg->type = YOUR_MSG_TYPE;
apt_task_msg_signal(task, msg);
```

### 3. 错误处理

- **检查返回值**: 总是检查函数返回值
- **发送错误响应**: 处理错误时发送适当的错误响应
- **日志记录**: 记录所有错误信息

```c
apr_status_t rv = apr_socket_create(&socket, ...);
if (rv != APR_SUCCESS) {
    apt_log(YOUR_LOG_MARK, APT_PRIO_ERROR, "Failed to create socket");
    response->start_line.status_code = MRCP_STATUS_CODE_METHOD_FAILED;
    mrcp_engine_channel_message_send(channel, response);
    return FALSE;
}
```

### 4. 线程安全

- **避免共享状态**: 尽量减少共享状态，每个 Channel 应该有独立的状态
- **使用锁**: 如果必须共享状态，使用 APR 锁机制
- **原子操作**: 对于简单的标志，考虑使用原子操作

### 5. 性能优化

- **减少内存分配**: 重用缓冲区
- **批量处理**: 批量处理音频数据
- **避免不必要的复制**: 使用指针而不是复制数据

## 常见问题

### Q1: 插件加载失败

**问题**: 服务器日志显示插件加载失败

**解决方法**:
1. 检查插件文件是否存在且可读
2. 检查依赖库是否正确链接
3. 检查插件符号是否正确导出
4. 使用 `ldd` 检查依赖

### Q2: 内存泄漏

**问题**: 长时间运行后内存不断增长

**解决方法**:
1. 确保使用 APR 内存池而不是 `malloc/free`
2. 检查是否有循环引用
3. 使用 Valgrind 检测泄漏

### Q3: 插件崩溃

**问题**: 插件导致服务器崩溃

**解决方法**:
1. 使用 GDB 获取崩溃堆栈
2. 检查空指针访问
3. 检查数组越界
4. 检查线程安全问题

### Q4: 请求超时

**问题**: MRCP 请求超时

**解决方法**:
1. 确保每个请求都发送了响应
2. 检查异步响应是否正确发送
3. 检查后台任务是否正常运行

### Q5: 音频数据问题

**问题**: 音频数据不正确或丢失

**解决方法**:
1. 检查编解码器设置
2. 检查采样率匹配
3. 检查音频缓冲区大小
4. 验证音频数据格式

## 参考资源

### 官方资源

- **官网**: https://www.unimrcp.org/
- **文档**: https://www.unimrcp.org/documentation
- **GitHub**: https://github.com/unispeech/unimrcp
- **邮件列表**: https://groups.google.com/group/unimrcp

### 示例插件

- `plugins/demo-synth`: 演示 TTS 插件
- `plugins/demo-recog`: 演示 ASR 插件
- `plugins/demo-verifier`: 演示 SVI 插件
- `plugins/mrcp-recorder`: 演示 Recorder 插件

### API 参考

关键头文件：

- `libs/mrcp-engine/include/mrcp_engine_plugin.h`: 插件接口
- `libs/mrcp-engine/include/mrcp_engine_types.h`: 类型定义
- `libs/mrcp-engine/include/mrcp_engine_impl.h`: 实现辅助函数
- `libs/mrcp-engine/include/mrcp_synth_engine.h`: TTS 接口
- `libs/mrcp-engine/include/mrcp_recog_engine.h`: ASR 接口
- `libs/mpf/include/mpf_stream.h`: 音频流接口

## 总结

开发 UniMRCP 插件需要：

1. **理解架构**: 理解 Engine、Channel、Stream 的关系
2. **实现接口**: 正确实现所有必需的接口
3. **异步处理**: 使用异步方式处理请求和响应
4. **内存管理**: 正确使用 APR 内存池
5. **错误处理**: 完善的错误处理和日志记录
6. **测试验证**: 充分的测试确保插件稳定可靠

遵循本指南，你应该能够成功开发自己的 UniMRCP 插件。如有问题，请参考示例插件代码或访问 UniMRCP 社区获取帮助。

---

**文档版本**: 1.0  
**最后更新**: 2024  
**作者**: UniMRCP 开发团队

