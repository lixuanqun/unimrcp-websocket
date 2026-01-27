/*
 * Copyright 2024
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* 
 * Mandatory rules concerning plugin implementation.
 * 1. Each plugin MUST implement a plugin/engine creator function
 *    with the exact signature and name (the main entry point)
 *        MRCP_PLUGIN_DECLARE(mrcp_engine_t*) mrcp_plugin_create(apr_pool_t *pool)
 * 2. Each plugin MUST declare its version number
 *        MRCP_PLUGIN_VERSION_DECLARE
 * 3. One and only one response MUST be sent back to the received request.
 * 4. Methods (callbacks) of the MRCP engine channel MUST not block.
 *   (asynchronous response can be sent from the context of other thread)
 * 5. Methods (callbacks) of the MPF engine stream MUST not block.
 */

#include <stdlib.h>
#include "mrcp_recog_engine.h"
#include "mpf_activity_detector.h"
#include "apt_consumer_task.h"
#include "apt_log.h"
#include "ws_client.h"
#include <apr_strings.h>
#include <apr_thread_mutex.h>
#include <string.h>

#define WEBSOCKET_RECOG_ENGINE_TASK_NAME "WebSocket Recog Engine"

/*******************************************************************************
 * Configuration Constants
 ******************************************************************************/

/** Audio buffer configuration */
#define AUDIO_BUFFER_SIZE       (512 * 1024)    /* 512KB buffer (~16 seconds @ 8kHz) */
#define STREAM_CHUNK_SIZE       (3200)          /* 200ms chunks for streaming */

/** Timing configuration */
#define RECV_POLL_INTERVAL      (50000)         /* 50ms polling interval */
#define MAX_RECOGNIZE_DURATION  (60 * 1000000)  /* 60 seconds max */

/*******************************************************************************
 * Type Definitions
 ******************************************************************************/

typedef struct websocket_recog_engine_t websocket_recog_engine_t;
typedef struct websocket_recog_channel_t websocket_recog_channel_t;
typedef struct websocket_recog_msg_t websocket_recog_msg_t;

/** Message types for background task */
typedef enum {
    WEBSOCKET_RECOG_MSG_OPEN_CHANNEL,
    WEBSOCKET_RECOG_MSG_CLOSE_CHANNEL,
    WEBSOCKET_RECOG_MSG_REQUEST_PROCESS,
    WEBSOCKET_RECOG_MSG_SEND_AUDIO,       /* Send buffered audio */
    WEBSOCKET_RECOG_MSG_STREAM_AUDIO,     /* Stream audio chunk (real-time) */
    WEBSOCKET_RECOG_MSG_RECV_RESULT       /* Poll for recognition result */
} websocket_recog_msg_type_e;

/** Declaration of websocket recognizer engine */
struct websocket_recog_engine_t {
    apt_consumer_task_t *task;
    apr_pool_t *pool;
};

/** Declaration of websocket recognizer channel */
struct websocket_recog_channel_t {
    /** Back pointer to engine */
    websocket_recog_engine_t *recog_engine;
    /** Engine channel base */
    mrcp_engine_channel_t    *channel;

    /** Active (in-progress) recognition request */
    mrcp_message_t           *recog_request;
    /** Pending stop response */
    mrcp_message_t           *stop_response;
    /** Indicates whether input timers are started */
    apt_bool_t                timers_started;
    /** Voice activity detector */
    mpf_activity_detector_t  *detector;
    
    /** WebSocket client */
    ws_client_t              *ws_client;
    
    /** Audio buffer */
    char                     *audio_buffer;
    apr_size_t                audio_buffer_size;
    apr_size_t                audio_buffer_pos;
    
    /** Streaming state */
    apt_bool_t                streaming_enabled;
    apt_bool_t                speech_started;
    apt_bool_t                waiting_result;
    apr_size_t                stream_pos;
    
    /** Mutex for thread safety */
    apr_thread_mutex_t       *mutex;
    
    /** Timing */
    apr_time_t                recognize_start_time;
};

/** Declaration of websocket recognizer task message */
struct websocket_recog_msg_t {
    websocket_recog_msg_type_e type;
    mrcp_engine_channel_t     *channel;
    mrcp_message_t            *request;
    const char                *audio_data;
    apr_size_t                 audio_len;
};

/*******************************************************************************
 * Function Declarations
 ******************************************************************************/

/** Engine methods */
static apt_bool_t websocket_recog_engine_destroy(mrcp_engine_t *engine);
static apt_bool_t websocket_recog_engine_open(mrcp_engine_t *engine);
static apt_bool_t websocket_recog_engine_close(mrcp_engine_t *engine);
static mrcp_engine_channel_t* websocket_recog_engine_channel_create(mrcp_engine_t *engine, apr_pool_t *pool);

static const struct mrcp_engine_method_vtable_t engine_vtable = {
    websocket_recog_engine_destroy,
    websocket_recog_engine_open,
    websocket_recog_engine_close,
    websocket_recog_engine_channel_create
};

/** Channel methods */
static apt_bool_t websocket_recog_channel_destroy(mrcp_engine_channel_t *channel);
static apt_bool_t websocket_recog_channel_open(mrcp_engine_channel_t *channel);
static apt_bool_t websocket_recog_channel_close(mrcp_engine_channel_t *channel);
static apt_bool_t websocket_recog_channel_request_process(mrcp_engine_channel_t *channel, mrcp_message_t *request);

static const struct mrcp_engine_channel_method_vtable_t channel_vtable = {
    websocket_recog_channel_destroy,
    websocket_recog_channel_open,
    websocket_recog_channel_close,
    websocket_recog_channel_request_process
};

/** Audio stream methods */
static apt_bool_t websocket_recog_stream_destroy(mpf_audio_stream_t *stream);
static apt_bool_t websocket_recog_stream_open(mpf_audio_stream_t *stream, mpf_codec_t *codec);
static apt_bool_t websocket_recog_stream_close(mpf_audio_stream_t *stream);
static apt_bool_t websocket_recog_stream_write(mpf_audio_stream_t *stream, const mpf_frame_t *frame);

static const mpf_audio_stream_vtable_t audio_stream_vtable = {
    websocket_recog_stream_destroy,
    NULL, NULL, NULL,
    websocket_recog_stream_open,
    websocket_recog_stream_close,
    websocket_recog_stream_write,
    NULL
};

/** Internal functions */
static apt_bool_t websocket_recog_msg_signal(websocket_recog_msg_type_e type, mrcp_engine_channel_t *channel, mrcp_message_t *request);
static apt_bool_t websocket_recog_msg_signal_audio(mrcp_engine_channel_t *channel, const char *data, apr_size_t len);
static apt_bool_t websocket_recog_msg_process(apt_task_t *task, apt_task_msg_t *msg);
static apt_bool_t websocket_recog_channel_request_dispatch(mrcp_engine_channel_t *channel, mrcp_message_t *request);

/*******************************************************************************
 * Plugin Declaration
 ******************************************************************************/

MRCP_PLUGIN_VERSION_DECLARE

MRCP_PLUGIN_LOG_SOURCE_IMPLEMENT(WEBSOCKET_RECOG_PLUGIN, "WEBSOCKET-RECOG-PLUGIN")

#define RECOG_LOG_MARK APT_LOG_MARK_DECLARE(WEBSOCKET_RECOG_PLUGIN)

/*******************************************************************************
 * Engine Implementation
 ******************************************************************************/

MRCP_PLUGIN_DECLARE(mrcp_engine_t*) mrcp_plugin_create(apr_pool_t *pool)
{
    websocket_recog_engine_t *recog_engine;
    apt_task_t *task;
    apt_task_vtable_t *vtable;
    apt_task_msg_pool_t *msg_pool;

    apt_log(RECOG_LOG_MARK, APT_PRIO_INFO, "Create WebSocket Recog Engine");

    recog_engine = apr_palloc(pool, sizeof(websocket_recog_engine_t));
    if (!recog_engine) {
        return NULL;
    }
    
    recog_engine->pool = pool;

    msg_pool = apt_task_msg_pool_create_dynamic(sizeof(websocket_recog_msg_t), pool);
    recog_engine->task = apt_consumer_task_create(recog_engine, msg_pool, pool);
    if (!recog_engine->task) {
        return NULL;
    }
    
    task = apt_consumer_task_base_get(recog_engine->task);
    apt_task_name_set(task, WEBSOCKET_RECOG_ENGINE_TASK_NAME);
    vtable = apt_task_vtable_get(task);
    if (vtable) {
        vtable->process_msg = websocket_recog_msg_process;
    }

    return mrcp_engine_create(
        MRCP_RECOGNIZER_RESOURCE,
        recog_engine,
        &engine_vtable,
        pool);
}

static apt_bool_t websocket_recog_engine_destroy(mrcp_engine_t *engine)
{
    websocket_recog_engine_t *recog_engine = engine->obj;
    apt_log(RECOG_LOG_MARK, APT_PRIO_INFO, "Destroy WebSocket Recog Engine");
    
    if (recog_engine->task) {
        apt_task_t *task = apt_consumer_task_base_get(recog_engine->task);
        apt_task_destroy(task);
        recog_engine->task = NULL;
    }
    return TRUE;
}

static apt_bool_t websocket_recog_engine_open(mrcp_engine_t *engine)
{
    websocket_recog_engine_t *recog_engine = engine->obj;
    apt_log(RECOG_LOG_MARK, APT_PRIO_INFO, "Open WebSocket Recog Engine");
    
    if (recog_engine->task) {
        apt_task_t *task = apt_consumer_task_base_get(recog_engine->task);
        apt_task_start(task);
    }
    return mrcp_engine_open_respond(engine, TRUE);
}

static apt_bool_t websocket_recog_engine_close(mrcp_engine_t *engine)
{
    websocket_recog_engine_t *recog_engine = engine->obj;
    apt_log(RECOG_LOG_MARK, APT_PRIO_INFO, "Close WebSocket Recog Engine");
    
    if (recog_engine->task) {
        apt_task_t *task = apt_consumer_task_base_get(recog_engine->task);
        apt_task_terminate(task, TRUE);
    }
    return mrcp_engine_close_respond(engine);
}

/*******************************************************************************
 * Channel Implementation
 ******************************************************************************/

static mrcp_engine_channel_t* websocket_recog_engine_channel_create(mrcp_engine_t *engine, apr_pool_t *pool)
{
    mpf_stream_capabilities_t *capabilities;
    mpf_termination_t *termination;
    websocket_recog_channel_t *recog_channel;
    ws_client_config_t ws_config;
    const char *ws_host;
    const char *ws_port_str;
    const char *ws_path;
    const char *streaming_str;

    apt_log(RECOG_LOG_MARK, APT_PRIO_INFO, "Create WebSocket Recog Channel");

    recog_channel = apr_palloc(pool, sizeof(websocket_recog_channel_t));
    if (!recog_channel) {
        return NULL;
    }
    
    recog_channel->recog_engine = engine->obj;
    recog_channel->recog_request = NULL;
    recog_channel->stop_response = NULL;
    recog_channel->timers_started = FALSE;
    recog_channel->detector = mpf_activity_detector_create(pool);
    recog_channel->ws_client = NULL;
    recog_channel->speech_started = FALSE;
    recog_channel->waiting_result = FALSE;
    recog_channel->stream_pos = 0;
    recog_channel->recognize_start_time = 0;

    /* Initialize audio buffer */
    recog_channel->audio_buffer_size = AUDIO_BUFFER_SIZE;
    recog_channel->audio_buffer = apr_palloc(pool, recog_channel->audio_buffer_size);
    if (!recog_channel->audio_buffer) {
        return NULL;
    }
    recog_channel->audio_buffer_pos = 0;

    /* Create mutex */
    if (apr_thread_mutex_create(&recog_channel->mutex, APR_THREAD_MUTEX_DEFAULT, pool) != APR_SUCCESS) {
        return NULL;
    }

    /* Get WebSocket configuration */
    ws_client_config_init(&ws_config);
    
    ws_host = mrcp_engine_param_get(engine, "ws-host");
    ws_port_str = mrcp_engine_param_get(engine, "ws-port");
    ws_path = mrcp_engine_param_get(engine, "ws-path");
    streaming_str = mrcp_engine_param_get(engine, "streaming");

    ws_config.host = ws_host ? ws_host : "localhost";
    ws_config.port = ws_port_str ? atoi(ws_port_str) : 8080;
    ws_config.path = ws_path ? ws_path : "/asr";
    ws_config.recv_timeout = RECV_POLL_INTERVAL;
    
    /* Enable streaming mode if configured */
    recog_channel->streaming_enabled = (streaming_str && strcasecmp(streaming_str, "true") == 0);

    apt_log(RECOG_LOG_MARK, APT_PRIO_INFO, 
        "WebSocket Config: host=%s port=%d path=%s streaming=%s",
        ws_config.host, ws_config.port, ws_config.path,
        recog_channel->streaming_enabled ? "enabled" : "disabled");

    /* Create WebSocket client */
    recog_channel->ws_client = ws_client_create(pool, &ws_config);
    if (!recog_channel->ws_client) {
        return NULL;
    }

    /* Create sink stream capabilities (ASR receives audio) */
    capabilities = mpf_sink_stream_capabilities_create(pool);
    mpf_codec_capabilities_add(
        &capabilities->codecs,
        MPF_SAMPLE_RATE_8000 | MPF_SAMPLE_RATE_16000,
        "LPCM");

    termination = mrcp_engine_audio_termination_create(
        recog_channel,
        &audio_stream_vtable,
        capabilities,
        pool);

    recog_channel->channel = mrcp_engine_channel_create(
        engine,
        &channel_vtable,
        recog_channel,
        termination,
        pool);

    return recog_channel->channel;
}

static apt_bool_t websocket_recog_channel_destroy(mrcp_engine_channel_t *channel)
{
    websocket_recog_channel_t *recog_channel = channel->method_obj;
    apt_log(RECOG_LOG_MARK, APT_PRIO_INFO, "Destroy WebSocket Recog Channel");
    
    if (recog_channel->ws_client) {
        ws_client_destroy(recog_channel->ws_client);
        recog_channel->ws_client = NULL;
    }
    if (recog_channel->mutex) {
        apr_thread_mutex_destroy(recog_channel->mutex);
        recog_channel->mutex = NULL;
    }
    return TRUE;
}

static apt_bool_t websocket_recog_channel_open(mrcp_engine_channel_t *channel)
{
    apt_log(RECOG_LOG_MARK, APT_PRIO_INFO, "Open WebSocket Recog Channel");
    
    if (channel->attribs) {
        const apr_array_header_t *header = apr_table_elts(channel->attribs);
        apr_table_entry_t *entry = (apr_table_entry_t*)header->elts;
        int i;
        for (i = 0; i < header->nelts; i++) {
            apt_log(RECOG_LOG_MARK, APT_PRIO_DEBUG, 
                "Attrib: %s=%s", entry[i].key, entry[i].val);
        }
    }

    return websocket_recog_msg_signal(WEBSOCKET_RECOG_MSG_OPEN_CHANNEL, channel, NULL);
}

static apt_bool_t websocket_recog_channel_close(mrcp_engine_channel_t *channel)
{
    apt_log(RECOG_LOG_MARK, APT_PRIO_INFO, "Close WebSocket Recog Channel");
    return websocket_recog_msg_signal(WEBSOCKET_RECOG_MSG_CLOSE_CHANNEL, channel, NULL);
}

static apt_bool_t websocket_recog_channel_request_process(mrcp_engine_channel_t *channel, mrcp_message_t *request)
{
    return websocket_recog_msg_signal(WEBSOCKET_RECOG_MSG_REQUEST_PROCESS, channel, request);
}

/*******************************************************************************
 * Audio Stream Implementation
 ******************************************************************************/

static apt_bool_t websocket_recog_stream_destroy(mpf_audio_stream_t *stream)
{
    return TRUE;
}

static apt_bool_t websocket_recog_stream_open(mpf_audio_stream_t *stream, mpf_codec_t *codec)
{
    return TRUE;
}

static apt_bool_t websocket_recog_stream_close(mpf_audio_stream_t *stream)
{
    return TRUE;
}

/** START-OF-INPUT event */
static apt_bool_t websocket_recog_start_of_input(websocket_recog_channel_t *recog_channel)
{
    mrcp_message_t *message;
    
    if (!recog_channel->recog_request) {
        return FALSE;
    }
    
    message = mrcp_event_create(
        recog_channel->recog_request,
        RECOGNIZER_START_OF_INPUT,
        recog_channel->recog_request->pool);
    if (!message) {
        return FALSE;
    }

    message->start_line.request_state = MRCP_REQUEST_STATE_INPROGRESS;
    return mrcp_engine_channel_message_send(recog_channel->channel, message);
}

/** RECOGNITION-COMPLETE event */
static apt_bool_t websocket_recog_recognition_complete(
    websocket_recog_channel_t *recog_channel,
    mrcp_recog_completion_cause_e cause,
    const char *result_text)
{
    mrcp_message_t *message;
    mrcp_recog_header_t *recog_header;
    
    if (!recog_channel->recog_request) {
        return FALSE;
    }

    message = mrcp_event_create(
        recog_channel->recog_request,
        RECOGNIZER_RECOGNITION_COMPLETE,
        recog_channel->recog_request->pool);
    if (!message) {
        return FALSE;
    }

    recog_header = mrcp_resource_header_prepare(message);
    if (recog_header) {
        recog_header->completion_cause = cause;
        mrcp_resource_header_property_add(message, RECOGNIZER_HEADER_COMPLETION_CAUSE);
    }

    message->start_line.request_state = MRCP_REQUEST_STATE_COMPLETE;

    /* Set result body */
    if (result_text && strlen(result_text) > 0) {
        mrcp_generic_header_t *generic_header = mrcp_generic_header_prepare(message);
        if (generic_header) {
            apt_string_assign(&message->body, result_text, message->pool);
            apt_string_assign(&generic_header->content_type, "application/x-nlsml", message->pool);
            mrcp_generic_header_property_add(message, GENERIC_HEADER_CONTENT_TYPE);
        }
    }

    recog_channel->recog_request = NULL;
    recog_channel->waiting_result = FALSE;

    apt_log(RECOG_LOG_MARK, APT_PRIO_INFO, "RECOGNITION-COMPLETE: cause=%d", cause);

    return mrcp_engine_channel_message_send(recog_channel->channel, message);
}

/** Write audio frame (called from MPF context) */
static apt_bool_t websocket_recog_stream_write(mpf_audio_stream_t *stream, const mpf_frame_t *frame)
{
    websocket_recog_channel_t *recog_channel = stream->obj;
    
    /* Handle STOP request */
    if (recog_channel->stop_response) {
        mrcp_engine_channel_message_send(recog_channel->channel, recog_channel->stop_response);
        recog_channel->stop_response = NULL;
        recog_channel->recog_request = NULL;
        return TRUE;
    }

    if (!recog_channel->recog_request || !recog_channel->ws_client) {
        return TRUE;
    }

    /* Process audio through VAD */
    if (ws_client_is_connected(recog_channel->ws_client)) {
        mpf_detector_event_e det_event = mpf_activity_detector_process(recog_channel->detector, frame);
        
        switch (det_event) {
            case MPF_DETECTOR_EVENT_ACTIVITY:
                apt_log(RECOG_LOG_MARK, APT_PRIO_INFO, "Voice Activity Detected");
                recog_channel->speech_started = TRUE;
                websocket_recog_start_of_input(recog_channel);
                break;
                
            case MPF_DETECTOR_EVENT_INACTIVITY:
                apt_log(RECOG_LOG_MARK, APT_PRIO_INFO, "Voice Inactivity Detected");
                if (recog_channel->audio_buffer_pos > 0) {
                    /* Send buffered audio and wait for result */
                    websocket_recog_msg_signal(WEBSOCKET_RECOG_MSG_SEND_AUDIO, 
                        recog_channel->channel, NULL);
                } else {
                    websocket_recog_recognition_complete(recog_channel, 
                        RECOGNIZER_COMPLETION_CAUSE_SUCCESS, NULL);
                }
                break;
                
            case MPF_DETECTOR_EVENT_NOINPUT:
                apt_log(RECOG_LOG_MARK, APT_PRIO_INFO, "No Input Detected");
                if (recog_channel->timers_started) {
                    websocket_recog_recognition_complete(recog_channel, 
                        RECOGNIZER_COMPLETION_CAUSE_NO_INPUT_TIMEOUT, NULL);
                }
                break;
                
            default:
                break;
        }

        /* Buffer audio data */
        if ((frame->type & MEDIA_FRAME_TYPE_AUDIO) == MEDIA_FRAME_TYPE_AUDIO) {
            apr_thread_mutex_lock(recog_channel->mutex);
            
            apr_size_t remaining = recog_channel->audio_buffer_size - recog_channel->audio_buffer_pos;
            apr_size_t to_copy = frame->codec_frame.size;
            
            if (to_copy > remaining) {
                to_copy = remaining;
                apt_log(RECOG_LOG_MARK, APT_PRIO_WARNING, "Audio buffer full");
            }
            
            if (to_copy > 0) {
                memcpy(recog_channel->audio_buffer + recog_channel->audio_buffer_pos,
                       frame->codec_frame.buffer, to_copy);
                recog_channel->audio_buffer_pos += to_copy;
            }
            
            apr_thread_mutex_unlock(recog_channel->mutex);
            
            /* Stream audio in real-time if enabled */
            if (recog_channel->streaming_enabled && recog_channel->speech_started) {
                apr_size_t unsent = recog_channel->audio_buffer_pos - recog_channel->stream_pos;
                if (unsent >= STREAM_CHUNK_SIZE) {
                    websocket_recog_msg_signal_audio(recog_channel->channel,
                        recog_channel->audio_buffer + recog_channel->stream_pos,
                        STREAM_CHUNK_SIZE);
                    recog_channel->stream_pos += STREAM_CHUNK_SIZE;
                }
            }
        }
    }

    return TRUE;
}

/*******************************************************************************
 * MRCP Request Handling
 ******************************************************************************/

static apt_bool_t websocket_recog_channel_recognize(
    mrcp_engine_channel_t *channel,
    mrcp_message_t *request,
    mrcp_message_t *response)
{
    websocket_recog_channel_t *recog_channel = channel->method_obj;
    mrcp_recog_header_t *recog_header;
    const mpf_codec_descriptor_t *descriptor = mrcp_engine_sink_stream_codec_get(channel);

    if (!descriptor) {
        apt_log(RECOG_LOG_MARK, APT_PRIO_WARNING, 
            "Failed to get codec descriptor " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
        response->start_line.status_code = MRCP_STATUS_CODE_METHOD_FAILED;
        return FALSE;
    }

    recog_channel->timers_started = TRUE;

    /* Get recognizer header */
    recog_header = mrcp_resource_header_get(request);
    if (recog_header) {
        if (mrcp_resource_header_property_check(request, RECOGNIZER_HEADER_START_INPUT_TIMERS) == TRUE) {
            recog_channel->timers_started = recog_header->start_input_timers;
        }
        if (mrcp_resource_header_property_check(request, RECOGNIZER_HEADER_NO_INPUT_TIMEOUT) == TRUE) {
            mpf_activity_detector_noinput_timeout_set(recog_channel->detector, recog_header->no_input_timeout);
        }
        if (mrcp_resource_header_property_check(request, RECOGNIZER_HEADER_SPEECH_COMPLETE_TIMEOUT) == TRUE) {
            mpf_activity_detector_silence_timeout_set(recog_channel->detector, recog_header->speech_complete_timeout);
        }
    }

    /* Connect to WebSocket server */
    if (!ws_client_ensure_connected(recog_channel->ws_client)) {
        apt_log(RECOG_LOG_MARK, APT_PRIO_ERROR, "Failed to connect to ASR server");
        response->start_line.status_code = MRCP_STATUS_CODE_METHOD_FAILED;
        return FALSE;
    }

    /* Reset state */
    apr_thread_mutex_lock(recog_channel->mutex);
    recog_channel->audio_buffer_pos = 0;
    recog_channel->stream_pos = 0;
    recog_channel->speech_started = FALSE;
    recog_channel->waiting_result = FALSE;
    recog_channel->recognize_start_time = apr_time_now();
    apr_thread_mutex_unlock(recog_channel->mutex);

    /* Send response */
    response->start_line.request_state = MRCP_REQUEST_STATE_INPROGRESS;
    mrcp_engine_channel_message_send(channel, response);
    recog_channel->recog_request = request;

    apt_log(RECOG_LOG_MARK, APT_PRIO_INFO, 
        "RECOGNIZE: sample_rate=%d " APT_SIDRES_FMT,
        descriptor->sampling_rate, MRCP_MESSAGE_SIDRES(request));

    return TRUE;
}

static apt_bool_t websocket_recog_channel_stop(
    mrcp_engine_channel_t *channel,
    mrcp_message_t *request,
    mrcp_message_t *response)
{
    websocket_recog_channel_t *recog_channel = channel->method_obj;
    apt_log(RECOG_LOG_MARK, APT_PRIO_INFO, "STOP Request");
    
    recog_channel->stop_response = response;
    return TRUE;
}

static apt_bool_t websocket_recog_channel_timers_start(
    mrcp_engine_channel_t *channel,
    mrcp_message_t *request,
    mrcp_message_t *response)
{
    websocket_recog_channel_t *recog_channel = channel->method_obj;
    recog_channel->timers_started = TRUE;
    return mrcp_engine_channel_message_send(channel, response);
}

static apt_bool_t websocket_recog_channel_set_params(
    mrcp_engine_channel_t *channel,
    mrcp_message_t *request,
    mrcp_message_t *response)
{
    mrcp_recog_header_t *recog_header = mrcp_resource_header_get(request);
    
    if (recog_header) {
        /* Handle parameters */
        if (mrcp_resource_header_property_check(request, RECOGNIZER_HEADER_CONFIDENCE_THRESHOLD) == TRUE) {
            apt_log(RECOG_LOG_MARK, APT_PRIO_INFO, 
                "Set Confidence Threshold: %.2f", recog_header->confidence_threshold);
        }
    }
    
    mrcp_engine_channel_message_send(channel, response);
    return TRUE;
}

static apt_bool_t websocket_recog_channel_get_params(
    mrcp_engine_channel_t *channel,
    mrcp_message_t *request,
    mrcp_message_t *response)
{
    mrcp_recog_header_t *req_header = mrcp_resource_header_get(request);
    
    if (req_header) {
        mrcp_recog_header_t *res_header = mrcp_resource_header_prepare(response);
        if (mrcp_resource_header_property_check(request, RECOGNIZER_HEADER_CONFIDENCE_THRESHOLD) == TRUE) {
            res_header->confidence_threshold = 0.5f;
            mrcp_resource_header_property_add(response, RECOGNIZER_HEADER_CONFIDENCE_THRESHOLD);
        }
    }
    
    mrcp_engine_channel_message_send(channel, response);
    return TRUE;
}

static apt_bool_t websocket_recog_channel_request_dispatch(mrcp_engine_channel_t *channel, mrcp_message_t *request)
{
    apt_bool_t processed = FALSE;
    mrcp_message_t *response = mrcp_response_create(request, request->pool);
    
    switch (request->start_line.method_id) {
        case RECOGNIZER_SET_PARAMS:
            processed = websocket_recog_channel_set_params(channel, request, response);
            break;
        case RECOGNIZER_GET_PARAMS:
            processed = websocket_recog_channel_get_params(channel, request, response);
            break;
        case RECOGNIZER_DEFINE_GRAMMAR:
            /* Grammar handling - just accept for now */
            mrcp_engine_channel_message_send(channel, response);
            processed = TRUE;
            break;
        case RECOGNIZER_RECOGNIZE:
            processed = websocket_recog_channel_recognize(channel, request, response);
            break;
        case RECOGNIZER_START_INPUT_TIMERS:
            processed = websocket_recog_channel_timers_start(channel, request, response);
            break;
        case RECOGNIZER_STOP:
            processed = websocket_recog_channel_stop(channel, request, response);
            break;
        default:
            break;
    }
    
    if (processed == FALSE) {
        mrcp_engine_channel_message_send(channel, response);
    }
    return TRUE;
}

/*******************************************************************************
 * Background Task Message Processing
 ******************************************************************************/

static apt_bool_t websocket_recog_msg_signal(
    websocket_recog_msg_type_e type,
    mrcp_engine_channel_t *channel,
    mrcp_message_t *request)
{
    websocket_recog_channel_t *recog_channel = channel->method_obj;
    websocket_recog_engine_t *recog_engine = recog_channel->recog_engine;
    apt_task_t *task = apt_consumer_task_base_get(recog_engine->task);
    apt_task_msg_t *msg = apt_task_msg_get(task);
    
    if (msg) {
        websocket_recog_msg_t *recog_msg;
        msg->type = TASK_MSG_USER;
        recog_msg = (websocket_recog_msg_t*)msg->data;

        recog_msg->type = type;
        recog_msg->channel = channel;
        recog_msg->request = request;
        recog_msg->audio_data = NULL;
        recog_msg->audio_len = 0;
        return apt_task_msg_signal(task, msg);
    }
    return FALSE;
}

static apt_bool_t websocket_recog_msg_signal_audio(
    mrcp_engine_channel_t *channel,
    const char *data,
    apr_size_t len)
{
    websocket_recog_channel_t *recog_channel = channel->method_obj;
    websocket_recog_engine_t *recog_engine = recog_channel->recog_engine;
    apt_task_t *task = apt_consumer_task_base_get(recog_engine->task);
    apt_task_msg_t *msg = apt_task_msg_get(task);
    
    if (msg) {
        websocket_recog_msg_t *recog_msg;
        msg->type = TASK_MSG_USER;
        recog_msg = (websocket_recog_msg_t*)msg->data;

        recog_msg->type = WEBSOCKET_RECOG_MSG_STREAM_AUDIO;
        recog_msg->channel = channel;
        recog_msg->request = NULL;
        recog_msg->audio_data = data;
        recog_msg->audio_len = len;
        return apt_task_msg_signal(task, msg);
    }
    return FALSE;
}

static apt_bool_t websocket_recog_msg_process(apt_task_t *task, apt_task_msg_t *msg)
{
    websocket_recog_msg_t *recog_msg = (websocket_recog_msg_t*)msg->data;
    websocket_recog_channel_t *recog_channel = recog_msg->channel->method_obj;
    
    switch (recog_msg->type) {
        case WEBSOCKET_RECOG_MSG_OPEN_CHANNEL:
            mrcp_engine_channel_open_respond(recog_msg->channel, TRUE);
            break;
            
        case WEBSOCKET_RECOG_MSG_CLOSE_CHANNEL:
            if (recog_channel->ws_client) {
                ws_client_disconnect(recog_channel->ws_client, TRUE);
            }
            mrcp_engine_channel_close_respond(recog_msg->channel);
            break;
        
        case WEBSOCKET_RECOG_MSG_REQUEST_PROCESS:
            websocket_recog_channel_request_dispatch(recog_msg->channel, recog_msg->request);
            break;
            
        case WEBSOCKET_RECOG_MSG_SEND_AUDIO:
        {
            /* Send all buffered audio to ASR server */
            apr_thread_mutex_lock(recog_channel->mutex);
            apr_size_t audio_len = recog_channel->audio_buffer_pos;
            apr_thread_mutex_unlock(recog_channel->mutex);
            
            if (audio_len > 0 && ws_client_is_connected(recog_channel->ws_client)) {
                apt_log(RECOG_LOG_MARK, APT_PRIO_INFO, 
                    "Sending audio to ASR: %zu bytes", audio_len);
                
                if (ws_client_send_binary(recog_channel->ws_client, 
                                          recog_channel->audio_buffer, audio_len)) {
                    /* Wait for recognition result */
                    recog_channel->waiting_result = TRUE;
                    websocket_recog_msg_signal(WEBSOCKET_RECOG_MSG_RECV_RESULT, 
                        recog_msg->channel, NULL);
                } else {
                    apt_log(RECOG_LOG_MARK, APT_PRIO_ERROR, "Failed to send audio");
                    websocket_recog_recognition_complete(recog_channel, 
                        RECOGNIZER_COMPLETION_CAUSE_ERROR, NULL);
                }
            } else {
                websocket_recog_recognition_complete(recog_channel, 
                    RECOGNIZER_COMPLETION_CAUSE_SUCCESS, NULL);
            }
            
            /* Clear buffer */
            apr_thread_mutex_lock(recog_channel->mutex);
            recog_channel->audio_buffer_pos = 0;
            recog_channel->stream_pos = 0;
            apr_thread_mutex_unlock(recog_channel->mutex);
            break;
        }
        
        case WEBSOCKET_RECOG_MSG_STREAM_AUDIO:
        {
            /* Stream audio chunk to ASR server */
            if (recog_msg->audio_data && recog_msg->audio_len > 0 &&
                ws_client_is_connected(recog_channel->ws_client)) {
                ws_client_send_binary(recog_channel->ws_client, 
                    recog_msg->audio_data, recog_msg->audio_len);
            }
            break;
        }
        
        case WEBSOCKET_RECOG_MSG_RECV_RESULT:
        {
            /* Poll for recognition result */
            ws_frame_t frame;
            
            if (!recog_channel->waiting_result || !recog_channel->recog_request) {
                break;
            }
            
            /* Check timeout */
            if (apr_time_now() - recog_channel->recognize_start_time > MAX_RECOGNIZE_DURATION) {
                apt_log(RECOG_LOG_MARK, APT_PRIO_WARNING, "Recognition timeout");
                websocket_recog_recognition_complete(recog_channel, 
                    RECOGNIZER_COMPLETION_CAUSE_ERROR, NULL);
                break;
            }
            
            /* Try to receive result */
            if (ws_client_receive_frame(recog_channel->ws_client, &frame)) {
                if (frame.opcode == WS_OPCODE_TEXT && frame.payload_len > 0) {
                    apt_log(RECOG_LOG_MARK, APT_PRIO_INFO, 
                        "Recognition result: %s", frame.payload);
                    websocket_recog_recognition_complete(recog_channel, 
                        RECOGNIZER_COMPLETION_CAUSE_SUCCESS, frame.payload);
                    break;
                } else if (frame.opcode == WS_OPCODE_CLOSE) {
                    websocket_recog_recognition_complete(recog_channel, 
                        RECOGNIZER_COMPLETION_CAUSE_ERROR, NULL);
                    break;
                }
            }
            
            /* Continue polling */
            if (recog_channel->waiting_result) {
                websocket_recog_msg_signal(WEBSOCKET_RECOG_MSG_RECV_RESULT, 
                    recog_msg->channel, NULL);
            }
            break;
        }
        
        default:
            break;
    }
    
    return TRUE;
}
