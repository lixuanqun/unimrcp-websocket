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

#include "ws_client.h"
#include <apr_network_io.h>
#include <apr_strings.h>
#include <apr_base64.h>
#include <apr_sha1.h>
#include <apt_log.h>
#include <apt_pair.h>

#define WS_LOG_MARK   APT_LOG_MARK_DECLARE(APT_PRIO_INFO)

/* Magic string for WebSocket handshake (RFC 6455) */
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

struct ws_client_t {
    apr_pool_t         *pool;
    apr_socket_t       *sock;
    apr_sockaddr_t     *addr;
    ws_client_config_t  config;
    apt_bool_t          connected;
    char               *sec_key;
};

void ws_client_config_init(ws_client_config_t *config)
{
    config->host = "localhost";
    config->port = 8080;
    config->path = "/";
    config->recv_timeout = 100000; /* 100ms */
}

ws_client_t* ws_client_create(apr_pool_t *pool, const ws_client_config_t *config)
{
    ws_client_t *client = apr_palloc(pool, sizeof(ws_client_t));
    client->pool = pool;
    client->sock = NULL;
    client->addr = NULL;
    client->connected = FALSE;
    client->sec_key = NULL;
    
    if (config) {
        client->config = *config;
    } else {
        ws_client_config_init(&client->config);
    }
    
    return client;
}

void ws_client_destroy(ws_client_t *client)
{
    if (client->connected) {
        ws_client_disconnect(client, FALSE);
    }
}

static apt_bool_t ws_client_handshake(ws_client_t *client)
{
    char buf[4096];
    apr_size_t len;
    apr_status_t status;
    char *key_src = "dGhlIHNhbXBsZSBub25jZQ=="; /* Fixed key for simplicity, should be random */
    
    /* Send Upgrade Request */
    len = apr_snprintf(buf, sizeof(buf),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        client->config.path,
        client->config.host,
        client->config.port,
        key_src);

    status = apr_socket_send(client->sock, buf, &len);
    if (status != APR_SUCCESS) {
        apt_log(WS_LOG_MARK, APT_PRIO_WARNING, "Failed to send handshake");
        return FALSE;
    }

    /* Receive Response */
    /* Note: This is a very simplified response parser */
    len = sizeof(buf) - 1;
    status = apr_socket_recv(client->sock, buf, &len);
    if (status != APR_SUCCESS) {
        apt_log(WS_LOG_MARK, APT_PRIO_WARNING, "Failed to receive handshake response");
        return FALSE;
    }
    buf[len] = '\0';

    if (strstr(buf, "101 Switching Protocols") == NULL) {
        apt_log(WS_LOG_MARK, APT_PRIO_WARNING, "Invalid handshake response: %s", buf);
        return FALSE;
    }

    return TRUE;
}

apt_bool_t ws_client_connect(ws_client_t *client)
{
    apr_status_t status;
    
    if (client->connected) {
        return TRUE;
    }

    /* Resolve address */
    if (!client->addr) {
        status = apr_sockaddr_info_get(&client->addr, client->config.host, APR_INET, 
                                     (apr_port_t)client->config.port, 0, client->pool);
        if (status != APR_SUCCESS) {
            apt_log(WS_LOG_MARK, APT_PRIO_WARNING, "Failed to resolve address: %s:%d", 
                client->config.host, client->config.port);
            return FALSE;
        }
    }

    /* Create socket */
    status = apr_socket_create(&client->sock, client->addr->family, SOCK_STREAM, APR_PROTO_TCP, client->pool);
    if (status != APR_SUCCESS) {
        apt_log(WS_LOG_MARK, APT_PRIO_WARNING, "Failed to create socket");
        return FALSE;
    }

    /* Set timeout */
    apr_socket_timeout_set(client->sock, client->config.recv_timeout);

    /* Connect */
    status = apr_socket_connect(client->sock, client->addr);
    if (status != APR_SUCCESS) {
        apt_log(WS_LOG_MARK, APT_PRIO_WARNING, "Failed to connect");
        apr_socket_close(client->sock);
        client->sock = NULL;
        return FALSE;
    }

    /* Perform Handshake */
    if (ws_client_handshake(client) == FALSE) {
        apr_socket_close(client->sock);
        client->sock = NULL;
        return FALSE;
    }

    client->connected = TRUE;
    return TRUE;
}

apt_bool_t ws_client_disconnect(ws_client_t *client, apt_bool_t send_close)
{
    if (client->sock) {
        apr_socket_close(client->sock);
        client->sock = NULL;
    }
    client->connected = FALSE;
    return TRUE;
}

apt_bool_t ws_client_is_connected(const ws_client_t *client)
{
    return client->connected;
}

apt_bool_t ws_client_ensure_connected(ws_client_t *client)
{
    if (ws_client_is_connected(client)) {
        return TRUE;
    }
    return ws_client_connect(client);
}

static apt_bool_t ws_client_send_frame(ws_client_t *client, ws_opcode_e opcode, const void *payload, apr_size_t len)
{
    char header[14];
    apr_size_t header_len = 0;
    apr_size_t sent_len;
    apr_status_t status;
    unsigned char *p = (unsigned char*)header;
    
    /* Fixed mask for simplicity */
    unsigned char mask[4] = {0x11, 0x22, 0x33, 0x44};

    if (!client->connected || !client->sock) {
        return FALSE;
    }

    /* FIN + Opcode */
    *p++ = (unsigned char)(0x80 | opcode);

    /* Mask + Payload Length */
    if (len < 126) {
        *p++ = (unsigned char)(0x80 | len);
    } else if (len < 65536) {
        *p++ = (unsigned char)(0x80 | 126);
        *p++ = (unsigned char)((len >> 8) & 0xFF);
        *p++ = (unsigned char)(len & 0xFF);
    } else {
        *p++ = (unsigned char)(0x80 | 127);
        *p++ = (unsigned char)((len >> 56) & 0xFF);
        *p++ = (unsigned char)((len >> 48) & 0xFF);
        *p++ = (unsigned char)((len >> 40) & 0xFF);
        *p++ = (unsigned char)((len >> 32) & 0xFF);
        *p++ = (unsigned char)((len >> 24) & 0xFF);
        *p++ = (unsigned char)((len >> 16) & 0xFF);
        *p++ = (unsigned char)((len >> 8) & 0xFF);
        *p++ = (unsigned char)(len & 0xFF);
    }

    /* Masking Key */
    memcpy(p, mask, 4);
    p += 4;

    header_len = p - (unsigned char*)header;

    /* Send Header */
    sent_len = header_len;
    status = apr_socket_send(client->sock, header, &sent_len);
    if (status != APR_SUCCESS || sent_len != header_len) {
        return FALSE;
    }

    /* Send Payload (masked) */
    if (len > 0) {
        /* Allocate temp buffer for masked data */
        /* Note: For large payloads, masking in chunks would be better */
        char *masked_payload = apr_palloc(client->pool, len);
        const char *src = (const char*)payload;
        apr_size_t i;

        for (i = 0; i < len; i++) {
            masked_payload[i] = src[i] ^ mask[i % 4];
        }

        sent_len = len;
        status = apr_socket_send(client->sock, masked_payload, &sent_len);
        if (status != APR_SUCCESS || sent_len != len) {
            return FALSE;
        }
    }

    return TRUE;
}

apt_bool_t ws_client_send_text(ws_client_t *client, const char *text)
{
    return ws_client_send_frame(client, WS_OPCODE_TEXT, text, strlen(text));
}

apt_bool_t ws_client_send_binary(ws_client_t *client, const void *data, apr_size_t len)
{
    return ws_client_send_frame(client, WS_OPCODE_BINARY, data, len);
}

apt_bool_t ws_client_receive_frame(ws_client_t *client, ws_frame_t *frame)
{
    char buf[2];
    apr_size_t len = 2;
    apr_status_t status;
    unsigned char b1, b2;
    apr_uint64_t payload_len;
    
    if (!client->connected || !client->sock) {
        return FALSE;
    }

    /* Read first 2 bytes */
    status = apr_socket_recv(client->sock, buf, &len);
    if (status != APR_SUCCESS || len != 2) {
        /* This is expected on timeout */
        return FALSE;
    }

    b1 = (unsigned char)buf[0];
    b2 = (unsigned char)buf[1];

    frame->fin = (b1 & 0x80) != 0;
    frame->opcode = (ws_opcode_e)(b1 & 0x0F);
    
    /* Check Mask bit (server to client should NOT be masked) */
    if (b2 & 0x80) {
        /* Server sent masked frame, which is violation of RFC 6455 */
        /* But we can ignore it or handle it */
    }

    payload_len = b2 & 0x7F;

    if (payload_len == 126) {
        char ext_len[2];
        len = 2;
        apr_socket_recv(client->sock, ext_len, &len);
        payload_len = ((unsigned char)ext_len[0] << 8) | (unsigned char)ext_len[1];
    } else if (payload_len == 127) {
        char ext_len[8];
        len = 8;
        apr_socket_recv(client->sock, ext_len, &len);
        /* Ignoring high 32 bits for simplicity */
        payload_len = ((unsigned char)ext_len[4] << 24) | 
                      ((unsigned char)ext_len[5] << 16) | 
                      ((unsigned char)ext_len[6] << 8) | 
                      (unsigned char)ext_len[7];
    }

    frame->payload_len = (apr_size_t)payload_len;
    frame->payload = NULL;

    if (payload_len > 0) {
        /* Allocate buffer for payload */
        /* Note: Be careful with memory usage here. 
           In a real plugin, you might want to reuse a buffer. */
        frame->payload = apr_palloc(client->pool, frame->payload_len + 1);
        
        len = frame->payload_len;
        apr_size_t total_read = 0;
        
        while (total_read < len) {
            apr_size_t to_read = len - total_read;
            status = apr_socket_recv(client->sock, frame->payload + total_read, &to_read);
            if (status != APR_SUCCESS) {
                return FALSE;
            }
            total_read += to_read;
        }
        frame->payload[frame->payload_len] = '\0';
    }

    return TRUE;
}
