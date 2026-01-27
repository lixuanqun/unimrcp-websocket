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

#ifndef WS_CLIENT_H
#define WS_CLIENT_H

/**
 * @file ws_client.h
 * @brief WebSocket client library for UniMRCP plugins
 */

#include <apr_pools.h>
#include <apr_network_io.h>
#include <apr_thread_mutex.h>
#include "apt_log.h"

APT_BEGIN_EXTERN_C

/*******************************************************************************
 * WebSocket Frame Constants
 ******************************************************************************/

/** WebSocket opcodes */
#define WS_OPCODE_CONTINUATION  0x00
#define WS_OPCODE_TEXT          0x01
#define WS_OPCODE_BINARY        0x02
#define WS_OPCODE_CLOSE         0x08
#define WS_OPCODE_PING          0x09
#define WS_OPCODE_PONG          0x0A

/** WebSocket frame bits */
#define WS_FIN_BIT              0x80
#define WS_MASK_BIT             0x80
#define WS_PAYLOAD_LEN_MASK     0x7F
#define WS_PAYLOAD_LEN_16BIT    126
#define WS_PAYLOAD_LEN_64BIT    127

/** Default configuration values */
#define WS_DEFAULT_CONNECT_TIMEOUT  (30 * 1000000)   /* 30 seconds */
#define WS_DEFAULT_RECV_TIMEOUT     (100000)         /* 100ms for polling */
#define WS_DEFAULT_SEND_TIMEOUT     (10 * 1000000)   /* 10 seconds */
#define WS_DEFAULT_MAX_RETRIES      3
#define WS_DEFAULT_RETRY_DELAY      (1000000)        /* 1 second */
#define WS_DEFAULT_MAX_FRAME_SIZE   (1024 * 1024)    /* 1MB */

/*******************************************************************************
 * Types and Structures
 ******************************************************************************/

/** Forward declarations */
typedef struct ws_client_t ws_client_t;
typedef struct ws_client_config_t ws_client_config_t;
typedef struct ws_frame_t ws_frame_t;

/** WebSocket client state */
typedef enum {
    WS_STATE_DISCONNECTED,
    WS_STATE_CONNECTING,
    WS_STATE_CONNECTED,
    WS_STATE_CLOSING,
    WS_STATE_ERROR
} ws_client_state_e;

/** WebSocket frame received */
struct ws_frame_t {
    unsigned char opcode;      /**< Frame opcode */
    apt_bool_t fin;           /**< FIN bit */
    char *payload;            /**< Payload data (allocated from pool) */
    apr_size_t payload_len;   /**< Payload length */
};

/** WebSocket client configuration */
struct ws_client_config_t {
    const char *host;                  /**< Server hostname */
    int port;                          /**< Server port */
    const char *path;                  /**< WebSocket path (e.g., "/tts") */
    apr_interval_time_t connect_timeout;  /**< Connection timeout */
    apr_interval_time_t recv_timeout;     /**< Receive timeout (for polling) */
    apr_interval_time_t send_timeout;     /**< Send timeout */
    int max_retries;                   /**< Max connection retry count */
    apr_interval_time_t retry_delay;   /**< Delay between retries */
    apr_size_t max_frame_size;         /**< Maximum frame payload size */
    apt_log_source_t *log_source;      /**< Log source for messages */
};

/** WebSocket client structure */
struct ws_client_t {
    apr_pool_t *pool;                  /**< Memory pool */
    apr_socket_t *socket;              /**< Socket handle */
    apr_sockaddr_t *sa;                /**< Socket address */
    ws_client_state_e state;           /**< Connection state */
    ws_client_config_t config;         /**< Configuration */
    apr_thread_mutex_t *mutex;         /**< Thread safety mutex */
    apr_time_t last_activity;          /**< Last activity timestamp */
    int retry_count;                   /**< Current retry count */
    char *recv_buffer;                 /**< Receive buffer */
    apr_size_t recv_buffer_size;       /**< Receive buffer size */
    apr_size_t recv_buffer_pos;        /**< Current position in recv buffer */
};

/*******************************************************************************
 * Client Lifecycle Functions
 ******************************************************************************/

/**
 * Create a new WebSocket client
 * @param pool Memory pool to use
 * @param config Client configuration
 * @return Created client or NULL on failure
 */
ws_client_t* ws_client_create(apr_pool_t *pool, const ws_client_config_t *config);

/**
 * Destroy a WebSocket client
 * @param client Client to destroy
 */
void ws_client_destroy(ws_client_t *client);

/**
 * Connect to WebSocket server
 * @param client Client instance
 * @return TRUE on success, FALSE on failure
 */
apt_bool_t ws_client_connect(ws_client_t *client);

/**
 * Connect with automatic retry on failure
 * @param client Client instance
 * @return TRUE on success, FALSE after all retries exhausted
 */
apt_bool_t ws_client_connect_with_retry(ws_client_t *client);

/**
 * Disconnect from WebSocket server
 * @param client Client instance
 * @param send_close Whether to send close frame
 */
void ws_client_disconnect(ws_client_t *client, apt_bool_t send_close);

/**
 * Check if client is connected
 * @param client Client instance
 * @return TRUE if connected
 */
apt_bool_t ws_client_is_connected(ws_client_t *client);

/**
 * Ensure client is connected (reconnect if needed)
 * @param client Client instance
 * @return TRUE if connected or reconnected successfully
 */
apt_bool_t ws_client_ensure_connected(ws_client_t *client);

/*******************************************************************************
 * Send Functions
 ******************************************************************************/

/**
 * Send a text message
 * @param client Client instance
 * @param text Text data to send
 * @param len Length of text data
 * @return TRUE on success
 */
apt_bool_t ws_client_send_text(ws_client_t *client, const char *text, apr_size_t len);

/**
 * Send a binary message
 * @param client Client instance
 * @param data Binary data to send
 * @param len Length of data
 * @return TRUE on success
 */
apt_bool_t ws_client_send_binary(ws_client_t *client, const char *data, apr_size_t len);

/**
 * Send a ping frame
 * @param client Client instance
 * @return TRUE on success
 */
apt_bool_t ws_client_send_ping(ws_client_t *client);

/**
 * Send a close frame
 * @param client Client instance
 * @param code Close status code (0 for none)
 * @param reason Close reason (can be NULL)
 * @return TRUE on success
 */
apt_bool_t ws_client_send_close(ws_client_t *client, unsigned short code, const char *reason);

/*******************************************************************************
 * Receive Functions
 ******************************************************************************/

/**
 * Poll for incoming data (non-blocking)
 * @param client Client instance
 * @param timeout Timeout in microseconds (0 for immediate return)
 * @return TRUE if data is available, FALSE otherwise
 */
apt_bool_t ws_client_poll(ws_client_t *client, apr_interval_time_t timeout);

/**
 * Receive a single frame (non-blocking with configured timeout)
 * @param client Client instance
 * @param frame Output frame structure (payload allocated from pool)
 * @return TRUE if frame received, FALSE if no data or error
 */
apt_bool_t ws_client_receive_frame(ws_client_t *client, ws_frame_t *frame);

/**
 * Receive text message (convenience wrapper)
 * @param client Client instance
 * @param buffer Output buffer
 * @param len In: buffer size, Out: received length
 * @return TRUE if text message received
 */
apt_bool_t ws_client_receive_text(ws_client_t *client, char *buffer, apr_size_t *len);

/**
 * Receive binary message (convenience wrapper)
 * @param client Client instance
 * @param buffer Output buffer
 * @param len In: buffer size, Out: received length
 * @return TRUE if binary message received
 */
apt_bool_t ws_client_receive_binary(ws_client_t *client, char *buffer, apr_size_t *len);

/*******************************************************************************
 * Utility Functions
 ******************************************************************************/

/**
 * Initialize default configuration
 * @param config Configuration structure to initialize
 */
void ws_client_config_init(ws_client_config_t *config);

/**
 * Get client state
 * @param client Client instance
 * @return Current state
 */
ws_client_state_e ws_client_get_state(ws_client_t *client);

/**
 * Get last error description
 * @param client Client instance
 * @return Error description string
 */
const char* ws_client_get_error(ws_client_t *client);

/**
 * Escape string for JSON
 * @param str String to escape
 * @param pool Memory pool for allocation
 * @return Escaped string
 */
char* ws_json_escape_string(const char *str, apr_pool_t *pool);

APT_END_EXTERN_C

#endif /* WS_CLIENT_H */
