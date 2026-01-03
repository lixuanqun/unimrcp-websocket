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

#include "mrcp_recog_engine.h"
#include "mpf_activity_detector.h"
#include "apt_consumer_task.h"
#include "apt_log.h"
#include <apr_strings.h>
#include <apr_network_io.h>
#include <apr_base64.h>
#include <apr_time.h>
#include <apr_uuid.h>
#include <string.h>

#define WEBSOCKET_RECOG_ENGINE_TASK_NAME "WebSocket Recog Engine"

typedef struct websocket_recog_engine_t websocket_recog_engine_t;
typedef struct websocket_recog_channel_t websocket_recog_channel_t;
typedef struct websocket_recog_msg_t websocket_recog_msg_t;
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

/** Declaration of recognizer engine methods */
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

/** Declaration of recognizer channel methods */
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

/** Declaration of recognizer audio stream methods */
static apt_bool_t websocket_recog_stream_destroy(mpf_audio_stream_t *stream);
static apt_bool_t websocket_recog_stream_open(mpf_audio_stream_t *stream, mpf_codec_t *codec);
static apt_bool_t websocket_recog_stream_close(mpf_audio_stream_t *stream);
static apt_bool_t websocket_recog_stream_write(mpf_audio_stream_t *stream, const mpf_frame_t *frame);

static const mpf_audio_stream_vtable_t audio_stream_vtable = {
	websocket_recog_stream_destroy,
	NULL,
	NULL,
	NULL,
	websocket_recog_stream_open,
	websocket_recog_stream_close,
	websocket_recog_stream_write,
	NULL
};

/** Declaration of websocket recognizer engine */
struct websocket_recog_engine_t {
	apt_consumer_task_t    *task;
};

/** Declaration of websocket recognizer channel */
struct websocket_recog_channel_t {
	/** Back pointer to engine */
	websocket_recog_engine_t     *websocket_engine;
	/** Engine channel base */
	mrcp_engine_channel_t   *channel;

	/** Active (in-progress) recognition request */
	mrcp_message_t          *recog_request;
	/** Pending stop response */
	mrcp_message_t          *stop_response;
	/** Indicates whether input timers are started */
	apt_bool_t               timers_started;
	/** Voice activity detector */
	mpf_activity_detector_t *detector;
	/** WebSocket client */
	websocket_client_t      *ws_client;
	/** Audio buffer for WebSocket transmission */
	apr_size_t              audio_buffer_size;
	char                   *audio_buffer;
	apr_size_t              audio_buffer_pos;
};

typedef enum {
	WEBSOCKET_RECOG_MSG_OPEN_CHANNEL,
	WEBSOCKET_RECOG_MSG_CLOSE_CHANNEL,
	WEBSOCKET_RECOG_MSG_REQUEST_PROCESS,
	WEBSOCKET_RECOG_MSG_SEND_AUDIO  /* P1: Send audio data via background task */
} websocket_recog_msg_type_e;

/** Declaration of websocket recognizer task message */
struct websocket_recog_msg_t {
	websocket_recog_msg_type_e  type;
	mrcp_engine_channel_t *channel; 
	mrcp_message_t        *request;
};

static apt_bool_t websocket_recog_msg_signal(websocket_recog_msg_type_e type, mrcp_engine_channel_t *channel, mrcp_message_t *request);
static apt_bool_t websocket_recog_msg_process(apt_task_t *task, apt_task_msg_t *msg);

/** WebSocket handshake and connection functions */
static apt_bool_t websocket_client_connect(websocket_client_t *ws_client);
static apt_bool_t websocket_client_send(websocket_client_t *ws_client, const char *data, apr_size_t len);
static apt_bool_t websocket_client_receive(websocket_client_t *ws_client, char *buffer, apr_size_t *len);
static void websocket_client_destroy(websocket_client_t *ws_client);

/** Declare this macro to set plugin version */
MRCP_PLUGIN_VERSION_DECLARE

/**
 * Declare this macro to use log routine of the server, plugin is loaded from.
 * Enable/add the corresponding entry in logger.xml to set a custom log source priority.
 *    <source name="WEBSOCKET-RECOG-PLUGIN" priority="DEBUG" masking="NONE"/>
 */
MRCP_PLUGIN_LOG_SOURCE_IMPLEMENT(WEBSOCKET_RECOG_PLUGIN,"WEBSOCKET-RECOG-PLUGIN")

/** Use custom log source mark */
#define WEBSOCKET_RECOG_LOG_MARK   APT_LOG_MARK_DECLARE(WEBSOCKET_RECOG_PLUGIN)

/** Create websocket recognizer engine */
MRCP_PLUGIN_DECLARE(mrcp_engine_t*) mrcp_plugin_create(apr_pool_t *pool)
{
	websocket_recog_engine_t *websocket_engine = apr_palloc(pool,sizeof(websocket_recog_engine_t));
	apt_task_t *task;
	apt_task_vtable_t *vtable;
	apt_task_msg_pool_t *msg_pool;

	msg_pool = apt_task_msg_pool_create_dynamic(sizeof(websocket_recog_msg_t),pool);
	websocket_engine->task = apt_consumer_task_create(websocket_engine,msg_pool,pool);
	if(!websocket_engine->task) {
		return NULL;
	}
	task = apt_consumer_task_base_get(websocket_engine->task);
	apt_task_name_set(task,WEBSOCKET_RECOG_ENGINE_TASK_NAME);
	vtable = apt_task_vtable_get(task);
	if(vtable) {
		vtable->process_msg = websocket_recog_msg_process;
	}

	/* create engine base */
	return mrcp_engine_create(
				MRCP_RECOGNIZER_RESOURCE,  /* MRCP resource identifier */
				websocket_engine,               /* object to associate */
				&engine_vtable,            /* virtual methods table of engine */
				pool);                     /* pool to allocate memory from */
}

/** Destroy recognizer engine */
static apt_bool_t websocket_recog_engine_destroy(mrcp_engine_t *engine)
{
	websocket_recog_engine_t *websocket_engine = engine->obj;
	if(websocket_engine->task) {
		apt_task_t *task = apt_consumer_task_base_get(websocket_engine->task);
		apt_task_destroy(task);
		websocket_engine->task = NULL;
	}
	return TRUE;
}

/** Open recognizer engine */
static apt_bool_t websocket_recog_engine_open(mrcp_engine_t *engine)
{
	websocket_recog_engine_t *websocket_engine = engine->obj;
	if(websocket_engine->task) {
		apt_task_t *task = apt_consumer_task_base_get(websocket_engine->task);
		apt_task_start(task);
	}
	return mrcp_engine_open_respond(engine,TRUE);
}

/** Close recognizer engine */
static apt_bool_t websocket_recog_engine_close(mrcp_engine_t *engine)
{
	websocket_recog_engine_t *websocket_engine = engine->obj;
	if(websocket_engine->task) {
		apt_task_t *task = apt_consumer_task_base_get(websocket_engine->task);
		apt_task_terminate(task,TRUE);
	}
	return mrcp_engine_close_respond(engine);
}

static mrcp_engine_channel_t* websocket_recog_engine_channel_create(mrcp_engine_t *engine, apr_pool_t *pool)
{
	mpf_stream_capabilities_t *capabilities;
	mpf_termination_t *termination; 
	const char *ws_host;
	const char *ws_port_str;
	const char *ws_path;
	int ws_port;

	/* create websocket recog channel */
	websocket_recog_channel_t *recog_channel = apr_palloc(pool,sizeof(websocket_recog_channel_t));
	recog_channel->websocket_engine = engine->obj;
	recog_channel->recog_request = NULL;
	recog_channel->stop_response = NULL;
	recog_channel->detector = mpf_activity_detector_create(pool);
	recog_channel->ws_client = NULL;
	recog_channel->audio_buffer = NULL;
	recog_channel->audio_buffer_size = 0;
	recog_channel->audio_buffer_pos = 0;

	/* Get WebSocket configuration from engine params */
	ws_host = mrcp_engine_param_get(engine, "ws-host");
	ws_port_str = mrcp_engine_param_get(engine, "ws-port");
	ws_path = mrcp_engine_param_get(engine, "ws-path");

	if(!ws_host) {
		ws_host = "localhost";
	}
	if(!ws_port_str) {
		ws_port = 8080;
	} else {
		ws_port = atoi(ws_port_str);
	}
	if(!ws_path) {
		ws_path = "/";
	}

	/* Create WebSocket client */
	recog_channel->ws_client = apr_palloc(pool, sizeof(websocket_client_t));
	recog_channel->ws_client->pool = pool;
	recog_channel->ws_client->host = apr_pstrdup(pool, ws_host);
	recog_channel->ws_client->port = ws_port;
	recog_channel->ws_client->path = apr_pstrdup(pool, ws_path);
	recog_channel->ws_client->socket = NULL;
	recog_channel->ws_client->sa = NULL;
	recog_channel->ws_client->connected = FALSE;

	capabilities = mpf_sink_stream_capabilities_create(pool);
	mpf_codec_capabilities_add(
			&capabilities->codecs,
			MPF_SAMPLE_RATE_8000 | MPF_SAMPLE_RATE_16000,
			"LPCM");

	/* create media termination */
	termination = mrcp_engine_audio_termination_create(
			recog_channel,        /* object to associate */
			&audio_stream_vtable, /* virtual methods table of audio stream */
			capabilities,         /* stream capabilities */
			pool);                /* pool to allocate memory from */

	/* create engine channel base */
	recog_channel->channel = mrcp_engine_channel_create(
			engine,               /* engine */
			&channel_vtable,      /* virtual methods table of engine channel */
			recog_channel,        /* object to associate */
			termination,          /* associated media termination */
			pool);                /* pool to allocate memory from */

	return recog_channel->channel;
}

/** Destroy engine channel */
static apt_bool_t websocket_recog_channel_destroy(mrcp_engine_channel_t *channel)
{
	websocket_recog_channel_t *recog_channel = channel->method_obj;
	if(recog_channel->ws_client) {
		websocket_client_destroy(recog_channel->ws_client);
	}
	/* audio_buffer is allocated from pool, will be freed automatically */
	return TRUE;
}

/** Open engine channel (asynchronous response MUST be sent)*/
static apt_bool_t websocket_recog_channel_open(mrcp_engine_channel_t *channel)
{
	if(channel->attribs) {
		/* process attributes */
		const apr_array_header_t *header = apr_table_elts(channel->attribs);
		apr_table_entry_t *entry = (apr_table_entry_t *)header->elts;
		int i;
		for(i=0; i<header->nelts; i++) {
			apt_log(WEBSOCKET_RECOG_LOG_MARK,APT_PRIO_INFO,"Attrib name [%s] value [%s]",entry[i].key,entry[i].val);
		}
	}

	return websocket_recog_msg_signal(WEBSOCKET_RECOG_MSG_OPEN_CHANNEL,channel,NULL);
}

/** Close engine channel (asynchronous response MUST be sent)*/
static apt_bool_t websocket_recog_channel_close(mrcp_engine_channel_t *channel)
{
	return websocket_recog_msg_signal(WEBSOCKET_RECOG_MSG_CLOSE_CHANNEL,channel,NULL);
}

/** Process MRCP channel request (asynchronous response MUST be sent)*/
static apt_bool_t websocket_recog_channel_request_process(mrcp_engine_channel_t *channel, mrcp_message_t *request)
{
	return websocket_recog_msg_signal(WEBSOCKET_RECOG_MSG_REQUEST_PROCESS,channel,request);
}

/** Process RECOGNIZE request */
static apt_bool_t websocket_recog_channel_recognize(mrcp_engine_channel_t *channel, mrcp_message_t *request, mrcp_message_t *response)
{
	/* process RECOGNIZE request */
	mrcp_recog_header_t *recog_header;
	websocket_recog_channel_t *recog_channel = channel->method_obj;
	const mpf_codec_descriptor_t *descriptor = mrcp_engine_sink_stream_codec_get(channel);

	if(!descriptor) {
		apt_log(WEBSOCKET_RECOG_LOG_MARK,APT_PRIO_WARNING,"Failed to Get Codec Descriptor " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
		response->start_line.status_code = MRCP_STATUS_CODE_METHOD_FAILED;
		return FALSE;
	}

	recog_channel->timers_started = TRUE;

	/* get recognizer header */
	recog_header = mrcp_resource_header_get(request);
	if(recog_header) {
		if(mrcp_resource_header_property_check(request,RECOGNIZER_HEADER_START_INPUT_TIMERS) == TRUE) {
			recog_channel->timers_started = recog_header->start_input_timers;
		}
		if(mrcp_resource_header_property_check(request,RECOGNIZER_HEADER_NO_INPUT_TIMEOUT) == TRUE) {
			mpf_activity_detector_noinput_timeout_set(recog_channel->detector,recog_header->no_input_timeout);
		}
		if(mrcp_resource_header_property_check(request,RECOGNIZER_HEADER_SPEECH_COMPLETE_TIMEOUT) == TRUE) {
			mpf_activity_detector_silence_timeout_set(recog_channel->detector,recog_header->speech_complete_timeout);
		}
	}

	/* Connect to WebSocket server if not connected */
	if(!recog_channel->ws_client->connected) {
		if(websocket_client_connect(recog_channel->ws_client) != TRUE) {
			apt_log(WEBSOCKET_RECOG_LOG_MARK,APT_PRIO_ERROR,"Failed to Connect to WebSocket Server");
			response->start_line.status_code = MRCP_STATUS_CODE_METHOD_FAILED;
			return FALSE;
		}
	}

	/* Initialize audio buffer */
	if(!recog_channel->audio_buffer) {
		recog_channel->audio_buffer_size = descriptor->sampling_rate * 2; /* 1 second buffer */
		recog_channel->audio_buffer = apr_palloc(channel->pool, recog_channel->audio_buffer_size);
		recog_channel->audio_buffer_pos = 0;
	}

	response->start_line.request_state = MRCP_REQUEST_STATE_INPROGRESS;
	/* send asynchronous response */
	mrcp_engine_channel_message_send(channel,response);
	recog_channel->recog_request = request;
	return TRUE;
}

/** Process STOP request */
static apt_bool_t websocket_recog_channel_stop(mrcp_engine_channel_t *channel, mrcp_message_t *request, mrcp_message_t *response)
{
	/* process STOP request */
	websocket_recog_channel_t *recog_channel = channel->method_obj;
	/* store STOP request, make sure there is no more activity and only then send the response */
	recog_channel->stop_response = response;
	return TRUE;
}

/** Process START-INPUT-TIMERS request */
static apt_bool_t websocket_recog_channel_timers_start(mrcp_engine_channel_t *channel, mrcp_message_t *request, mrcp_message_t *response)
{
	websocket_recog_channel_t *recog_channel = channel->method_obj;
	recog_channel->timers_started = TRUE;
	return mrcp_engine_channel_message_send(channel,response);
}

/** Dispatch MRCP request */
static apt_bool_t websocket_recog_channel_request_dispatch(mrcp_engine_channel_t *channel, mrcp_message_t *request)
{
	apt_bool_t processed = FALSE;
	mrcp_message_t *response = mrcp_response_create(request,request->pool);
	switch(request->start_line.method_id) {
		case RECOGNIZER_SET_PARAMS:
			break;
		case RECOGNIZER_GET_PARAMS:
			break;
		case RECOGNIZER_DEFINE_GRAMMAR:
			break;
		case RECOGNIZER_RECOGNIZE:
			processed = websocket_recog_channel_recognize(channel,request,response);
			break;
		case RECOGNIZER_GET_RESULT:
			break;
		case RECOGNIZER_START_INPUT_TIMERS:
			processed = websocket_recog_channel_timers_start(channel,request,response);
			break;
		case RECOGNIZER_STOP:
			processed = websocket_recog_channel_stop(channel,request,response);
			break;
		default:
			break;
	}
	if(processed == FALSE) {
		/* send asynchronous response for not handled request */
		mrcp_engine_channel_message_send(channel,response);
	}
	return TRUE;
}

/** Callback is called from MPF engine context to destroy any additional data associated with audio stream */
static apt_bool_t websocket_recog_stream_destroy(mpf_audio_stream_t *stream)
{
	return TRUE;
}

/** Callback is called from MPF engine context to perform any action before open */
static apt_bool_t websocket_recog_stream_open(mpf_audio_stream_t *stream, mpf_codec_t *codec)
{
	return TRUE;
}

/** Callback is called from MPF engine context to perform any action after close */
static apt_bool_t websocket_recog_stream_close(mpf_audio_stream_t *stream)
{
	return TRUE;
}

/* Raise websocket START-OF-INPUT event */
static apt_bool_t websocket_recog_start_of_input(websocket_recog_channel_t *recog_channel)
{
	/* create START-OF-INPUT event */
	mrcp_message_t *message = mrcp_event_create(
						recog_channel->recog_request,
						RECOGNIZER_START_OF_INPUT,
						recog_channel->recog_request->pool);
	if(!message) {
		return FALSE;
	}

	/* set request state */
	message->start_line.request_state = MRCP_REQUEST_STATE_INPROGRESS;
	/* send asynch event */
	return mrcp_engine_channel_message_send(recog_channel->channel,message);
}

/* Process WebSocket recognition result */
static apt_bool_t websocket_recog_result_process(websocket_recog_channel_t *recog_channel, const char *result_text)
{
	mrcp_recog_header_t *recog_header;
	/* create RECOGNITION-COMPLETE event */
	mrcp_message_t *message = mrcp_event_create(
						recog_channel->recog_request,
						RECOGNIZER_RECOGNITION_COMPLETE,
						recog_channel->recog_request->pool);
	if(!message) {
		return FALSE;
	}

	/* get/allocate recognizer header */
	recog_header = mrcp_resource_header_prepare(message);
	if(recog_header) {
		/* set completion cause */
		recog_header->completion_cause = RECOGNIZER_COMPLETION_CAUSE_SUCCESS;
		mrcp_resource_header_property_add(message,RECOGNIZER_HEADER_COMPLETION_CAUSE);
	}
	/* set request state */
	message->start_line.request_state = MRCP_REQUEST_STATE_COMPLETE;

	/* Set result body if available */
	if(result_text && strlen(result_text) > 0) {
		mrcp_generic_header_t *generic_header = mrcp_generic_header_prepare(message);
		if(generic_header) {
			apt_string_assign(&message->body, result_text, message->pool);
			apt_string_assign(&generic_header->content_type, "application/x-nlsml", message->pool);
			mrcp_generic_header_property_add(message, GENERIC_HEADER_CONTENT_TYPE);
		}
	}

	recog_channel->recog_request = NULL;
	/* send asynch event */
	return mrcp_engine_channel_message_send(recog_channel->channel,message);
}

/* Raise websocket RECOGNITION-COMPLETE event */
static apt_bool_t websocket_recog_recognition_complete(websocket_recog_channel_t *recog_channel, mrcp_recog_completion_cause_e cause)
{
	mrcp_recog_header_t *recog_header;
	/* create RECOGNITION-COMPLETE event */
	mrcp_message_t *message = mrcp_event_create(
						recog_channel->recog_request,
						RECOGNIZER_RECOGNITION_COMPLETE,
						recog_channel->recog_request->pool);
	if(!message) {
		return FALSE;
	}

	/* get/allocate recognizer header */
	recog_header = mrcp_resource_header_prepare(message);
	if(recog_header) {
		/* set completion cause */
		recog_header->completion_cause = cause;
		mrcp_resource_header_property_add(message,RECOGNIZER_HEADER_COMPLETION_CAUSE);
	}
	/* set request state */
	message->start_line.request_state = MRCP_REQUEST_STATE_COMPLETE;

	recog_channel->recog_request = NULL;
	/* send asynch event */
	return mrcp_engine_channel_message_send(recog_channel->channel,message);
}

/** Callback is called from MPF engine context to write/send new frame */
static apt_bool_t websocket_recog_stream_write(mpf_audio_stream_t *stream, const mpf_frame_t *frame)
{
	websocket_recog_channel_t *recog_channel = stream->obj;
	if(recog_channel->stop_response) {
		/* send asynchronous response to STOP request */
		mrcp_engine_channel_message_send(recog_channel->channel,recog_channel->stop_response);
		recog_channel->stop_response = NULL;
		recog_channel->recog_request = NULL;
		return TRUE;
	}

	if(recog_channel->recog_request && recog_channel->ws_client && recog_channel->ws_client->connected) {
		mpf_detector_event_e det_event = mpf_activity_detector_process(recog_channel->detector,frame);
		switch(det_event) {
			case MPF_DETECTOR_EVENT_ACTIVITY:
				apt_log(WEBSOCKET_RECOG_LOG_MARK,APT_PRIO_INFO,"Detected Voice Activity " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
				websocket_recog_start_of_input(recog_channel);
				break;
			case MPF_DETECTOR_EVENT_INACTIVITY:
				apt_log(WEBSOCKET_RECOG_LOG_MARK,APT_PRIO_INFO,"Detected Voice Inactivity " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
				/* P1: Send audio buffer via background task instead of directly */
				if(recog_channel->audio_buffer_pos > 0) {
					/* Signal background task to send audio data (non-blocking) */
					websocket_recog_msg_signal(WEBSOCKET_RECOG_MSG_SEND_AUDIO, recog_channel->channel, NULL);
					/* Don't clear buffer or complete here - background task will handle it */
				} else {
					websocket_recog_recognition_complete(recog_channel,RECOGNIZER_COMPLETION_CAUSE_SUCCESS);
				}
				break;
			case MPF_DETECTOR_EVENT_NOINPUT:
				apt_log(WEBSOCKET_RECOG_LOG_MARK,APT_PRIO_INFO,"Detected Noinput " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
				if(recog_channel->timers_started == TRUE) {
					websocket_recog_recognition_complete(recog_channel,RECOGNIZER_COMPLETION_CAUSE_NO_INPUT_TIMEOUT);
				}
				break;
			default:
				break;
		}

		/* Buffer audio data for WebSocket transmission */
		if((frame->type & MEDIA_FRAME_TYPE_AUDIO) == MEDIA_FRAME_TYPE_AUDIO) {
			apr_size_t remaining = recog_channel->audio_buffer_size - recog_channel->audio_buffer_pos;
			apr_size_t to_copy = frame->codec_frame.size;
			if(to_copy > remaining) {
				to_copy = remaining;
			}
			if(to_copy > 0) {
				memcpy(recog_channel->audio_buffer + recog_channel->audio_buffer_pos, 
				       frame->codec_frame.buffer, to_copy);
				recog_channel->audio_buffer_pos += to_copy;
			}
		}
	}
	return TRUE;
}

/** WebSocket client implementation */
static apt_bool_t websocket_client_connect(websocket_client_t *ws_client)
{
	apr_status_t rv;
	char *key;
	char *accept_key;
	char request[2048];
	char response[4096];
	apr_size_t response_len;
	const char *host_header;
	char *path_header;

	if(ws_client->connected) {
		return TRUE;
	}

	/* Create socket */
	rv = apr_socket_create(&ws_client->socket, APR_INET, SOCK_STREAM, APR_PROTO_TCP, ws_client->pool);
	if(rv != APR_SUCCESS) {
		apt_log(WEBSOCKET_RECOG_LOG_MARK,APT_PRIO_ERROR,"Failed to Create Socket");
		return FALSE;
	}

	/* Set socket to non-blocking mode (P1 improvement) */
	apr_socket_opt_set(ws_client->socket, APR_SO_NONBLOCK, 1);

	/* Resolve hostname */
	rv = apr_sockaddr_info_get(&ws_client->sa, ws_client->host, APR_INET, ws_client->port, 0, ws_client->pool);
	if(rv != APR_SUCCESS) {
		apt_log(WEBSOCKET_RECOG_LOG_MARK,APT_PRIO_ERROR,"Failed to Resolve Hostname [%s]", ws_client->host);
		return FALSE;
	}

	/* Connect */
	rv = apr_socket_connect(ws_client->socket, ws_client->sa);
	if(rv != APR_SUCCESS) {
		apt_log(WEBSOCKET_RECOG_LOG_MARK,APT_PRIO_ERROR,"Failed to Connect to [%s:%d]", ws_client->host, ws_client->port);
		return FALSE;
	}

	/* P2: Generate random WebSocket key (16 bytes, Base64 encoded) */
	unsigned char random_key[16];
	apr_uuid_t uuid;
	apr_uuid_get(&uuid);
	/* Use UUID as random source (16 bytes) */
	memcpy(random_key, uuid.data, 16);
	
	/* Base64 encode */
	char *key_buf = apr_palloc(ws_client->pool, apr_base64_encode_len(16) + 1);
	apr_size_t encoded_len = apr_base64_encode(key_buf, (char*)random_key, 16);
	key_buf[encoded_len] = '\0';
	key = key_buf;

	/* Send WebSocket handshake */
	host_header = apr_psprintf(ws_client->pool, "%s:%d", ws_client->host, ws_client->port);
	path_header = ws_client->path ? ws_client->path : "/";
	
	apr_snprintf(request, sizeof(request),
		"GET %s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Key: %s\r\n"
		"Sec-WebSocket-Version: 13\r\n"
		"\r\n",
		path_header, host_header, key);

	rv = apr_socket_send(ws_client->socket, request, &response_len);
	if(rv != APR_SUCCESS) {
		apt_log(WEBSOCKET_RECOG_LOG_MARK,APT_PRIO_ERROR,"Failed to Send WebSocket Handshake");
		return FALSE;
	}

	/* Read response */
	response_len = sizeof(response) - 1;
	rv = apr_socket_recv(ws_client->socket, response, &response_len);
	if(rv != APR_SUCCESS) {
		apt_log(WEBSOCKET_RECOG_LOG_MARK,APT_PRIO_ERROR,"Failed to Receive WebSocket Handshake Response");
		return FALSE;
	}
	response[response_len] = '\0';

	/* Check for "101 Switching Protocols" */
	if(strstr(response, "101") == NULL && strstr(response, "Switching Protocols") == NULL) {
		apt_log(WEBSOCKET_RECOG_LOG_MARK,APT_PRIO_ERROR,"WebSocket Handshake Failed: %s", response);
		return FALSE;
	}

	ws_client->connected = TRUE;
	apt_log(WEBSOCKET_RECOG_LOG_MARK,APT_PRIO_INFO,"WebSocket Connected to [%s:%d%s]", 
		ws_client->host, ws_client->port, path_header);
	return TRUE;
}

static apt_bool_t websocket_client_send(websocket_client_t *ws_client, const char *data, apr_size_t len)
{
	apr_status_t rv;
	apr_size_t sent;
	unsigned char frame[14];
	apr_size_t frame_len = 0;
	apr_size_t payload_len;
	unsigned char mask[4];
	apr_size_t i;

	if(!ws_client->connected || !ws_client->socket) {
		return FALSE;
	}

	/* Generate masking key (simplified - use pseudo-random from current time) */
	apr_time_t now = apr_time_now();
	apr_int64_t time_usec = now;
	mask[0] = (unsigned char)(time_usec & 0xFF);
	mask[1] = (unsigned char)((time_usec >> 8) & 0xFF);
	mask[2] = (unsigned char)((time_usec >> 16) & 0xFF);
	mask[3] = (unsigned char)((time_usec >> 24) & 0xFF);

	/* WebSocket frame format: FIN=1, opcode=2 (binary frame), MASK=1 */
	frame[0] = 0x82; /* FIN=1, opcode=2 (binary frame) */
	
	if(len < 126) {
		frame[1] = 0x80 | len; /* MASK=1, payload len */
		frame[2] = mask[0];
		frame[3] = mask[1];
		frame[4] = mask[2];
		frame[5] = mask[3];
		frame_len = 6;
	} else if(len < 65536) {
		frame[1] = 0xFE; /* MASK=1, extended payload length (16-bit) */
		frame[2] = (len >> 8) & 0xFF;
		frame[3] = len & 0xFF;
		frame[4] = mask[0];
		frame[5] = mask[1];
		frame[6] = mask[2];
		frame[7] = mask[3];
		frame_len = 8;
	} else {
		frame[1] = 0xFF; /* MASK=1, extended payload length (64-bit) */
		/* For simplicity, assume len < 2^32 */
		frame[2] = 0;
		frame[3] = 0;
		frame[4] = 0;
		frame[5] = 0;
		frame[6] = (len >> 24) & 0xFF;
		frame[7] = (len >> 16) & 0xFF;
		frame[8] = (len >> 8) & 0xFF;
		frame[9] = len & 0xFF;
		frame[10] = mask[0];
		frame[11] = mask[1];
		frame[12] = mask[2];
		frame[13] = mask[3];
		frame_len = 14;
	}

	/* Send frame header */
	sent = frame_len;
	rv = apr_socket_send(ws_client->socket, (char*)frame, &sent);
	if(rv != APR_SUCCESS || sent != frame_len) {
		apt_log(WEBSOCKET_RECOG_LOG_MARK,APT_PRIO_ERROR,"Failed to Send WebSocket Frame Header");
		return FALSE;
	}

	/* Apply mask to payload and send */
	/* Note: In production, should allocate temporary buffer for masked data */
	/* For simplicity, we send masked data directly (modifies original data) */
	if(len > 0) {
		char *masked_data = (char*)apr_palloc(ws_client->pool, len);
		if(!masked_data) {
			return FALSE;
		}
		for(i = 0; i < len; i++) {
			masked_data[i] = data[i] ^ mask[i % 4];
		}
		
		payload_len = len;
		rv = apr_socket_send(ws_client->socket, masked_data, &payload_len);
		if(rv != APR_SUCCESS || payload_len != len) {
			apt_log(WEBSOCKET_RECOG_LOG_MARK,APT_PRIO_ERROR,"Failed to Send WebSocket Payload");
			return FALSE;
		}
	}

	return TRUE;
}

static apt_bool_t websocket_client_receive(websocket_client_t *ws_client, char *buffer, apr_size_t *len)
{
	/* Simplified - in production implement full WebSocket frame parsing */
	return FALSE;
}

static void websocket_client_destroy(websocket_client_t *ws_client)
{
	if(ws_client->socket) {
		apr_socket_close(ws_client->socket);
		ws_client->socket = NULL;
	}
	ws_client->connected = FALSE;
}

static apt_bool_t websocket_recog_msg_signal(websocket_recog_msg_type_e type, mrcp_engine_channel_t *channel, mrcp_message_t *request)
{
	apt_bool_t status = FALSE;
	websocket_recog_channel_t *demo_channel = channel->method_obj;
	websocket_recog_engine_t *demo_engine = demo_channel->websocket_engine;
	apt_task_t *task = apt_consumer_task_base_get(demo_engine->task);
	apt_task_msg_t *msg = apt_task_msg_get(task);
	if(msg) {
		websocket_recog_msg_t *demo_msg;
		msg->type = TASK_MSG_USER;
		demo_msg = (websocket_recog_msg_t*) msg->data;

		demo_msg->type = type;
		demo_msg->channel = channel;
		demo_msg->request = request;
		status = apt_task_msg_signal(task,msg);
	}
	return status;
}

static apt_bool_t websocket_recog_msg_process(apt_task_t *task, apt_task_msg_t *msg)
{
	websocket_recog_msg_t *demo_msg = (websocket_recog_msg_t*)msg->data;
	switch(demo_msg->type) {
		case WEBSOCKET_RECOG_MSG_OPEN_CHANNEL:
			/* open channel and send asynch response */
			mrcp_engine_channel_open_respond(demo_msg->channel,TRUE);
			break;
		case WEBSOCKET_RECOG_MSG_CLOSE_CHANNEL:
		{
			/* close channel, make sure there is no activity and send asynch response */
			websocket_recog_channel_t *recog_channel = demo_msg->channel->method_obj;
			if(recog_channel->ws_client) {
				websocket_client_destroy(recog_channel->ws_client);
			}

			mrcp_engine_channel_close_respond(demo_msg->channel);
			break;
		}
		case WEBSOCKET_RECOG_MSG_REQUEST_PROCESS:
			websocket_recog_channel_request_dispatch(demo_msg->channel,demo_msg->request);
			break;
		case WEBSOCKET_RECOG_MSG_SEND_AUDIO:
		{
			/* P1: Send audio data via WebSocket in background task (non-blocking) */
			websocket_recog_channel_t *recog_channel = demo_msg->channel->method_obj;
			if(recog_channel->ws_client && recog_channel->ws_client->connected && 
			   recog_channel->audio_buffer_pos > 0) {
				/* Send audio data via WebSocket */
				if(websocket_client_send(recog_channel->ws_client, 
										recog_channel->audio_buffer, 
										recog_channel->audio_buffer_pos)) {
					recog_channel->audio_buffer_pos = 0; /* Clear buffer after sending */
					/* In a full implementation, we would wait for WebSocket response */
					/* For now, simulate success */
					websocket_recog_recognition_complete(recog_channel, RECOGNIZER_COMPLETION_CAUSE_SUCCESS);
				} else {
					apt_log(WEBSOCKET_RECOG_LOG_MARK,APT_PRIO_ERROR,"Failed to Send Audio Data via WebSocket");
					websocket_recog_recognition_complete(recog_channel, RECOGNIZER_COMPLETION_CAUSE_ERROR);
				}
			}
			break;
		}
		default:
			break;
	}
	return TRUE;
}

