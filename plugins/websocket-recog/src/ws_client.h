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
 * @brief Simple WebSocket Client
 */ 

#include <apr_pools.h>
#include <apt_bool.h>

/** WebSocket client Opaque declaration */
typedef struct ws_client_t ws_client_t;

/** WebSocket frame declaration */
typedef struct ws_frame_t ws_frame_t;

/** WebSocket opcodes */
typedef enum {
    WS_OPCODE_CONTINUATION = 0x0,
    WS_OPCODE_TEXT         = 0x1,
    WS_OPCODE_BINARY       = 0x2,
    WS_OPCODE_CLOSE        = 0x8,
    WS_OPCODE_PING         = 0x9,
    WS_OPCODE_PONG         = 0xA
} ws_opcode_e;

/** WebSocket frame structure */
struct ws_frame_t {
    ws_opcode_e opcode;
    char       *payload;
    apr_size_t  payload_len;
    apt_bool_t  fin;
};

/** WebSocket client config */
typedef struct ws_client_config_t ws_client_config_t;
struct ws_client_config_t {
    const char *host;
    int         port;
    const char *path;
    apr_size_t  recv_timeout; /* Microseconds */
};

/**
 * Initialize config with default values
 */
void ws_client_config_init(ws_client_config_t *config);

/**
 * Create WebSocket client
 */
ws_client_t* ws_client_create(apr_pool_t *pool, const ws_client_config_t *config);

/**
 * Destroy WebSocket client
 */
void ws_client_destroy(ws_client_t *client);

/**
 * Connect to server
 * @return TRUE if connected (or already connected), FALSE on failure
 */
apt_bool_t ws_client_connect(ws_client_t *client);

/**
 * Disconnect from server
 */
apt_bool_t ws_client_disconnect(ws_client_t *client, apt_bool_t send_close);

/**
 * Check if connected
 */
apt_bool_t ws_client_is_connected(const ws_client_t *client);

/**
 * Ensure connection (reconnect if needed)
 */
apt_bool_t ws_client_ensure_connected(ws_client_t *client);

/**
 * Send text message
 */
apt_bool_t ws_client_send_text(ws_client_t *client, const char *text);

/**
 * Send binary message
 */
apt_bool_t ws_client_send_binary(ws_client_t *client, const void *data, apr_size_t len);

/**
 * Receive frame
 * @param frame Pointer to frame structure to fill
 * @return TRUE if frame received, FALSE on timeout or error
 */
apt_bool_t ws_client_receive_frame(ws_client_t *client, ws_frame_t *frame);

#endif /* WS_CLIENT_H */
