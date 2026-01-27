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
#include <apr_strings.h>
#include <apr_network_io.h>
#include <apr_base64.h>
#include <apr_time.h>
#include <apr_uuid.h>
#include <apr_thread_mutex.h>
#include <string.h>

#define WEBSOCKET_SYNTH_ENGINE_TASK_NAME "WebSocket Synth Engine"

/* Audio buffer configuration */
#define AUDIO_BUFFER_SIZE (1024 * 1024)  /* 1MB audio buffer */
#define AUDIO_CHUNK_SIZE  (320)           /* 20ms at 8kHz, 16-bit */

typedef struct websocket_synth_engine_t websocket_synth_engine_t;
typedef struct websocket_synth_channel_t websocket_synth_channel_t;
typedef struct websocket_synth_msg_t websocket_synth_msg_t;
typedef struct websocket_client_t websocket_client_t;

/** WebSocket client structure */
struct websocket_client_t {
	apr_socket_t *socket;
	apr_sockaddr_t *sa;
	apr_pool_t *pool;
	apt_bool_t connected;
	char *host;
	int port;
	char *path;
};

/** Declaration of synthesizer engine methods */
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

/** Declaration of synthesizer channel methods */
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

/** Declaration of synthesizer audio stream methods */
static apt_bool_t websocket_synth_stream_destroy(mpf_audio_stream_t *stream);
static apt_bool_t websocket_synth_stream_open(mpf_audio_stream_t *stream, mpf_codec_t *codec);
static apt_bool_t websocket_synth_stream_close(mpf_audio_stream_t *stream);
static apt_bool_t websocket_synth_stream_read(mpf_audio_stream_t *stream, mpf_frame_t *frame);

static const mpf_audio_stream_vtable_t audio_stream_vtable = {
	websocket_synth_stream_destroy,
	websocket_synth_stream_open,
	websocket_synth_stream_close,
	websocket_synth_stream_read,
	NULL,
	NULL,
	NULL,
	NULL
};

/** Declaration of websocket synthesizer engine */
struct websocket_synth_engine_t {
	apt_consumer_task_t *task;
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
	
	/** WebSocket client */
	websocket_client_t       *ws_client;
	
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
};

typedef enum {
	WEBSOCKET_SYNTH_MSG_OPEN_CHANNEL,
	WEBSOCKET_SYNTH_MSG_CLOSE_CHANNEL,
	WEBSOCKET_SYNTH_MSG_REQUEST_PROCESS,
	WEBSOCKET_SYNTH_MSG_SPEAK_REQUEST   /* Send text to TTS service */
} websocket_synth_msg_type_e;

/** Declaration of websocket synthesizer task message */
struct websocket_synth_msg_t {
	websocket_synth_msg_type_e type;
	mrcp_engine_channel_t     *channel;
	mrcp_message_t            *request;
};

static apt_bool_t websocket_synth_msg_signal(websocket_synth_msg_type_e type, mrcp_engine_channel_t *channel, mrcp_message_t *request);
static apt_bool_t websocket_synth_msg_process(apt_task_t *task, apt_task_msg_t *msg);

/** WebSocket client functions */
static apt_bool_t websocket_client_connect(websocket_client_t *ws_client);
static apt_bool_t websocket_client_send_text(websocket_client_t *ws_client, const char *text, apr_size_t len);
static apt_bool_t websocket_client_send_binary(websocket_client_t *ws_client, const char *data, apr_size_t len);
static apt_bool_t websocket_client_receive_audio(websocket_client_t *ws_client, websocket_synth_channel_t *synth_channel);
static void websocket_client_destroy(websocket_client_t *ws_client);

/** Declare this macro to set plugin version */
MRCP_PLUGIN_VERSION_DECLARE

/**
 * Declare this macro to use log routine of the server, plugin is loaded from.
 * Enable/add the corresponding entry in logger.xml to set a custom log source priority.
 *    <source name="WEBSOCKET-SYNTH-PLUGIN" priority="DEBUG" masking="NONE"/>
 */
MRCP_PLUGIN_LOG_SOURCE_IMPLEMENT(WEBSOCKET_SYNTH_PLUGIN, "WEBSOCKET-SYNTH-PLUGIN")

/** Use custom log source mark */
#define WEBSOCKET_SYNTH_LOG_MARK APT_LOG_MARK_DECLARE(WEBSOCKET_SYNTH_PLUGIN)

/** Create websocket synthesizer engine */
MRCP_PLUGIN_DECLARE(mrcp_engine_t*) mrcp_plugin_create(apr_pool_t *pool)
{
	websocket_synth_engine_t *websocket_engine = apr_palloc(pool, sizeof(websocket_synth_engine_t));
	apt_task_t *task;
	apt_task_vtable_t *vtable;
	apt_task_msg_pool_t *msg_pool;

	apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, "Create WebSocket Synth Engine");

	msg_pool = apt_task_msg_pool_create_dynamic(sizeof(websocket_synth_msg_t), pool);
	websocket_engine->task = apt_consumer_task_create(websocket_engine, msg_pool, pool);
	if (!websocket_engine->task) {
		apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_ERROR, "Failed to Create Consumer Task");
		return NULL;
	}
	
	task = apt_consumer_task_base_get(websocket_engine->task);
	apt_task_name_set(task, WEBSOCKET_SYNTH_ENGINE_TASK_NAME);
	vtable = apt_task_vtable_get(task);
	if (vtable) {
		vtable->process_msg = websocket_synth_msg_process;
	}

	/* create engine base */
	return mrcp_engine_create(
		MRCP_SYNTHESIZER_RESOURCE,  /* MRCP resource identifier */
		websocket_engine,           /* object to associate */
		&engine_vtable,             /* virtual methods table of engine */
		pool);                      /* pool to allocate memory from */
}

/** Destroy synthesizer engine */
static apt_bool_t websocket_synth_engine_destroy(mrcp_engine_t *engine)
{
	websocket_synth_engine_t *websocket_engine = engine->obj;
	apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, "Destroy WebSocket Synth Engine");
	
	if (websocket_engine->task) {
		apt_task_t *task = apt_consumer_task_base_get(websocket_engine->task);
		apt_task_destroy(task);
		websocket_engine->task = NULL;
	}
	return TRUE;
}

/** Open synthesizer engine */
static apt_bool_t websocket_synth_engine_open(mrcp_engine_t *engine)
{
	websocket_synth_engine_t *websocket_engine = engine->obj;
	apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, "Open WebSocket Synth Engine");
	
	if (websocket_engine->task) {
		apt_task_t *task = apt_consumer_task_base_get(websocket_engine->task);
		apt_task_start(task);
	}
	return mrcp_engine_open_respond(engine, TRUE);
}

/** Close synthesizer engine */
static apt_bool_t websocket_synth_engine_close(mrcp_engine_t *engine)
{
	websocket_synth_engine_t *websocket_engine = engine->obj;
	apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, "Close WebSocket Synth Engine");
	
	if (websocket_engine->task) {
		apt_task_t *task = apt_consumer_task_base_get(websocket_engine->task);
		apt_task_terminate(task, TRUE);
	}
	return mrcp_engine_close_respond(engine);
}

/** Create websocket synthesizer channel */
static mrcp_engine_channel_t* websocket_synth_engine_channel_create(mrcp_engine_t *engine, apr_pool_t *pool)
{
	mpf_stream_capabilities_t *capabilities;
	mpf_termination_t *termination;
	const char *ws_host;
	const char *ws_port_str;
	const char *ws_path;
	int ws_port;

	apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, "Create WebSocket Synth Channel");

	/* create websocket synth channel */
	websocket_synth_channel_t *synth_channel = apr_palloc(pool, sizeof(websocket_synth_channel_t));
	synth_channel->websocket_engine = engine->obj;
	synth_channel->speak_request = NULL;
	synth_channel->stop_response = NULL;
	synth_channel->paused = FALSE;
	synth_channel->ws_client = NULL;
	synth_channel->audio_complete = FALSE;
	synth_channel->codec_descriptor = NULL;

	/* Initialize audio buffer */
	synth_channel->audio_buffer_size = AUDIO_BUFFER_SIZE;
	synth_channel->audio_buffer = apr_palloc(pool, synth_channel->audio_buffer_size);
	synth_channel->audio_buffer_write_pos = 0;
	synth_channel->audio_buffer_read_pos = 0;

	/* Create mutex for thread-safe buffer access */
	apr_thread_mutex_create(&synth_channel->mutex, APR_THREAD_MUTEX_DEFAULT, pool);

	/* Get WebSocket configuration from engine params */
	ws_host = mrcp_engine_param_get(engine, "ws-host");
	ws_port_str = mrcp_engine_param_get(engine, "ws-port");
	ws_path = mrcp_engine_param_get(engine, "ws-path");

	if (!ws_host) {
		ws_host = "localhost";
	}
	if (!ws_port_str) {
		ws_port = 8080;
	} else {
		ws_port = atoi(ws_port_str);
	}
	if (!ws_path) {
		ws_path = "/tts";
	}

	/* Create WebSocket client */
	synth_channel->ws_client = apr_palloc(pool, sizeof(websocket_client_t));
	synth_channel->ws_client->pool = pool;
	synth_channel->ws_client->host = apr_pstrdup(pool, ws_host);
	synth_channel->ws_client->port = ws_port;
	synth_channel->ws_client->path = apr_pstrdup(pool, ws_path);
	synth_channel->ws_client->socket = NULL;
	synth_channel->ws_client->sa = NULL;
	synth_channel->ws_client->connected = FALSE;

	apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, 
		"WebSocket Config: host=%s port=%d path=%s", ws_host, ws_port, ws_path);

	/* Create source stream capabilities (TTS outputs audio) */
	capabilities = mpf_source_stream_capabilities_create(pool);
	mpf_codec_capabilities_add(
		&capabilities->codecs,
		MPF_SAMPLE_RATE_8000 | MPF_SAMPLE_RATE_16000,
		"LPCM");

	/* create media termination */
	termination = mrcp_engine_audio_termination_create(
		synth_channel,        /* object to associate */
		&audio_stream_vtable, /* virtual methods table of audio stream */
		capabilities,         /* stream capabilities */
		pool);                /* pool to allocate memory from */

	/* create engine channel base */
	synth_channel->channel = mrcp_engine_channel_create(
		engine,               /* engine */
		&channel_vtable,      /* virtual methods table of engine channel */
		synth_channel,        /* object to associate */
		termination,          /* associated media termination */
		pool);                /* pool to allocate memory from */

	return synth_channel->channel;
}

/** Destroy engine channel */
static apt_bool_t websocket_synth_channel_destroy(mrcp_engine_channel_t *channel)
{
	websocket_synth_channel_t *synth_channel = channel->method_obj;
	apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, "Destroy WebSocket Synth Channel");
	
	if (synth_channel->ws_client) {
		websocket_client_destroy(synth_channel->ws_client);
	}
	if (synth_channel->mutex) {
		apr_thread_mutex_destroy(synth_channel->mutex);
		synth_channel->mutex = NULL;
	}
	return TRUE;
}

/** Open engine channel (asynchronous response MUST be sent) */
static apt_bool_t websocket_synth_channel_open(mrcp_engine_channel_t *channel)
{
	apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, "Open WebSocket Synth Channel");
	
	if (channel->attribs) {
		/* process attributes */
		const apr_array_header_t *header = apr_table_elts(channel->attribs);
		apr_table_entry_t *entry = (apr_table_entry_t*)header->elts;
		int i;
		for (i = 0; i < header->nelts; i++) {
			apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, 
				"Attrib name [%s] value [%s]", entry[i].key, entry[i].val);
		}
	}

	return websocket_synth_msg_signal(WEBSOCKET_SYNTH_MSG_OPEN_CHANNEL, channel, NULL);
}

/** Close engine channel (asynchronous response MUST be sent) */
static apt_bool_t websocket_synth_channel_close(mrcp_engine_channel_t *channel)
{
	apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, "Close WebSocket Synth Channel");
	return websocket_synth_msg_signal(WEBSOCKET_SYNTH_MSG_CLOSE_CHANNEL, channel, NULL);
}

/** Process MRCP channel request (asynchronous response MUST be sent) */
static apt_bool_t websocket_synth_channel_request_process(mrcp_engine_channel_t *channel, mrcp_message_t *request)
{
	return websocket_synth_msg_signal(WEBSOCKET_SYNTH_MSG_REQUEST_PROCESS, channel, request);
}

/*******************************************************************************
 * TTS Request/Response JSON Protocol
 * 
 * Request JSON format (sent to TTS server):
 * {
 *     "action": "tts",
 *     "text": "要合成的文本内容",
 *     "voice": "speaker_name",
 *     "speed": 1.0,
 *     "pitch": 1.0,
 *     "volume": 1.0,
 *     "sample_rate": 8000,
 *     "format": "pcm",
 *     "session_id": "unique_session_id"
 * }
 * 
 * Response: Binary audio data frames via WebSocket
 * Final message: JSON with {"status": "complete"} or audio stream end
 ******************************************************************************/

/** Escape string for JSON (handles special characters) */
static char* json_escape_string(const char *str, apr_pool_t *pool)
{
	apr_size_t len;
	apr_size_t i, j;
	char *escaped;
	apr_size_t escaped_len;
	
	if (!str) {
		return apr_pstrdup(pool, "");
	}
	
	len = strlen(str);
	/* Worst case: every character needs escaping (6 chars for \uXXXX) */
	escaped = apr_palloc(pool, len * 6 + 1);
	
	for (i = 0, j = 0; i < len; i++) {
		unsigned char c = (unsigned char)str[i];
		switch (c) {
			case '"':
				escaped[j++] = '\\';
				escaped[j++] = '"';
				break;
			case '\\':
				escaped[j++] = '\\';
				escaped[j++] = '\\';
				break;
			case '\b':
				escaped[j++] = '\\';
				escaped[j++] = 'b';
				break;
			case '\f':
				escaped[j++] = '\\';
				escaped[j++] = 'f';
				break;
			case '\n':
				escaped[j++] = '\\';
				escaped[j++] = 'n';
				break;
			case '\r':
				escaped[j++] = '\\';
				escaped[j++] = 'r';
				break;
			case '\t':
				escaped[j++] = '\\';
				escaped[j++] = 't';
				break;
			default:
				if (c < 32) {
					/* Control character - escape as \u00XX */
					j += apr_snprintf(escaped + j, 7, "\\u%04x", c);
				} else {
					escaped[j++] = c;
				}
				break;
		}
	}
	escaped[j] = '\0';
	
	return escaped;
}

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

	/* Get text content from request body */
	if (request->body.length > 0) {
		text = request->body.buf;
	}

	if (!text || strlen(text) == 0) {
		apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_WARNING, "Empty text content in SPEAK request");
		return NULL;
	}

	/* Get synthesizer header parameters */
	synth_header = mrcp_resource_header_get(request);
	if (synth_header) {
		/* Voice name */
		if (mrcp_resource_header_property_check(request, SYNTHESIZER_HEADER_VOICE_NAME) == TRUE) {
			if (synth_header->voice_param.name.buf) {
				voice_name = synth_header->voice_param.name.buf;
			}
		}
		/* Speech rate (prosody-rate) */
		if (mrcp_resource_header_property_check(request, SYNTHESIZER_HEADER_PROSODY_RATE) == TRUE) {
			/* Convert prosody rate to speed factor */
			speed = synth_header->prosody_param.rate.value.relative;
		}
		/* Prosody pitch */
		if (mrcp_resource_header_property_check(request, SYNTHESIZER_HEADER_PROSODY_PITCH) == TRUE) {
			pitch = synth_header->prosody_param.pitch.value.relative;
		}
		/* Prosody volume */
		if (mrcp_resource_header_property_check(request, SYNTHESIZER_HEADER_PROSODY_VOLUME) == TRUE) {
			volume = synth_header->prosody_param.volume.value.relative;
		}
	}

	/* Get sample rate from codec descriptor */
	if (synth_channel->codec_descriptor) {
		sample_rate = synth_channel->codec_descriptor->sampling_rate;
	}

	/* Get session ID */
	if (request->channel_id.session_id.buf) {
		session_id = request->channel_id.session_id.buf;
	}

	/* Build JSON request with properly escaped strings */
	{
		const char *escaped_text = json_escape_string(text, pool);
		const char *escaped_voice = json_escape_string(voice_name, pool);
		const char *escaped_session = json_escape_string(session_id, pool);
		
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

	apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_DEBUG, "TTS Request JSON: %s", json);
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
		apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_WARNING, 
			"Failed to Get Codec Descriptor " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
		response->start_line.status_code = MRCP_STATUS_CODE_METHOD_FAILED;
		return FALSE;
	}

	synth_channel->codec_descriptor = descriptor;

	apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, 
		"SPEAK Request: sample_rate=%d " APT_SIDRES_FMT,
		descriptor->sampling_rate, MRCP_MESSAGE_SIDRES(request));

	/* Reset audio buffer */
	apr_thread_mutex_lock(synth_channel->mutex);
	synth_channel->audio_buffer_write_pos = 0;
	synth_channel->audio_buffer_read_pos = 0;
	synth_channel->audio_complete = FALSE;
	synth_channel->paused = FALSE;
	apr_thread_mutex_unlock(synth_channel->mutex);

	/* Set request state to in-progress and send response */
	response->start_line.request_state = MRCP_REQUEST_STATE_INPROGRESS;
	mrcp_engine_channel_message_send(channel, response);
	
	/* Store speak request */
	synth_channel->speak_request = request;

	/* Signal background task to process TTS request */
	return websocket_synth_msg_signal(WEBSOCKET_SYNTH_MSG_SPEAK_REQUEST, channel, request);
}

/** Process STOP request */
static apt_bool_t websocket_synth_channel_stop(
	mrcp_engine_channel_t *channel,
	mrcp_message_t *request,
	mrcp_message_t *response)
{
	websocket_synth_channel_t *synth_channel = channel->method_obj;
	apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, "STOP Request");
	
	/* Store STOP request, make sure there is no more activity and only then send the response */
	synth_channel->stop_response = response;
	return TRUE;
}

/** Process PAUSE request */
static apt_bool_t websocket_synth_channel_pause(
	mrcp_engine_channel_t *channel,
	mrcp_message_t *request,
	mrcp_message_t *response)
{
	websocket_synth_channel_t *synth_channel = channel->method_obj;
	apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, "PAUSE Request");
	
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
	apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, "RESUME Request");
	
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
	mrcp_synth_header_t *req_synth_header;
	
	req_synth_header = mrcp_resource_header_get(request);
	if (req_synth_header) {
		if (mrcp_resource_header_property_check(request, SYNTHESIZER_HEADER_VOICE_AGE) == TRUE) {
			apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, 
				"Set Voice Age [%" APR_SIZE_T_FMT "]", req_synth_header->voice_param.age);
		}
		if (mrcp_resource_header_property_check(request, SYNTHESIZER_HEADER_VOICE_NAME) == TRUE) {
			apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, 
				"Set Voice Name [%s]", req_synth_header->voice_param.name.buf);
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
	mrcp_synth_header_t *req_synth_header;
	
	req_synth_header = mrcp_resource_header_get(request);
	if (req_synth_header) {
		mrcp_synth_header_t *res_synth_header = mrcp_resource_header_prepare(response);
		if (mrcp_resource_header_property_check(request, SYNTHESIZER_HEADER_VOICE_AGE) == TRUE) {
			res_synth_header->voice_param.age = 30;
			mrcp_resource_header_property_add(response, SYNTHESIZER_HEADER_VOICE_AGE);
		}
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
		case SYNTHESIZER_CONTROL:
			break;
		case SYNTHESIZER_DEFINE_LEXICON:
			break;
		default:
			break;
	}
	
	if (processed == FALSE) {
		/* send asynchronous response for not handled request */
		mrcp_engine_channel_message_send(channel, response);
	}
	return TRUE;
}

/** Callback is called from MPF engine context to destroy any additional data associated with audio stream */
static apt_bool_t websocket_synth_stream_destroy(mpf_audio_stream_t *stream)
{
	return TRUE;
}

/** Callback is called from MPF engine context to perform any action before open */
static apt_bool_t websocket_synth_stream_open(mpf_audio_stream_t *stream, mpf_codec_t *codec)
{
	return TRUE;
}

/** Callback is called from MPF engine context to perform any action after close */
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

	/* create SPEAK-COMPLETE event */
	message = mrcp_event_create(
		synth_channel->speak_request,
		SYNTHESIZER_SPEAK_COMPLETE,
		synth_channel->speak_request->pool);
	if (!message) {
		return FALSE;
	}

	/* get/allocate synthesizer header */
	synth_header = mrcp_resource_header_prepare(message);
	if (synth_header) {
		synth_header->completion_cause = cause;
		mrcp_resource_header_property_add(message, SYNTHESIZER_HEADER_COMPLETION_CAUSE);
	}

	/* set request state */
	message->start_line.request_state = MRCP_REQUEST_STATE_COMPLETE;

	synth_channel->speak_request = NULL;

	apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, 
		"SPEAK-COMPLETE event: cause=%d", cause);

	/* send async event */
	return mrcp_engine_channel_message_send(synth_channel->channel, message);
}

/** Callback is called from MPF engine context to read/get new frame (Source Stream for TTS) */
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
		
		/* Calculate available data in buffer */
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
 * WebSocket Client Implementation
 ******************************************************************************/

/** Connect to WebSocket server */
static apt_bool_t websocket_client_connect(websocket_client_t *ws_client)
{
	apr_status_t rv;
	char request[2048];
	char response[4096];
	apr_size_t len;
	const char *host_header;
	unsigned char random_key[16];
	apr_uuid_t uuid;
	char *key;

	if (ws_client->connected) {
		return TRUE;
	}

	apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, 
		"Connecting to WebSocket server: %s:%d%s", 
		ws_client->host, ws_client->port, ws_client->path);

	/* Create socket */
	rv = apr_socket_create(&ws_client->socket, APR_INET, SOCK_STREAM, APR_PROTO_TCP, ws_client->pool);
	if (rv != APR_SUCCESS) {
		apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_ERROR, "Failed to create socket");
		return FALSE;
	}

	/* Set socket timeout */
	apr_socket_timeout_set(ws_client->socket, 30 * 1000000); /* 30 seconds */

	/* Resolve hostname */
	rv = apr_sockaddr_info_get(&ws_client->sa, ws_client->host, APR_INET, ws_client->port, 0, ws_client->pool);
	if (rv != APR_SUCCESS) {
		apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_ERROR, "Failed to resolve hostname: %s", ws_client->host);
		apr_socket_close(ws_client->socket);
		ws_client->socket = NULL;
		return FALSE;
	}

	/* Connect */
	rv = apr_socket_connect(ws_client->socket, ws_client->sa);
	if (rv != APR_SUCCESS) {
		apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_ERROR, 
			"Failed to connect to %s:%d", ws_client->host, ws_client->port);
		apr_socket_close(ws_client->socket);
		ws_client->socket = NULL;
		return FALSE;
	}

	/* Generate random WebSocket key */
	apr_uuid_get(&uuid);
	memcpy(random_key, uuid.data, 16);
	key = apr_palloc(ws_client->pool, apr_base64_encode_len(16) + 1);
	apr_base64_encode(key, (char*)random_key, 16);

	/* Build WebSocket handshake request */
	host_header = apr_psprintf(ws_client->pool, "%s:%d", ws_client->host, ws_client->port);
	apr_snprintf(request, sizeof(request),
		"GET %s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Key: %s\r\n"
		"Sec-WebSocket-Version: 13\r\n"
		"\r\n",
		ws_client->path, host_header, key);

	/* Send handshake */
	len = strlen(request);
	rv = apr_socket_send(ws_client->socket, request, &len);
	if (rv != APR_SUCCESS) {
		apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_ERROR, "Failed to send WebSocket handshake");
		apr_socket_close(ws_client->socket);
		ws_client->socket = NULL;
		return FALSE;
	}

	/* Receive response */
	len = sizeof(response) - 1;
	rv = apr_socket_recv(ws_client->socket, response, &len);
	if (rv != APR_SUCCESS) {
		apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_ERROR, "Failed to receive WebSocket handshake response");
		apr_socket_close(ws_client->socket);
		ws_client->socket = NULL;
		return FALSE;
	}
	response[len] = '\0';

	/* Check for successful handshake */
	if (strstr(response, "101") == NULL) {
		apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_ERROR, "WebSocket handshake failed: %s", response);
		apr_socket_close(ws_client->socket);
		ws_client->socket = NULL;
		return FALSE;
	}

	ws_client->connected = TRUE;
	apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, "WebSocket connected successfully");
	return TRUE;
}

/** Send text message via WebSocket */
static apt_bool_t websocket_client_send_text(websocket_client_t *ws_client, const char *text, apr_size_t len)
{
	apr_status_t rv;
	apr_size_t sent;
	unsigned char frame_header[14];
	apr_size_t header_len = 0;
	unsigned char mask[4];
	char *masked_data;
	apr_size_t i;
	apr_time_t now;

	if (!ws_client->connected || !ws_client->socket) {
		return FALSE;
	}

	/* Generate masking key */
	now = apr_time_now();
	mask[0] = (unsigned char)(now & 0xFF);
	mask[1] = (unsigned char)((now >> 8) & 0xFF);
	mask[2] = (unsigned char)((now >> 16) & 0xFF);
	mask[3] = (unsigned char)((now >> 24) & 0xFF);

	/* Build frame header: FIN=1, opcode=1 (text frame), MASK=1 */
	frame_header[0] = 0x81; /* FIN=1, opcode=1 (text) */
	
	if (len < 126) {
		frame_header[1] = 0x80 | (unsigned char)len;
		memcpy(&frame_header[2], mask, 4);
		header_len = 6;
	} else if (len < 65536) {
		frame_header[1] = 0xFE;
		frame_header[2] = (len >> 8) & 0xFF;
		frame_header[3] = len & 0xFF;
		memcpy(&frame_header[4], mask, 4);
		header_len = 8;
	} else {
		frame_header[1] = 0xFF;
		memset(&frame_header[2], 0, 4);
		frame_header[6] = (len >> 24) & 0xFF;
		frame_header[7] = (len >> 16) & 0xFF;
		frame_header[8] = (len >> 8) & 0xFF;
		frame_header[9] = len & 0xFF;
		memcpy(&frame_header[10], mask, 4);
		header_len = 14;
	}

	/* Send frame header */
	sent = header_len;
	rv = apr_socket_send(ws_client->socket, (char*)frame_header, &sent);
	if (rv != APR_SUCCESS || sent != header_len) {
		apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_ERROR, "Failed to send WebSocket frame header");
		return FALSE;
	}

	/* Mask and send payload */
	masked_data = apr_palloc(ws_client->pool, len);
	for (i = 0; i < len; i++) {
		masked_data[i] = text[i] ^ mask[i % 4];
	}

	sent = len;
	rv = apr_socket_send(ws_client->socket, masked_data, &sent);
	if (rv != APR_SUCCESS || sent != len) {
		apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_ERROR, "Failed to send WebSocket payload");
		return FALSE;
	}

	apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_DEBUG, "Sent WebSocket text message: %d bytes", (int)len);
	return TRUE;
}

/** Send binary message via WebSocket */
static apt_bool_t websocket_client_send_binary(websocket_client_t *ws_client, const char *data, apr_size_t len)
{
	apr_status_t rv;
	apr_size_t sent;
	unsigned char frame_header[14];
	apr_size_t header_len = 0;
	unsigned char mask[4];
	char *masked_data;
	apr_size_t i;
	apr_time_t now;

	if (!ws_client->connected || !ws_client->socket) {
		return FALSE;
	}

	/* Generate masking key */
	now = apr_time_now();
	mask[0] = (unsigned char)(now & 0xFF);
	mask[1] = (unsigned char)((now >> 8) & 0xFF);
	mask[2] = (unsigned char)((now >> 16) & 0xFF);
	mask[3] = (unsigned char)((now >> 24) & 0xFF);

	/* Build frame header: FIN=1, opcode=2 (binary frame), MASK=1 */
	frame_header[0] = 0x82; /* FIN=1, opcode=2 (binary) */
	
	if (len < 126) {
		frame_header[1] = 0x80 | (unsigned char)len;
		memcpy(&frame_header[2], mask, 4);
		header_len = 6;
	} else if (len < 65536) {
		frame_header[1] = 0xFE;
		frame_header[2] = (len >> 8) & 0xFF;
		frame_header[3] = len & 0xFF;
		memcpy(&frame_header[4], mask, 4);
		header_len = 8;
	} else {
		frame_header[1] = 0xFF;
		memset(&frame_header[2], 0, 4);
		frame_header[6] = (len >> 24) & 0xFF;
		frame_header[7] = (len >> 16) & 0xFF;
		frame_header[8] = (len >> 8) & 0xFF;
		frame_header[9] = len & 0xFF;
		memcpy(&frame_header[10], mask, 4);
		header_len = 14;
	}

	/* Send frame header */
	sent = header_len;
	rv = apr_socket_send(ws_client->socket, (char*)frame_header, &sent);
	if (rv != APR_SUCCESS || sent != header_len) {
		return FALSE;
	}

	/* Mask and send payload */
	masked_data = apr_palloc(ws_client->pool, len);
	for (i = 0; i < len; i++) {
		masked_data[i] = data[i] ^ mask[i % 4];
	}

	sent = len;
	rv = apr_socket_send(ws_client->socket, masked_data, &sent);
	if (rv != APR_SUCCESS || sent != len) {
		return FALSE;
	}

	return TRUE;
}

/** Receive audio data from WebSocket server */
static apt_bool_t websocket_client_receive_audio(websocket_client_t *ws_client, websocket_synth_channel_t *synth_channel)
{
	apr_status_t rv;
	unsigned char header[14];
	apr_size_t header_len;
	apr_size_t payload_len;
	unsigned char opcode;
	apt_bool_t fin;
	char *payload;
	apr_size_t received;
	apr_size_t remaining;

	if (!ws_client->connected || !ws_client->socket) {
		return FALSE;
	}

	while (1) {
		/* Read frame header (at least 2 bytes) */
		header_len = 2;
		rv = apr_socket_recv(ws_client->socket, (char*)header, &header_len);
		if (rv != APR_SUCCESS || header_len < 2) {
			if (APR_STATUS_IS_TIMEUP(rv) || APR_STATUS_IS_EAGAIN(rv)) {
				/* Timeout or would block - continue */
				continue;
			}
			apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_ERROR, "Failed to receive WebSocket frame header");
			return FALSE;
		}

		fin = (header[0] & 0x80) != 0;
		opcode = header[0] & 0x0F;
		payload_len = header[1] & 0x7F;

		/* Extended payload length */
		if (payload_len == 126) {
			header_len = 2;
			rv = apr_socket_recv(ws_client->socket, (char*)&header[2], &header_len);
			if (rv != APR_SUCCESS || header_len < 2) {
				return FALSE;
			}
			payload_len = ((apr_size_t)header[2] << 8) | header[3];
		} else if (payload_len == 127) {
			header_len = 8;
			rv = apr_socket_recv(ws_client->socket, (char*)&header[2], &header_len);
			if (rv != APR_SUCCESS || header_len < 8) {
				return FALSE;
			}
			payload_len = ((apr_size_t)header[6] << 24) | ((apr_size_t)header[7] << 16) |
			              ((apr_size_t)header[8] << 8) | header[9];
		}

		/* Read payload */
		if (payload_len > 0) {
			payload = apr_palloc(ws_client->pool, payload_len);
			received = 0;
			while (received < payload_len) {
				remaining = payload_len - received;
				rv = apr_socket_recv(ws_client->socket, payload + received, &remaining);
				if (rv != APR_SUCCESS) {
					return FALSE;
				}
				received += remaining;
			}

			/* Process frame based on opcode */
			if (opcode == 0x02 || opcode == 0x00) {
				/* Binary frame or continuation - audio data */
				apr_thread_mutex_lock(synth_channel->mutex);
				
				/* Check buffer space */
				apr_size_t space = synth_channel->audio_buffer_size - synth_channel->audio_buffer_write_pos;
				if (payload_len <= space) {
					memcpy(synth_channel->audio_buffer + synth_channel->audio_buffer_write_pos,
						payload, payload_len);
					synth_channel->audio_buffer_write_pos += payload_len;
					apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_DEBUG, 
						"Received audio data: %d bytes, buffer pos: %d",
						(int)payload_len, (int)synth_channel->audio_buffer_write_pos);
				} else {
					apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_WARNING, 
						"Audio buffer overflow, dropping %d bytes", (int)payload_len);
				}
				
				apr_thread_mutex_unlock(synth_channel->mutex);
			} else if (opcode == 0x01) {
				/* Text frame - could be status message */
				apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_DEBUG, 
					"Received text message: %.*s", (int)payload_len, payload);
				
				/* Check for completion message */
				if (strstr(payload, "complete") || strstr(payload, "end") || strstr(payload, "done")) {
					apr_thread_mutex_lock(synth_channel->mutex);
					synth_channel->audio_complete = TRUE;
					apr_thread_mutex_unlock(synth_channel->mutex);
					apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, "TTS synthesis complete");
					return TRUE;
				}
			} else if (opcode == 0x08) {
				/* Close frame */
				apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, "WebSocket close frame received");
				apr_thread_mutex_lock(synth_channel->mutex);
				synth_channel->audio_complete = TRUE;
				apr_thread_mutex_unlock(synth_channel->mutex);
				return TRUE;
			} else if (opcode == 0x09) {
				/* Ping - respond with pong */
				unsigned char pong[2] = {0x8A, 0x00};
				apr_size_t pong_len = 2;
				apr_socket_send(ws_client->socket, (char*)pong, &pong_len);
			}
		}

		/* If FIN is set on a binary/text frame, check if we're done */
		if (fin && (opcode == 0x01 || opcode == 0x02)) {
			/* For single-frame messages, continue reading for more frames */
		}
	}

	return TRUE;
}

/** Destroy WebSocket client */
static void websocket_client_destroy(websocket_client_t *ws_client)
{
	if (ws_client->socket) {
		/* Send close frame */
		if (ws_client->connected) {
			unsigned char close_frame[6];
			apr_size_t len;
			apr_time_t now = apr_time_now();
			
			close_frame[0] = 0x88; /* FIN=1, opcode=8 (close) */
			close_frame[1] = 0x80; /* MASK=1, length=0 */
			close_frame[2] = (unsigned char)(now & 0xFF);
			close_frame[3] = (unsigned char)((now >> 8) & 0xFF);
			close_frame[4] = (unsigned char)((now >> 16) & 0xFF);
			close_frame[5] = (unsigned char)((now >> 24) & 0xFF);
			
			len = 6;
			apr_socket_send(ws_client->socket, (char*)close_frame, &len);
		}
		
		apr_socket_close(ws_client->socket);
		ws_client->socket = NULL;
	}
	ws_client->connected = FALSE;
}

/*******************************************************************************
 * Background Task Message Processing
 ******************************************************************************/

static apt_bool_t websocket_synth_msg_signal(
	websocket_synth_msg_type_e type,
	mrcp_engine_channel_t *channel,
	mrcp_message_t *request)
{
	apt_bool_t status = FALSE;
	websocket_synth_channel_t *synth_channel = channel->method_obj;
	websocket_synth_engine_t *websocket_engine = synth_channel->websocket_engine;
	apt_task_t *task = apt_consumer_task_base_get(websocket_engine->task);
	apt_task_msg_t *msg = apt_task_msg_get(task);
	
	if (msg) {
		websocket_synth_msg_t *synth_msg;
		msg->type = TASK_MSG_USER;
		synth_msg = (websocket_synth_msg_t*)msg->data;

		synth_msg->type = type;
		synth_msg->channel = channel;
		synth_msg->request = request;
		status = apt_task_msg_signal(task, msg);
	}
	return status;
}

static apt_bool_t websocket_synth_msg_process(apt_task_t *task, apt_task_msg_t *msg)
{
	websocket_synth_msg_t *synth_msg = (websocket_synth_msg_t*)msg->data;
	
	switch (synth_msg->type) {
		case WEBSOCKET_SYNTH_MSG_OPEN_CHANNEL:
			/* Open channel and send async response */
			mrcp_engine_channel_open_respond(synth_msg->channel, TRUE);
			break;
			
		case WEBSOCKET_SYNTH_MSG_CLOSE_CHANNEL:
		{
			/* Close channel, cleanup and send async response */
			websocket_synth_channel_t *synth_channel = synth_msg->channel->method_obj;
			if (synth_channel->ws_client) {
				websocket_client_destroy(synth_channel->ws_client);
			}
			mrcp_engine_channel_close_respond(synth_msg->channel);
			break;
		}
		
		case WEBSOCKET_SYNTH_MSG_REQUEST_PROCESS:
			websocket_synth_channel_request_dispatch(synth_msg->channel, synth_msg->request);
			break;
			
		case WEBSOCKET_SYNTH_MSG_SPEAK_REQUEST:
		{
			/* Process SPEAK request in background task */
			websocket_synth_channel_t *synth_channel = synth_msg->channel->method_obj;
			char *json_request;
			
			/* Connect to WebSocket server if not connected */
			if (!synth_channel->ws_client->connected) {
				if (!websocket_client_connect(synth_channel->ws_client)) {
					apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_ERROR, 
						"Failed to connect to TTS WebSocket server");
					websocket_synth_speak_complete(synth_channel, SYNTHESIZER_COMPLETION_CAUSE_ERROR);
					break;
				}
			}
			
			/* Build and send TTS request */
			json_request = websocket_synth_build_request_json(
				synth_channel, synth_msg->request, synth_msg->channel->pool);
			
			if (!json_request) {
				apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_ERROR, "Failed to build TTS request JSON");
				websocket_synth_speak_complete(synth_channel, SYNTHESIZER_COMPLETION_CAUSE_ERROR);
				break;
			}
			
			if (!websocket_client_send_text(synth_channel->ws_client, json_request, strlen(json_request))) {
				apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_ERROR, "Failed to send TTS request");
				websocket_synth_speak_complete(synth_channel, SYNTHESIZER_COMPLETION_CAUSE_ERROR);
				break;
			}
			
			apt_log(WEBSOCKET_SYNTH_LOG_MARK, APT_PRIO_INFO, "TTS request sent, receiving audio...");
			
			/* Receive audio data from TTS server */
			websocket_client_receive_audio(synth_channel->ws_client, synth_channel);
			
			break;
		}
		
		default:
			break;
	}
	
	return TRUE;
}
