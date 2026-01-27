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

/**
 * @file ws_client.c
 * @brief WebSocket client implementation
 */

#include "ws_client.h"
#include <apr_strings.h>
#include <apr_base64.h>
#include <apr_time.h>
#include <apr_uuid.h>
#include <string.h>

/** Internal logging macro */
#define WS_LOG(client, prio, fmt, ...) \
    apt_log(APT_LOG_MARK, prio, "[WS %s:%d] " fmt, \
            (client)->config.host, (client)->config.port, ##__VA_ARGS__)

/*******************************************************************************
 * Internal Helper Functions
 ******************************************************************************/

/** Generate random masking key */
static void ws_generate_mask(unsigned char mask[4])
{
    apr_time_t now = apr_time_now();
    mask[0] = (unsigned char)(now & 0xFF);
    mask[1] = (unsigned char)((now >> 8) & 0xFF);
    mask[2] = (unsigned char)((now >> 16) & 0xFF);
    mask[3] = (unsigned char)((now >> 24) & 0xFF);
}

/** Generate WebSocket key for handshake */
static char* ws_generate_key(apr_pool_t *pool)
{
    unsigned char random_key[16];
    apr_uuid_t uuid;
    char *key;
    
    apr_uuid_get(&uuid);
    memcpy(random_key, uuid.data, 16);
    
    key = apr_palloc(pool, apr_base64_encode_len(16) + 1);
    if (key) {
        apr_base64_encode(key, (char*)random_key, 16);
    }
    return key;
}

/** Build WebSocket frame header */
static apr_size_t ws_build_frame_header(
    unsigned char *header,
    unsigned char opcode,
    apr_size_t payload_len,
    unsigned char mask[4])
{
    apr_size_t header_len = 0;
    
    /* FIN=1, opcode */
    header[0] = WS_FIN_BIT | (opcode & 0x0F);
    
    if (payload_len < WS_PAYLOAD_LEN_16BIT) {
        header[1] = WS_MASK_BIT | (unsigned char)payload_len;
        memcpy(&header[2], mask, 4);
        header_len = 6;
    } else if (payload_len < 65536) {
        header[1] = WS_MASK_BIT | WS_PAYLOAD_LEN_16BIT;
        header[2] = (payload_len >> 8) & 0xFF;
        header[3] = payload_len & 0xFF;
        memcpy(&header[4], mask, 4);
        header_len = 8;
    } else {
        header[1] = WS_MASK_BIT | WS_PAYLOAD_LEN_64BIT;
        memset(&header[2], 0, 4);
        header[6] = (payload_len >> 24) & 0xFF;
        header[7] = (payload_len >> 16) & 0xFF;
        header[8] = (payload_len >> 8) & 0xFF;
        header[9] = payload_len & 0xFF;
        memcpy(&header[10], mask, 4);
        header_len = 14;
    }
    
    return header_len;
}

/** Apply mask to data */
static void ws_mask_data(char *data, apr_size_t len, const unsigned char mask[4])
{
    apr_size_t i;
    for (i = 0; i < len; i++) {
        data[i] ^= mask[i % 4];
    }
}

/** Send raw data through socket */
static apt_bool_t ws_socket_send(ws_client_t *client, const char *data, apr_size_t len)
{
    apr_status_t rv;
    apr_size_t sent = 0;
    apr_size_t remaining = len;
    
    while (remaining > 0) {
        apr_size_t to_send = remaining;
        rv = apr_socket_send(client->socket, data + sent, &to_send);
        if (rv != APR_SUCCESS) {
            WS_LOG(client, APT_PRIO_ERROR, "Socket send failed: %d", rv);
            return FALSE;
        }
        sent += to_send;
        remaining -= to_send;
    }
    
    return TRUE;
}

/** Receive raw data from socket */
static apt_bool_t ws_socket_recv(ws_client_t *client, char *buffer, apr_size_t *len)
{
    apr_status_t rv;
    
    rv = apr_socket_recv(client->socket, buffer, len);
    if (rv != APR_SUCCESS) {
        if (APR_STATUS_IS_TIMEUP(rv) || APR_STATUS_IS_EAGAIN(rv)) {
            *len = 0;
            return TRUE;  /* Timeout is not an error */
        }
        return FALSE;
    }
    
    return TRUE;
}

/** Receive exact number of bytes */
static apt_bool_t ws_socket_recv_exact(ws_client_t *client, char *buffer, apr_size_t len)
{
    apr_size_t received = 0;
    apr_size_t remaining = len;
    apr_time_t start_time = apr_time_now();
    apr_interval_time_t timeout = client->config.recv_timeout * 10; /* Extended timeout for exact recv */
    
    while (remaining > 0) {
        apr_size_t to_recv = remaining;
        if (!ws_socket_recv(client, buffer + received, &to_recv)) {
            return FALSE;
        }
        
        if (to_recv > 0) {
            received += to_recv;
            remaining -= to_recv;
        } else {
            /* Check timeout */
            if (apr_time_now() - start_time > timeout) {
                WS_LOG(client, APT_PRIO_WARNING, "Receive timeout waiting for %zu bytes", remaining);
                return FALSE;
            }
            apr_sleep(10000); /* 10ms */
        }
    }
    
    return TRUE;
}

/*******************************************************************************
 * Client Lifecycle Implementation
 ******************************************************************************/

void ws_client_config_init(ws_client_config_t *config)
{
    if (!config) return;
    
    memset(config, 0, sizeof(*config));
    config->host = "localhost";
    config->port = 8080;
    config->path = "/";
    config->connect_timeout = WS_DEFAULT_CONNECT_TIMEOUT;
    config->recv_timeout = WS_DEFAULT_RECV_TIMEOUT;
    config->send_timeout = WS_DEFAULT_SEND_TIMEOUT;
    config->max_retries = WS_DEFAULT_MAX_RETRIES;
    config->retry_delay = WS_DEFAULT_RETRY_DELAY;
    config->max_frame_size = WS_DEFAULT_MAX_FRAME_SIZE;
    config->log_source = NULL;
}

ws_client_t* ws_client_create(apr_pool_t *pool, const ws_client_config_t *config)
{
    ws_client_t *client;
    
    if (!pool || !config) {
        return NULL;
    }
    
    client = apr_pcalloc(pool, sizeof(ws_client_t));
    if (!client) {
        return NULL;
    }
    
    client->pool = pool;
    client->socket = NULL;
    client->sa = NULL;
    client->state = WS_STATE_DISCONNECTED;
    client->retry_count = 0;
    client->last_activity = 0;
    
    /* Copy configuration */
    client->config = *config;
    client->config.host = apr_pstrdup(pool, config->host);
    client->config.path = apr_pstrdup(pool, config->path);
    
    /* Create mutex */
    if (apr_thread_mutex_create(&client->mutex, APR_THREAD_MUTEX_DEFAULT, pool) != APR_SUCCESS) {
        return NULL;
    }
    
    /* Allocate receive buffer */
    client->recv_buffer_size = 4096;
    client->recv_buffer = apr_palloc(pool, client->recv_buffer_size);
    client->recv_buffer_pos = 0;
    
    if (!client->recv_buffer) {
        return NULL;
    }
    
    return client;
}

void ws_client_destroy(ws_client_t *client)
{
    if (!client) return;
    
    ws_client_disconnect(client, TRUE);
    
    if (client->mutex) {
        apr_thread_mutex_destroy(client->mutex);
        client->mutex = NULL;
    }
}

apt_bool_t ws_client_connect(ws_client_t *client)
{
    apr_status_t rv;
    char request[2048];
    char response[4096];
    apr_size_t len;
    char *key;
    const char *host_header;
    
    if (!client) return FALSE;
    
    apr_thread_mutex_lock(client->mutex);
    
    if (client->state == WS_STATE_CONNECTED) {
        apr_thread_mutex_unlock(client->mutex);
        return TRUE;
    }
    
    client->state = WS_STATE_CONNECTING;
    
    WS_LOG(client, APT_PRIO_INFO, "Connecting to %s", client->config.path);
    
    /* Create socket */
    rv = apr_socket_create(&client->socket, APR_INET, SOCK_STREAM, APR_PROTO_TCP, client->pool);
    if (rv != APR_SUCCESS) {
        WS_LOG(client, APT_PRIO_ERROR, "Failed to create socket");
        client->state = WS_STATE_ERROR;
        apr_thread_mutex_unlock(client->mutex);
        return FALSE;
    }
    
    /* Set socket timeout */
    apr_socket_timeout_set(client->socket, client->config.connect_timeout);
    
    /* Resolve hostname */
    rv = apr_sockaddr_info_get(&client->sa, client->config.host, APR_INET, 
                                client->config.port, 0, client->pool);
    if (rv != APR_SUCCESS) {
        WS_LOG(client, APT_PRIO_ERROR, "Failed to resolve hostname");
        apr_socket_close(client->socket);
        client->socket = NULL;
        client->state = WS_STATE_ERROR;
        apr_thread_mutex_unlock(client->mutex);
        return FALSE;
    }
    
    /* Connect */
    rv = apr_socket_connect(client->socket, client->sa);
    if (rv != APR_SUCCESS) {
        WS_LOG(client, APT_PRIO_ERROR, "Failed to connect");
        apr_socket_close(client->socket);
        client->socket = NULL;
        client->state = WS_STATE_ERROR;
        apr_thread_mutex_unlock(client->mutex);
        return FALSE;
    }
    
    /* Generate WebSocket key */
    key = ws_generate_key(client->pool);
    if (!key) {
        apr_socket_close(client->socket);
        client->socket = NULL;
        client->state = WS_STATE_ERROR;
        apr_thread_mutex_unlock(client->mutex);
        return FALSE;
    }
    
    /* Build handshake request */
    host_header = apr_psprintf(client->pool, "%s:%d", client->config.host, client->config.port);
    apr_snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        client->config.path, host_header, key);
    
    /* Send handshake */
    len = strlen(request);
    if (!ws_socket_send(client, request, len)) {
        WS_LOG(client, APT_PRIO_ERROR, "Failed to send handshake");
        apr_socket_close(client->socket);
        client->socket = NULL;
        client->state = WS_STATE_ERROR;
        apr_thread_mutex_unlock(client->mutex);
        return FALSE;
    }
    
    /* Set receive timeout for handshake response */
    apr_socket_timeout_set(client->socket, client->config.connect_timeout);
    
    /* Receive response */
    len = sizeof(response) - 1;
    rv = apr_socket_recv(client->socket, response, &len);
    if (rv != APR_SUCCESS || len == 0) {
        WS_LOG(client, APT_PRIO_ERROR, "Failed to receive handshake response");
        apr_socket_close(client->socket);
        client->socket = NULL;
        client->state = WS_STATE_ERROR;
        apr_thread_mutex_unlock(client->mutex);
        return FALSE;
    }
    response[len] = '\0';
    
    /* Check for 101 Switching Protocols */
    if (strstr(response, "101") == NULL) {
        WS_LOG(client, APT_PRIO_ERROR, "Handshake failed: %s", response);
        apr_socket_close(client->socket);
        client->socket = NULL;
        client->state = WS_STATE_ERROR;
        apr_thread_mutex_unlock(client->mutex);
        return FALSE;
    }
    
    /* Set socket to non-blocking mode for polling */
    apr_socket_timeout_set(client->socket, client->config.recv_timeout);
    
    client->state = WS_STATE_CONNECTED;
    client->last_activity = apr_time_now();
    client->retry_count = 0;
    
    WS_LOG(client, APT_PRIO_INFO, "Connected successfully");
    
    apr_thread_mutex_unlock(client->mutex);
    return TRUE;
}

apt_bool_t ws_client_connect_with_retry(ws_client_t *client)
{
    int i;
    
    if (!client) return FALSE;
    
    for (i = 0; i <= client->config.max_retries; i++) {
        if (ws_client_connect(client)) {
            return TRUE;
        }
        
        client->retry_count = i + 1;
        
        if (i < client->config.max_retries) {
            WS_LOG(client, APT_PRIO_WARNING, "Connection failed, retry %d/%d", 
                   i + 1, client->config.max_retries);
            apr_sleep(client->config.retry_delay);
        }
    }
    
    WS_LOG(client, APT_PRIO_ERROR, "All connection retries exhausted");
    return FALSE;
}

void ws_client_disconnect(ws_client_t *client, apt_bool_t send_close)
{
    if (!client) return;
    
    apr_thread_mutex_lock(client->mutex);
    
    if (client->socket) {
        if (send_close && client->state == WS_STATE_CONNECTED) {
            /* Send close frame */
            unsigned char close_frame[6];
            unsigned char mask[4];
            apr_size_t len = 6;
            
            ws_generate_mask(mask);
            close_frame[0] = WS_FIN_BIT | WS_OPCODE_CLOSE;
            close_frame[1] = WS_MASK_BIT | 0;
            memcpy(&close_frame[2], mask, 4);
            
            apr_socket_send(client->socket, (char*)close_frame, &len);
        }
        
        apr_socket_close(client->socket);
        client->socket = NULL;
    }
    
    client->state = WS_STATE_DISCONNECTED;
    
    apr_thread_mutex_unlock(client->mutex);
    
    WS_LOG(client, APT_PRIO_INFO, "Disconnected");
}

apt_bool_t ws_client_is_connected(ws_client_t *client)
{
    apt_bool_t connected;
    
    if (!client) return FALSE;
    
    apr_thread_mutex_lock(client->mutex);
    connected = (client->state == WS_STATE_CONNECTED);
    apr_thread_mutex_unlock(client->mutex);
    
    return connected;
}

apt_bool_t ws_client_ensure_connected(ws_client_t *client)
{
    if (!client) return FALSE;
    
    if (ws_client_is_connected(client)) {
        return TRUE;
    }
    
    return ws_client_connect_with_retry(client);
}

/*******************************************************************************
 * Send Functions Implementation
 ******************************************************************************/

static apt_bool_t ws_client_send_frame(ws_client_t *client, unsigned char opcode, 
                                        const char *data, apr_size_t len)
{
    unsigned char header[14];
    apr_size_t header_len;
    unsigned char mask[4];
    char *masked_data = NULL;
    apt_bool_t result = FALSE;
    
    if (!client) return FALSE;
    
    apr_thread_mutex_lock(client->mutex);
    
    if (client->state != WS_STATE_CONNECTED || !client->socket) {
        apr_thread_mutex_unlock(client->mutex);
        return FALSE;
    }
    
    /* Check payload size limit */
    if (len > client->config.max_frame_size) {
        WS_LOG(client, APT_PRIO_ERROR, "Payload size %zu exceeds limit %zu", 
               len, client->config.max_frame_size);
        apr_thread_mutex_unlock(client->mutex);
        return FALSE;
    }
    
    /* Generate mask and build header */
    ws_generate_mask(mask);
    header_len = ws_build_frame_header(header, opcode, len, mask);
    
    /* Send header */
    if (!ws_socket_send(client, (char*)header, header_len)) {
        client->state = WS_STATE_ERROR;
        apr_thread_mutex_unlock(client->mutex);
        return FALSE;
    }
    
    /* Mask and send payload */
    if (len > 0 && data) {
        masked_data = apr_palloc(client->pool, len);
        if (!masked_data) {
            apr_thread_mutex_unlock(client->mutex);
            return FALSE;
        }
        memcpy(masked_data, data, len);
        ws_mask_data(masked_data, len, mask);
        
        if (!ws_socket_send(client, masked_data, len)) {
            client->state = WS_STATE_ERROR;
            apr_thread_mutex_unlock(client->mutex);
            return FALSE;
        }
    }
    
    client->last_activity = apr_time_now();
    result = TRUE;
    
    apr_thread_mutex_unlock(client->mutex);
    return result;
}

apt_bool_t ws_client_send_text(ws_client_t *client, const char *text, apr_size_t len)
{
    return ws_client_send_frame(client, WS_OPCODE_TEXT, text, len);
}

apt_bool_t ws_client_send_binary(ws_client_t *client, const char *data, apr_size_t len)
{
    return ws_client_send_frame(client, WS_OPCODE_BINARY, data, len);
}

apt_bool_t ws_client_send_ping(ws_client_t *client)
{
    return ws_client_send_frame(client, WS_OPCODE_PING, NULL, 0);
}

apt_bool_t ws_client_send_close(ws_client_t *client, unsigned short code, const char *reason)
{
    char payload[128];
    apr_size_t payload_len = 0;
    
    if (code > 0) {
        payload[0] = (code >> 8) & 0xFF;
        payload[1] = code & 0xFF;
        payload_len = 2;
        
        if (reason) {
            apr_size_t reason_len = strlen(reason);
            if (reason_len > sizeof(payload) - 2) {
                reason_len = sizeof(payload) - 2;
            }
            memcpy(payload + 2, reason, reason_len);
            payload_len += reason_len;
        }
    }
    
    return ws_client_send_frame(client, WS_OPCODE_CLOSE, payload, payload_len);
}

/*******************************************************************************
 * Receive Functions Implementation
 ******************************************************************************/

apt_bool_t ws_client_poll(ws_client_t *client, apr_interval_time_t timeout)
{
    apr_pollfd_t pfd;
    apr_int32_t num;
    apr_status_t rv;
    
    if (!client || !ws_client_is_connected(client)) {
        return FALSE;
    }
    
    apr_thread_mutex_lock(client->mutex);
    
    pfd.desc_type = APR_POLL_SOCKET;
    pfd.desc.s = client->socket;
    pfd.reqevents = APR_POLLIN;
    pfd.rtnevents = 0;
    pfd.p = client->pool;
    pfd.client_data = NULL;
    
    rv = apr_poll(&pfd, 1, &num, timeout);
    
    apr_thread_mutex_unlock(client->mutex);
    
    if (rv == APR_SUCCESS && num > 0 && (pfd.rtnevents & APR_POLLIN)) {
        return TRUE;
    }
    
    return FALSE;
}

apt_bool_t ws_client_receive_frame(ws_client_t *client, ws_frame_t *frame)
{
    unsigned char header[2];
    apr_size_t header_len;
    apr_size_t payload_len;
    unsigned char opcode;
    apt_bool_t fin;
    apt_bool_t masked;
    unsigned char mask[4];
    
    if (!client || !frame) return FALSE;
    
    memset(frame, 0, sizeof(*frame));
    
    apr_thread_mutex_lock(client->mutex);
    
    if (client->state != WS_STATE_CONNECTED || !client->socket) {
        apr_thread_mutex_unlock(client->mutex);
        return FALSE;
    }
    
    /* Read first 2 bytes of header */
    header_len = 2;
    if (!ws_socket_recv(client, (char*)header, &header_len) || header_len == 0) {
        apr_thread_mutex_unlock(client->mutex);
        return FALSE;
    }
    
    if (header_len < 2) {
        /* Partial read, wait for more */
        apr_thread_mutex_unlock(client->mutex);
        return FALSE;
    }
    
    fin = (header[0] & WS_FIN_BIT) != 0;
    opcode = header[0] & 0x0F;
    masked = (header[1] & WS_MASK_BIT) != 0;
    payload_len = header[1] & WS_PAYLOAD_LEN_MASK;
    
    /* Extended payload length */
    if (payload_len == WS_PAYLOAD_LEN_16BIT) {
        unsigned char ext[2];
        if (!ws_socket_recv_exact(client, (char*)ext, 2)) {
            apr_thread_mutex_unlock(client->mutex);
            return FALSE;
        }
        payload_len = ((apr_size_t)ext[0] << 8) | ext[1];
    } else if (payload_len == WS_PAYLOAD_LEN_64BIT) {
        unsigned char ext[8];
        if (!ws_socket_recv_exact(client, (char*)ext, 8)) {
            apr_thread_mutex_unlock(client->mutex);
            return FALSE;
        }
        payload_len = ((apr_size_t)ext[4] << 24) | ((apr_size_t)ext[5] << 16) |
                      ((apr_size_t)ext[6] << 8) | ext[7];
    }
    
    /* Check payload size limit */
    if (payload_len > client->config.max_frame_size) {
        WS_LOG(client, APT_PRIO_ERROR, "Received frame size %zu exceeds limit", payload_len);
        client->state = WS_STATE_ERROR;
        apr_thread_mutex_unlock(client->mutex);
        return FALSE;
    }
    
    /* Read masking key if present */
    if (masked) {
        if (!ws_socket_recv_exact(client, (char*)mask, 4)) {
            apr_thread_mutex_unlock(client->mutex);
            return FALSE;
        }
    }
    
    /* Read payload */
    frame->opcode = opcode;
    frame->fin = fin;
    frame->payload_len = payload_len;
    
    if (payload_len > 0) {
        frame->payload = apr_palloc(client->pool, payload_len + 1);
        if (!frame->payload) {
            apr_thread_mutex_unlock(client->mutex);
            return FALSE;
        }
        
        if (!ws_socket_recv_exact(client, frame->payload, payload_len)) {
            apr_thread_mutex_unlock(client->mutex);
            return FALSE;
        }
        
        /* Unmask if needed */
        if (masked) {
            ws_mask_data(frame->payload, payload_len, mask);
        }
        
        frame->payload[payload_len] = '\0';
    }
    
    client->last_activity = apr_time_now();
    
    /* Handle control frames */
    if (opcode == WS_OPCODE_CLOSE) {
        WS_LOG(client, APT_PRIO_INFO, "Close frame received");
        client->state = WS_STATE_CLOSING;
    } else if (opcode == WS_OPCODE_PING) {
        /* Send pong */
        apr_thread_mutex_unlock(client->mutex);
        ws_client_send_frame(client, WS_OPCODE_PONG, frame->payload, frame->payload_len);
        apr_thread_mutex_lock(client->mutex);
    }
    
    apr_thread_mutex_unlock(client->mutex);
    return TRUE;
}

apt_bool_t ws_client_receive_text(ws_client_t *client, char *buffer, apr_size_t *len)
{
    ws_frame_t frame;
    
    if (!client || !buffer || !len || *len == 0) return FALSE;
    
    if (!ws_client_receive_frame(client, &frame)) {
        return FALSE;
    }
    
    if (frame.opcode != WS_OPCODE_TEXT) {
        return FALSE;
    }
    
    if (frame.payload_len < *len) {
        memcpy(buffer, frame.payload, frame.payload_len);
        buffer[frame.payload_len] = '\0';
        *len = frame.payload_len;
    } else {
        memcpy(buffer, frame.payload, *len - 1);
        buffer[*len - 1] = '\0';
    }
    
    return TRUE;
}

apt_bool_t ws_client_receive_binary(ws_client_t *client, char *buffer, apr_size_t *len)
{
    ws_frame_t frame;
    
    if (!client || !buffer || !len || *len == 0) return FALSE;
    
    if (!ws_client_receive_frame(client, &frame)) {
        return FALSE;
    }
    
    if (frame.opcode != WS_OPCODE_BINARY && frame.opcode != WS_OPCODE_CONTINUATION) {
        return FALSE;
    }
    
    if (frame.payload_len <= *len) {
        memcpy(buffer, frame.payload, frame.payload_len);
        *len = frame.payload_len;
    } else {
        memcpy(buffer, frame.payload, *len);
    }
    
    return TRUE;
}

/*******************************************************************************
 * Utility Functions Implementation
 ******************************************************************************/

ws_client_state_e ws_client_get_state(ws_client_t *client)
{
    ws_client_state_e state;
    
    if (!client) return WS_STATE_DISCONNECTED;
    
    apr_thread_mutex_lock(client->mutex);
    state = client->state;
    apr_thread_mutex_unlock(client->mutex);
    
    return state;
}

const char* ws_client_get_error(ws_client_t *client)
{
    if (!client) return "Invalid client";
    
    switch (client->state) {
        case WS_STATE_DISCONNECTED:
            return "Disconnected";
        case WS_STATE_CONNECTING:
            return "Connecting";
        case WS_STATE_CONNECTED:
            return "Connected (no error)";
        case WS_STATE_CLOSING:
            return "Connection closing";
        case WS_STATE_ERROR:
            return "Connection error";
        default:
            return "Unknown state";
    }
}

char* ws_json_escape_string(const char *str, apr_pool_t *pool)
{
    apr_size_t len;
    apr_size_t i, j;
    char *escaped;
    
    if (!str || !pool) {
        return apr_pstrdup(pool, "");
    }
    
    len = strlen(str);
    /* Worst case: every character needs escaping (6 chars for \uXXXX) */
    escaped = apr_palloc(pool, len * 6 + 1);
    if (!escaped) {
        return NULL;
    }
    
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
