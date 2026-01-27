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

#include "mrcp_synth_engine.h"
#include "apt_consumer_task.h"
#include "apt_log.h"
#include "ws_client.h"
#include <apr_strings.h>
#include <apr_thread_mutex.h>
#include <string.h>

#define WEBSOCKET_SYNTH_ENGINE_TASK_NAME "WebSocket Synth Engine"

/*******************************************************************************
 * Configuration Constants
 ******************************************************************************/

/** Audio buffer configuration */
#define AUDIO_BUFFER_SIZE       (2 * 1024 * 1024)  /* 2MB audio buffer */
#define AUDIO_CHUNK_SIZE        (320)               /* 20ms at 8kHz, 16-bit */

/** Timing configuration */
#define RECV_POLL_INTERVAL      (10000)             /* 10ms polling interval */
#define MAX_SPEAK_DURATION      (300 * 1000000)     /* 5 minutes max */
#define MAX_IDLE_POLLS          (500)               /* 5 seconds of idle before error */

/*******************************************************************************
 * Type Definitions
 ******************************************************************************/

typedef struct websocket_synth_engine_t websocket_synth_engine_t;
typedef struct websocket_synth_channel_t websocket_synth_channel_t;
typedef struct websocket_synth_msg_t websocket_synth_msg_t;

/** Message types for background task */
typedef enum {
    WEBSOCKET_SYNTH_MSG_OPEN_CHANNEL,
    WEBSOCKET_SYNTH_MSG_CLOSE_CHANNEL,
    WEBSOCKET_SYNTH_MSG_REQUEST_PROCESS,
    WEBSOCKET_SYNTH_MSG_SPEAK_START,      /* Start TTS synthesis */
    WEBSOCKET_SYNTH_MSG_RECV_POLL         /* Poll for audio data (non-blocking) */
} websocket_synth_msg_type_e;

/** Declaration of websocket synthesizer engine */
struct websocket_synth_engine_t {
    apt_consumer_task_t *task;
    apr_pool_t *pool;
};

/** Declaration of websocket synthesizer channel */
struct websocket_synth_channel_t {
    /** Back pointer to engine */
    websocket_synth_engine_t *websocket_engine;
    /** Engine channel base */
    mrcp_engine_channel_t    *channel;

    /** Active (in-progress) speak request */
    mrcp_message_t           *speak_request;
    /** Pending stop response */
    mrcp_message_t           *stop_response;
    /** Is paused */
    apt_bool_t                paused;
    /** Is receiving audio */
    apt_bool_t                receiving;
    
    /** WebSocket client (shared library) */
    ws_client_t              *ws_client;
    
    /** Audio buffer for received TTS audio */
    char                     *audio_buffer;
    apr_size_t                audio_buffer_size;
    apr_size_t                audio_buffer_write_pos;  /* Write position (filled by WebSocket) */
    apr_size_t                audio_buffer_read_pos;   /* Read position (consumed by stream) */
    apt_bool_t                audio_complete;          /* TTS generation complete flag */
    
    /** Mutex for thread-safe buffer access */
    apr_thread_mutex_t       *mutex;
    
    /** Current codec descriptor */
    const mpf_codec_descriptor_t *codec_descriptor;
    
    /** Timing for receive polling */
    apr_time_t                speak_start_time;
    int                       idle_poll_count;
};

/** Declaration of websocket synthesizer task message */
struct websocket_synth_msg_t {
    websocket_synth_msg_type_e type;
    mrcp_engine_channel_t     *channel;
    mrcp_message_t            *request;
};

/*******************************************************************************
 * Function Declarations
 ******************************************************************************/

/** Engine methods */
static apt_bool_t websocket_synth_engine_destroy(mrcp_engine_t *engine);
static apt_bool_t websocket_synth_engine_open(mrcp_engine_t *engine);
static apt_bool_t websocket_synth_engine_close(mrcp_engine_t *engine);
static mrcp_engine_channel_t* websocket_synth_engine_channel_create(mrcp_engine_t *engine, apr_pool_t *pool);

static const struct mrcp_engine_method_vtable_t engine_vtable = {
    websocket_synth_engine_destroy,
    websocket_synth_engine_open,
    websocket_synth_engine_close,
    websocket_synth_engine_channel_create
};

/** Channel methods */
static apt_bool_t websocket_synth_channel_destroy(mrcp_engine_channel_t *channel);
static apt_bool_t websocket_synth_channel_open(mrcp_engine_channel_t *channel);
static apt_bool_t websocket_synth_channel_close(mrcp_engine_channel_t *channel);
static apt_bool_t websocket_synth_channel_request_process(mrcp_engine_channel_t *channel, mrcp_message_t *request);

static const struct mrcp_engine_channel_method_vtable_t channel_vtable = {
    websocket_synth_channel_destroy,
    websocket_synth_channel_open,
    websocket_synth_channel_close,
    websocket_synth_channel_request_process
};

/** Audio stream methods */
static apt_bool_t websocket_synth_stream_destroy(mpf_audio_stream_t *stream);
static apt_bool_t websocket_synth_stream_open(mpf_audio_stream_t *stream, mpf_codec_t *codec);
static apt_bool_t websocket_synth_stream_close(mpf_audio_stream_t *stream);
static apt_bool_t websocket_synth_stream_read(mpf_audio_stream_t *stream, mpf_frame_t *frame);

static const mpf_audio_stream_vtable_t audio_stream_vtable = {
    websocket_synth_stream_destroy,
    websocket_synth_stream_open,
    websocket_synth_stream_close,
    websocket_synth_stream_read,
    NULL, NULL, NULL, NULL
};

/** Internal functions */
static apt_bool_t websocket_synth_msg_signal(websocket_synth_msg_type_e type, mrcp_engine_channel_t *channel, mrcp_message_t *request);
static apt_bool_t websocket_synth_msg_process(apt_task_t *task, apt_task_msg_t *msg);
static apt_bool_t websocket_synth_channel_request_dispatch(mrcp_engine_channel_t *channel, mrcp_message_t *request);

/*******************************************************************************
 * Plugin Declaration
 ******************************************************************************/

/** Declare this macro to set plugin version */
MRCP_PLUGIN_VERSION_DECLARE

/** Declare log source */
MRCP_PLUGIN_LOG_SOURCE_IMPLEMENT(WEBSOCKET_SYNTH_PLUGIN, "WEBSOCKET-SYNTH-PLUGIN")

/** Use custom log source mark */
#define SYNTH_LOG_MARK APT_LOG_MARK_DECLARE(WEBSOCKET_SYNTH_PLUGIN)

/*******************************************************************************
 * Engine Implementation
 ******************************************************************************/

/** Create websocket synthesizer engine */
MRCP_PLUGIN_DECLARE(mrcp_engine_t*) mrcp_plugin_create(apr_pool_t *pool)
{
    websocket_synth_engine_t *synth_engine;
    apt_task_t *task;
    apt_task_vtable_t *vtable;
    apt_task_msg_pool_t *msg_pool;

    apt_log(SYNTH_LOG_MARK, APT_PRIO_INFO, "Create WebSocket Synth Engine");

    synth_engine = apr_palloc(pool, sizeof(websocket_synth_engine_t));
    if (!synth_engine) {
        apt_log(SYNTH_LOG_MARK, APT_PRIO_ERROR, "Failed to allocate engine");
        return NULL;
    }
    
    synth_engine->pool = pool;

    msg_pool = apt_task_msg_pool_create_dynamic(sizeof(websocket_synth_msg_t), pool);
    synth_engine->task = apt_consumer_task_create(synth_engine, msg_pool, pool);
    if (!synth_engine->task) {
        apt_log(SYNTH_LOG_MARK, APT_PRIO_ERROR, "Failed to create consumer task");
        return NULL;
    }
    
    task = apt_consumer_task_base_get(synth_engine->task);
    apt_task_name_set(task, WEBSOCKET_SYNTH_ENGINE_TASK_NAME);
    vtable = apt_task_vtable_get(task);
    if (vtable) {
        vtable->process_msg = websocket_synth_msg_process;
    }

    return mrcp_engine_create(
        MRCP_SYNTHESIZER_RESOURCE,
        synth_engine,
        &engine_vtable,
        pool);
}

/** Destroy synthesizer engine */
static apt_bool_t websocket_synth_engine_destroy(mrcp_engine_t *engine)
{
    websocket_synth_engine_t *synth_engine = engine->obj;
    apt_log(SYNTH_LOG_MARK, APT_PRIO_INFO, "Destroy WebSocket Synth Engine");
    
    if (synth_engine->task) {
        apt_task_t *task = apt_consumer_task_base_get(synth_engine->task);
        apt_task_destroy(task);
        synth_engine->task = NULL;
    }
    return TRUE;
}

/** Open synthesizer engine */
static apt_bool_t websocket_synth_engine_open(mrcp_engine_t *engine)
{
    websocket_synth_engine_t *synth_engine = engine->obj;
    apt_log(SYNTH_LOG_MARK, APT_PRIO_INFO, "Open WebSocket Synth Engine");
    
    if (synth_engine->task) {
        apt_task_t *task = apt_consumer_task_base_get(synth_engine->task);
        apt_task_start(task);
    }
    return mrcp_engine_open_respond(engine, TRUE);
}

/** Close synthesizer engine */
static apt_bool_t websocket_synth_engine_close(mrcp_engine_t *engine)
{
    websocket_synth_engine_t *synth_engine = engine->obj;
    apt_log(SYNTH_LOG_MARK, APT_PRIO_INFO, "Close WebSocket Synth Engine");
    
    if (synth_engine->task) {
        apt_task_t *task = apt_consumer_task_base_get(synth_engine->task);
        apt_task_terminate(task, TRUE);
    }
    return mrcp_engine_close_respond(engine);
}

/*******************************************************************************
 * Channel Implementation
 ******************************************************************************/

/** Create websocket synthesizer channel */
static mrcp_engine_channel_t* websocket_synth_engine_channel_create(mrcp_engine_t *engine, apr_pool_t *pool)
{
    mpf_stream_capabilities_t *capabilities;
    mpf_termination_t *termination;
    websocket_synth_channel_t *synth_channel;
    ws_client_config_t ws_config;
    const char *ws_host;
    const char *ws_port_str;
    const char *ws_path;
    const char *max_audio_size_str;

    apt_log(SYNTH_LOG_MARK, APT_PRIO_INFO, "Create WebSocket Synth Channel");

    /* Allocate channel */
    synth_channel = apr_palloc(pool, sizeof(websocket_synth_channel_t));
    if (!synth_channel) {
        apt_log(SYNTH_LOG_MARK, APT_PRIO_ERROR, "Failed to allocate channel");
        return NULL;
    }
    
    synth_channel->websocket_engine = engine->obj;
    synth_channel->speak_request = NULL;
    synth_channel->stop_response = NULL;
    synth_channel->paused = FALSE;
    synth_channel->receiving = FALSE;
    synth_channel->ws_client = NULL;
    synth_channel->audio_complete = FALSE;
    synth_channel->codec_descriptor = NULL;
    synth_channel->speak_start_time = 0;
    synth_channel->idle_poll_count = 0;

    /* Initialize audio buffer */
    synth_channel->audio_buffer_size = AUDIO_BUFFER_SIZE;
    
    /* Check for custom max audio size */
    max_audio_size_str = mrcp_engine_param_get(engine, "max-audio-size");
    if (max_audio_size_str) {
        apr_size_t custom_size = (apr_size_t)apr_atoi64(max_audio_size_str);
        if (custom_size > 0 && custom_size <= 50 * 1024 * 1024) {  /* Max 50MB */
            synth_channel->audio_buffer_size = custom_size;
        }
    }
    
    synth_channel->audio_buffer = apr_palloc(pool, synth_channel->audio_buffer_size);
    if (!synth_channel->audio_buffer) {
        apt_log(SYNTH_LOG_MARK, APT_PRIO_ERROR, "Failed to allocate audio buffer");
        return NULL;
    }
    synth_channel->audio_buffer_write_pos = 0;
    synth_channel->audio_buffer_read_pos = 0;

    /* Create mutex */
    if (apr_thread_mutex_create(&synth_channel->mutex, APR_THREAD_MUTEX_DEFAULT, pool) != APR_SUCCESS) {
        apt_log(SYNTH_LOG_MARK, APT_PRIO_ERROR, "Failed to create mutex");
        return NULL;
    }

    /* Get WebSocket configuration */
    ws_client_config_init(&ws_config);
    
    ws_host = mrcp_engine_param_get(engine, "ws-host");
    ws_port_str = mrcp_engine_param_get(engine, "ws-port");
    ws_path = mrcp_engine_param_get(engine, "ws-path");

    ws_config.host = ws_host ? ws_host : "localhost";
    ws_config.port = ws_port_str ? atoi(ws_port_str) : 8080;
    ws_config.path = ws_path ? ws_path : "/tts";
    ws_config.recv_timeout = RECV_POLL_INTERVAL;
    ws_config.max_frame_size = synth_channel->audio_buffer_size;

    apt_log(SYNTH_LOG_MARK, APT_PRIO_INFO, 
        "WebSocket Config: host=%s port=%d path=%s buffer_size=%zu",
        ws_config.host, ws_config.port, ws_config.path, synth_channel->audio_buffer_size);

    /* Create WebSocket client */
    synth_channel->ws_client = ws_client_create(pool, &ws_config);
    if (!synth_channel->ws_client) {
        apt_log(SYNTH_LOG_MARK, APT_PRIO_ERROR, "Failed to create WebSocket client");
        return NULL;
    }

    /* Create source stream capabilities (TTS outputs audio) */
    capabilities = mpf_source_stream_capabilities_create(pool);
    mpf_codec_capabilities_add(
        &capabilities->codecs,
        MPF_SAMPLE_RATE_8000 | MPF_SAMPLE_RATE_16000,
        "LPCM");

    /* Create media termination */
    termination = mrcp_engine_audio_termination_create(
        synth_channel,
        &audio_stream_vtable,
        capabilities,
        pool);

    /* Create engine channel base */
    synth_channel->channel = mrcp_engine_channel_create(
        engine,
        &channel_vtable,
        synth_channel,
        termination,
        pool);

    return synth_channel->channel;
}

/** Destroy engine channel */
static apt_bool_t websocket_synth_channel_destroy(mrcp_engine_channel_t *channel)
{
    websocket_synth_channel_t *synth_channel = channel->method_obj;
    apt_log(SYNTH_LOG_MARK, APT_PRIO_INFO, "Destroy WebSocket Synth Channel");
    
    if (synth_channel->ws_client) {
        ws_client_destroy(synth_channel->ws_client);
        synth_channel->ws_client = NULL;
    }
    if (synth_channel->mutex) {
        apr_thread_mutex_destroy(synth_channel->mutex);
        synth_channel->mutex = NULL;
    }
    return TRUE;
}

/** Open engine channel */
static apt_bool_t websocket_synth_channel_open(mrcp_engine_channel_t *channel)
{
    apt_log(SYNTH_LOG_MARK, APT_PRIO_INFO, "Open WebSocket Synth Channel");
    
    if (channel->attribs) {
        const apr_array_header_t *header = apr_table_elts(channel->attribs);
        apr_table_entry_t *entry = (apr_table_entry_t*)header->elts;
        int i;
        for (i = 0; i < header->nelts; i++) {
            apt_log(SYNTH_LOG_MARK, APT_PRIO_DEBUG, 
                "Attrib: %s=%s", entry[i].key, entry[i].val);
        }
    }

    return websocket_synth_msg_signal(WEBSOCKET_SYNTH_MSG_OPEN_CHANNEL, channel, NULL);
}

/** Close engine channel */
static apt_bool_t websocket_synth_channel_close(mrcp_engine_channel_t *channel)
{
    apt_log(SYNTH_LOG_MARK, APT_PRIO_INFO, "Close WebSocket Synth Channel");
    return websocket_synth_msg_signal(WEBSOCKET_SYNTH_MSG_CLOSE_CHANNEL, channel, NULL);
}

/** Process MRCP channel request */
static apt_bool_t websocket_synth_channel_request_process(mrcp_engine_channel_t *channel, mrcp_message_t *request)
{
    return websocket_synth_msg_signal(WEBSOCKET_SYNTH_MSG_REQUEST_PROCESS, channel, request);
}

/*******************************************************************************
 * Audio Stream Implementation
 ******************************************************************************/

static apt_bool_t websocket_synth_stream_destroy(mpf_audio_stream_t *stream)
{
    return TRUE;
}

static apt_bool_t websocket_synth_stream_open(mpf_audio_stream_t *stream, mpf_codec_t *codec)
{
    return TRUE;
}

static apt_bool_t websocket_synth_stream_close(mpf_audio_stream_t *stream)
{
    return TRUE;
}

/** Raise SPEAK-COMPLETE event */
static apt_bool_t websocket_synth_speak_complete(
    websocket_synth_channel_t *synth_channel,
    mrcp_synth_completion_cause_e cause)
{
    mrcp_message_t *message;
    mrcp_synth_header_t *synth_header;

    if (!synth_channel->speak_request) {
        return FALSE;
    }

    /* Create SPEAK-COMPLETE event */
    message = mrcp_event_create(
        synth_channel->speak_request,
        SYNTHESIZER_SPEAK_COMPLETE,
        synth_channel->speak_request->pool);
    if (!message) {
        return FALSE;
    }

    /* Set completion cause */
    synth_header = mrcp_resource_header_prepare(message);
    if (synth_header) {
        synth_header->completion_cause = cause;
        mrcp_resource_header_property_add(message, SYNTHESIZER_HEADER_COMPLETION_CAUSE);
    }

    message->start_line.request_state = MRCP_REQUEST_STATE_COMPLETE;
    synth_channel->speak_request = NULL;
    synth_channel->receiving = FALSE;

    apt_log(SYNTH_LOG_MARK, APT_PRIO_INFO, "SPEAK-COMPLETE: cause=%d", cause);

    return mrcp_engine_channel_message_send(synth_channel->channel, message);
}

/** Read audio frame (called from MPF engine context) */
static apt_bool_t websocket_synth_stream_read(mpf_audio_stream_t *stream, mpf_frame_t *frame)
{
    websocket_synth_channel_t *synth_channel = stream->obj;
    apr_size_t available;
    apr_size_t frame_size = frame->codec_frame.size;

    /* Check if STOP was requested */
    if (synth_channel->stop_response) {
        mrcp_engine_channel_message_send(synth_channel->channel, synth_channel->stop_response);
        synth_channel->stop_response = NULL;
        synth_channel->speak_request = NULL;
        synth_channel->paused = FALSE;
        synth_channel->receiving = FALSE;
        
        apr_thread_mutex_lock(synth_channel->mutex);
        synth_channel->audio_buffer_write_pos = 0;
        synth_channel->audio_buffer_read_pos = 0;
        synth_channel->audio_complete = FALSE;
        apr_thread_mutex_unlock(synth_channel->mutex);
        
        return TRUE;
    }

    /* Check if there is active SPEAK request and not paused */
    if (synth_channel->speak_request && !synth_channel->paused) {
        apr_thread_mutex_lock(synth_channel->mutex);
        
        available = synth_channel->audio_buffer_write_pos - synth_channel->audio_buffer_read_pos;
        
        if (available >= frame_size) {
            /* Read audio data from buffer */
            memcpy(frame->codec_frame.buffer,
                synth_channel->audio_buffer + synth_channel->audio_buffer_read_pos,
                frame_size);
            synth_channel->audio_buffer_read_pos += frame_size;
            frame->type |= MEDIA_FRAME_TYPE_AUDIO;
        } else if (synth_channel->audio_complete && available == 0) {
            /* TTS complete and buffer empty - send SPEAK-COMPLETE */
            apr_thread_mutex_unlock(synth_channel->mutex);
            websocket_synth_speak_complete(synth_channel, SYNTHESIZER_COMPLETION_CAUSE_NORMAL);
            return TRUE;
        } else if (synth_channel->audio_complete && available > 0) {
            /* Send remaining audio (partial frame, pad with silence) */
            memcpy(frame->codec_frame.buffer,
                synth_channel->audio_buffer + synth_channel->audio_buffer_read_pos,
                available);
            memset((char*)frame->codec_frame.buffer + available, 0, frame_size - available);
            synth_channel->audio_buffer_read_pos += available;
            frame->type |= MEDIA_FRAME_TYPE_AUDIO;
        } else {
            /* Buffer underrun - fill with silence, waiting for more data */
            memset(frame->codec_frame.buffer, 0, frame_size);
            frame->type |= MEDIA_FRAME_TYPE_AUDIO;
        }
        
        apr_thread_mutex_unlock(synth_channel->mutex);
    }

    return TRUE;
}

/*******************************************************************************
 * TTS Request Processing
 ******************************************************************************/

/** Build TTS request JSON */
static char* websocket_synth_build_request_json(
    websocket_synth_channel_t *synth_channel,
    mrcp_message_t *request,
    apr_pool_t *pool)
{
    mrcp_synth_header_t *synth_header;
    const char *text = NULL;
    const char *voice_name = "default";
    float speed = 1.0f;
    float pitch = 1.0f;
    float volume = 1.0f;
    int sample_rate = 8000;
    const char *session_id = "";
    char *json;

    /* Get text content */
    if (request->body.length > 0) {
        text = request->body.buf;
    }

    if (!text || strlen(text) == 0) {
        apt_log(SYNTH_LOG_MARK, APT_PRIO_WARNING, "Empty text in SPEAK request");
        return NULL;
    }

    /* Get synthesizer header parameters */
    synth_header = mrcp_resource_header_get(request);
    if (synth_header) {
        if (mrcp_resource_header_property_check(request, SYNTHESIZER_HEADER_VOICE_NAME) == TRUE) {
            if (synth_header->voice_param.name.buf) {
                voice_name = synth_header->voice_param.name.buf;
            }
        }
        if (mrcp_resource_header_property_check(request, SYNTHESIZER_HEADER_PROSODY_RATE) == TRUE) {
            speed = synth_header->prosody_param.rate.value.relative;
        }
        if (mrcp_resource_header_property_check(request, SYNTHESIZER_HEADER_PROSODY_PITCH) == TRUE) {
            pitch = synth_header->prosody_param.pitch.value.relative;
        }
        if (mrcp_resource_header_property_check(request, SYNTHESIZER_HEADER_PROSODY_VOLUME) == TRUE) {
            volume = synth_header->prosody_param.volume.value.relative;
        }
    }

    /* Get sample rate */
    if (synth_channel->codec_descriptor) {
        sample_rate = synth_channel->codec_descriptor->sampling_rate;
    }

    /* Get session ID */
    if (request->channel_id.session_id.buf) {
        session_id = request->channel_id.session_id.buf;
    }

    /* Build JSON with escaped strings */
    {
        const char *escaped_text = ws_json_escape_string(text, pool);
        const char *escaped_voice = ws_json_escape_string(voice_name, pool);
        const char *escaped_session = ws_json_escape_string(session_id, pool);
        
        if (!escaped_text || !escaped_voice || !escaped_session) {
            apt_log(SYNTH_LOG_MARK, APT_PRIO_ERROR, "Failed to escape JSON strings");
            return NULL;
        }
        
        json = apr_psprintf(pool,
            "{"
            "\"action\":\"tts\","
            "\"text\":\"%s\","
            "\"voice\":\"%s\","
            "\"speed\":%.2f,"
            "\"pitch\":%.2f,"
            "\"volume\":%.2f,"
            "\"sample_rate\":%d,"
            "\"format\":\"pcm\","
            "\"session_id\":\"%s\""
            "}",
            escaped_text, escaped_voice, speed, pitch, volume, sample_rate, escaped_session);
    }

    apt_log(SYNTH_LOG_MARK, APT_PRIO_DEBUG, "TTS Request: %s", json);
    return json;
}

/** Process SPEAK request */
static apt_bool_t websocket_synth_channel_speak(
    mrcp_engine_channel_t *channel,
    mrcp_message_t *request,
    mrcp_message_t *response)
{
    websocket_synth_channel_t *synth_channel = channel->method_obj;
    const mpf_codec_descriptor_t *descriptor = mrcp_engine_source_stream_codec_get(channel);

    if (!descriptor) {
        apt_log(SYNTH_LOG_MARK, APT_PRIO_WARNING, 
            "Failed to get codec descriptor " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
        response->start_line.status_code = MRCP_STATUS_CODE_METHOD_FAILED;
        return FALSE;
    }

    synth_channel->codec_descriptor = descriptor;

    apt_log(SYNTH_LOG_MARK, APT_PRIO_INFO, 
        "SPEAK: sample_rate=%d " APT_SIDRES_FMT,
        descriptor->sampling_rate, MRCP_MESSAGE_SIDRES(request));

    /* Reset audio buffer */
    apr_thread_mutex_lock(synth_channel->mutex);
    synth_channel->audio_buffer_write_pos = 0;
    synth_channel->audio_buffer_read_pos = 0;
    synth_channel->audio_complete = FALSE;
    synth_channel->paused = FALSE;
    synth_channel->receiving = TRUE;
    synth_channel->speak_start_time = apr_time_now();
    synth_channel->idle_poll_count = 0;
    apr_thread_mutex_unlock(synth_channel->mutex);

    /* Send response */
    response->start_line.request_state = MRCP_REQUEST_STATE_INPROGRESS;
    mrcp_engine_channel_message_send(channel, response);
    
    /* Store speak request */
    synth_channel->speak_request = request;

    /* Signal background task to start TTS */
    return websocket_synth_msg_signal(WEBSOCKET_SYNTH_MSG_SPEAK_START, channel, request);
}

/** Process STOP request */
static apt_bool_t websocket_synth_channel_stop(
    mrcp_engine_channel_t *channel,
    mrcp_message_t *request,
    mrcp_message_t *response)
{
    websocket_synth_channel_t *synth_channel = channel->method_obj;
    apt_log(SYNTH_LOG_MARK, APT_PRIO_INFO, "STOP Request");
    
    synth_channel->stop_response = response;
    synth_channel->receiving = FALSE;
    return TRUE;
}

/** Process PAUSE request */
static apt_bool_t websocket_synth_channel_pause(
    mrcp_engine_channel_t *channel,
    mrcp_message_t *request,
    mrcp_message_t *response)
{
    websocket_synth_channel_t *synth_channel = channel->method_obj;
    apt_log(SYNTH_LOG_MARK, APT_PRIO_INFO, "PAUSE Request");
    
    synth_channel->paused = TRUE;
    mrcp_engine_channel_message_send(channel, response);
    return TRUE;
}

/** Process RESUME request */
static apt_bool_t websocket_synth_channel_resume(
    mrcp_engine_channel_t *channel,
    mrcp_message_t *request,
    mrcp_message_t *response)
{
    websocket_synth_channel_t *synth_channel = channel->method_obj;
    apt_log(SYNTH_LOG_MARK, APT_PRIO_INFO, "RESUME Request");
    
    synth_channel->paused = FALSE;
    mrcp_engine_channel_message_send(channel, response);
    return TRUE;
}

/** Process SET-PARAMS request */
static apt_bool_t websocket_synth_channel_set_params(
    mrcp_engine_channel_t *channel,
    mrcp_message_t *request,
    mrcp_message_t *response)
{
    mrcp_synth_header_t *synth_header = mrcp_resource_header_get(request);
    
    if (synth_header) {
        if (mrcp_resource_header_property_check(request, SYNTHESIZER_HEADER_VOICE_NAME) == TRUE) {
            apt_log(SYNTH_LOG_MARK, APT_PRIO_INFO, 
                "Set Voice: %s", synth_header->voice_param.name.buf);
        }
    }

    mrcp_engine_channel_message_send(channel, response);
    return TRUE;
}

/** Process GET-PARAMS request */
static apt_bool_t websocket_synth_channel_get_params(
    mrcp_engine_channel_t *channel,
    mrcp_message_t *request,
    mrcp_message_t *response)
{
    mrcp_synth_header_t *req_synth_header = mrcp_resource_header_get(request);
    
    if (req_synth_header) {
        mrcp_synth_header_t *res_synth_header = mrcp_resource_header_prepare(response);
        if (mrcp_resource_header_property_check(request, SYNTHESIZER_HEADER_VOICE_NAME) == TRUE) {
            apt_string_set(&res_synth_header->voice_param.name, "websocket-tts");
            mrcp_resource_header_property_add(response, SYNTHESIZER_HEADER_VOICE_NAME);
        }
    }

    mrcp_engine_channel_message_send(channel, response);
    return TRUE;
}

/** Dispatch MRCP request */
static apt_bool_t websocket_synth_channel_request_dispatch(mrcp_engine_channel_t *channel, mrcp_message_t *request)
{
    apt_bool_t processed = FALSE;
    mrcp_message_t *response = mrcp_response_create(request, request->pool);
    
    switch (request->start_line.method_id) {
        case SYNTHESIZER_SET_PARAMS:
            processed = websocket_synth_channel_set_params(channel, request, response);
            break;
        case SYNTHESIZER_GET_PARAMS:
            processed = websocket_synth_channel_get_params(channel, request, response);
            break;
        case SYNTHESIZER_SPEAK:
            processed = websocket_synth_channel_speak(channel, request, response);
            break;
        case SYNTHESIZER_STOP:
            processed = websocket_synth_channel_stop(channel, request, response);
            break;
        case SYNTHESIZER_PAUSE:
            processed = websocket_synth_channel_pause(channel, request, response);
            break;
        case SYNTHESIZER_RESUME:
            processed = websocket_synth_channel_resume(channel, request, response);
            break;
        case SYNTHESIZER_BARGE_IN_OCCURRED:
            processed = websocket_synth_channel_stop(channel, request, response);
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

static apt_bool_t websocket_synth_msg_signal(
    websocket_synth_msg_type_e type,
    mrcp_engine_channel_t *channel,
    mrcp_message_t *request)
{
    websocket_synth_channel_t *synth_channel = channel->method_obj;
    websocket_synth_engine_t *synth_engine = synth_channel->websocket_engine;
    apt_task_t *task = apt_consumer_task_base_get(synth_engine->task);
    apt_task_msg_t *msg = apt_task_msg_get(task);
    
    if (msg) {
        websocket_synth_msg_t *synth_msg;
        msg->type = TASK_MSG_USER;
        synth_msg = (websocket_synth_msg_t*)msg->data;

        synth_msg->type = type;
        synth_msg->channel = channel;
        synth_msg->request = request;
        return apt_task_msg_signal(task, msg);
    }
    return FALSE;
}

/** Process received audio frame */
static apt_bool_t websocket_synth_process_audio_frame(
    websocket_synth_channel_t *synth_channel,
    ws_frame_t *frame)
{
    if (frame->opcode == WS_OPCODE_BINARY || frame->opcode == WS_OPCODE_CONTINUATION) {
        /* Binary frame - audio data */
        apr_thread_mutex_lock(synth_channel->mutex);
        
        apr_size_t space = synth_channel->audio_buffer_size - synth_channel->audio_buffer_write_pos;
        if (frame->payload_len <= space) {
            memcpy(synth_channel->audio_buffer + synth_channel->audio_buffer_write_pos,
                frame->payload, frame->payload_len);
            synth_channel->audio_buffer_write_pos += frame->payload_len;
            apt_log(SYNTH_LOG_MARK, APT_PRIO_DEBUG, 
                "Audio received: %zu bytes, total: %zu",
                frame->payload_len, synth_channel->audio_buffer_write_pos);
        } else {
            apt_log(SYNTH_LOG_MARK, APT_PRIO_WARNING, 
                "Audio buffer overflow, dropping %zu bytes", frame->payload_len);
        }
        
        synth_channel->idle_poll_count = 0;
        apr_thread_mutex_unlock(synth_channel->mutex);
        return TRUE;
        
    } else if (frame->opcode == WS_OPCODE_TEXT) {
        /* Text frame - status message */
        apt_log(SYNTH_LOG_MARK, APT_PRIO_DEBUG, "Text message: %s", frame->payload);
        
        if (strstr(frame->payload, "complete") || 
            strstr(frame->payload, "end") || 
            strstr(frame->payload, "done")) {
            apr_thread_mutex_lock(synth_channel->mutex);
            synth_channel->audio_complete = TRUE;
            apr_thread_mutex_unlock(synth_channel->mutex);
            apt_log(SYNTH_LOG_MARK, APT_PRIO_INFO, "TTS synthesis complete");
            return FALSE;  /* Stop polling */
        }
        return TRUE;
        
    } else if (frame->opcode == WS_OPCODE_CLOSE) {
        /* Close frame */
        apr_thread_mutex_lock(synth_channel->mutex);
        synth_channel->audio_complete = TRUE;
        apr_thread_mutex_unlock(synth_channel->mutex);
        apt_log(SYNTH_LOG_MARK, APT_PRIO_INFO, "WebSocket closed by server");
        return FALSE;
    }
    
    return TRUE;
}

static apt_bool_t websocket_synth_msg_process(apt_task_t *task, apt_task_msg_t *msg)
{
    websocket_synth_msg_t *synth_msg = (websocket_synth_msg_t*)msg->data;
    websocket_synth_channel_t *synth_channel = synth_msg->channel->method_obj;
    
    switch (synth_msg->type) {
        case WEBSOCKET_SYNTH_MSG_OPEN_CHANNEL:
            mrcp_engine_channel_open_respond(synth_msg->channel, TRUE);
            break;
            
        case WEBSOCKET_SYNTH_MSG_CLOSE_CHANNEL:
            if (synth_channel->ws_client) {
                ws_client_disconnect(synth_channel->ws_client, TRUE);
            }
            mrcp_engine_channel_close_respond(synth_msg->channel);
            break;
        
        case WEBSOCKET_SYNTH_MSG_REQUEST_PROCESS:
            websocket_synth_channel_request_dispatch(synth_msg->channel, synth_msg->request);
            break;
            
        case WEBSOCKET_SYNTH_MSG_SPEAK_START:
        {
            /* Start TTS synthesis */
            char *json_request;
            
            /* Ensure connection */
            if (!ws_client_ensure_connected(synth_channel->ws_client)) {
                apt_log(SYNTH_LOG_MARK, APT_PRIO_ERROR, "Failed to connect to TTS server");
                websocket_synth_speak_complete(synth_channel, SYNTHESIZER_COMPLETION_CAUSE_ERROR);
                break;
            }
            
            /* Build and send TTS request */
            json_request = websocket_synth_build_request_json(
                synth_channel, synth_msg->request, synth_msg->channel->pool);
            
            if (!json_request) {
                apt_log(SYNTH_LOG_MARK, APT_PRIO_ERROR, "Failed to build TTS request");
                websocket_synth_speak_complete(synth_channel, SYNTHESIZER_COMPLETION_CAUSE_ERROR);
                break;
            }
            
            if (!ws_client_send_text(synth_channel->ws_client, json_request, strlen(json_request))) {
                apt_log(SYNTH_LOG_MARK, APT_PRIO_ERROR, "Failed to send TTS request");
                websocket_synth_speak_complete(synth_channel, SYNTHESIZER_COMPLETION_CAUSE_ERROR);
                break;
            }
            
            apt_log(SYNTH_LOG_MARK, APT_PRIO_INFO, "TTS request sent, starting audio receive");
            
            /* Start non-blocking receive polling */
            websocket_synth_msg_signal(WEBSOCKET_SYNTH_MSG_RECV_POLL, synth_msg->channel, NULL);
            break;
        }
        
        case WEBSOCKET_SYNTH_MSG_RECV_POLL:
        {
            /* Non-blocking receive poll */
            ws_frame_t frame;
            apt_bool_t continue_polling = TRUE;
            
            /* Check if stopped or not receiving */
            if (synth_channel->stop_response || !synth_channel->receiving) {
                break;
            }
            
            /* Check max duration */
            if (apr_time_now() - synth_channel->speak_start_time > MAX_SPEAK_DURATION) {
                apt_log(SYNTH_LOG_MARK, APT_PRIO_WARNING, "Max speak duration exceeded");
                apr_thread_mutex_lock(synth_channel->mutex);
                synth_channel->audio_complete = TRUE;
                apr_thread_mutex_unlock(synth_channel->mutex);
                break;
            }
            
            /* Try to receive a frame (non-blocking) */
            if (ws_client_receive_frame(synth_channel->ws_client, &frame)) {
                continue_polling = websocket_synth_process_audio_frame(synth_channel, &frame);
            } else {
                /* No data or error */
                synth_channel->idle_poll_count++;
                
                if (synth_channel->idle_poll_count > MAX_IDLE_POLLS) {
                    /* Check if we have received any audio */
                    apr_thread_mutex_lock(synth_channel->mutex);
                    if (synth_channel->audio_buffer_write_pos > 0) {
                        /* Have some audio, mark as complete */
                        synth_channel->audio_complete = TRUE;
                        apt_log(SYNTH_LOG_MARK, APT_PRIO_INFO, "Idle timeout, marking complete");
                    } else {
                        /* No audio at all, error */
                        apt_log(SYNTH_LOG_MARK, APT_PRIO_ERROR, "No audio received, timeout");
                        apr_thread_mutex_unlock(synth_channel->mutex);
                        websocket_synth_speak_complete(synth_channel, SYNTHESIZER_COMPLETION_CAUSE_ERROR);
                        break;
                    }
                    apr_thread_mutex_unlock(synth_channel->mutex);
                    continue_polling = FALSE;
                }
            }
            
            /* Schedule next poll if needed */
            if (continue_polling && synth_channel->receiving && !synth_channel->stop_response) {
                websocket_synth_msg_signal(WEBSOCKET_SYNTH_MSG_RECV_POLL, synth_msg->channel, NULL);
            }
            break;
        }
        
        default:
            break;
    }
    
    return TRUE;
}
