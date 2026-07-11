//
// Created on 2026-07-02.
//
// 轻量级 MQTT 客户端（纯 socket，零外部依赖）
// 仅支持 CONNECT + PUBLISH QoS 0 + DISCONNECT
// 兼容 MQTT 3.1.1
// 带自动重连参数保存
//
// ========== 上位机切换宏 ==========
// 0 = 旧上位机（topic 拼接为 elf2/{device_id}/{topic_suffix}）
// 1 = 新上位机（topic 直接透传，不拼接前缀）
#define USE_NEW_HOST 1
// ==================================

#include "mqtt_client.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <cstring>
#include <cerrno>
#include <hilog/log.h>
#include <napi/native_api.h>

#undef LOG_DOMAIN
#define LOG_DOMAIN 0x3200
#undef LOG_TAG
#define LOG_TAG "MqttClient"

static int g_sock = -1;
static std::string g_device_id;

// 保存连接参数，用于自动重连
static char g_last_host[64] = {0};
static int g_last_port = 1883;
static char g_last_client_id[64] = {0};
static char g_last_device_id[64] = {0};

// ========== 内部工具函数 ==========

// MQTT 剩余长度变长编码（最多 4 字节）
static int encode_remaining_length(uint8_t* buf, int len) {
    int idx = 0;
    do {
        uint8_t byte = len % 128;
        len /= 128;
        if (len > 0) byte |= 0x80;
        buf[idx++] = byte;
    } while (len > 0);
    return idx;
}

static bool send_all(int fd, const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(fd, p + total, len - total, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

static bool recv_all(int fd, void* data, size_t len, int timeout_ms) {
    uint8_t* p = static_cast<uint8_t*>(data);
    size_t total = 0;

    while (total < len) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int ret = select(fd + 1, &fds, nullptr, nullptr, &tv);
        if (ret <= 0) return false;

        ssize_t n = recv(fd, p + total, len - total, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

// ========== MQTT 核心逻辑 ==========

static bool mqtt_connect_internal(const char* host, int port, const char* client_id) {
    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock < 0) {
        OH_LOG_ERROR(LOG_APP, "mqtt socket() failed: %{public}d", errno);
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        OH_LOG_ERROR(LOG_APP, "mqtt inet_pton() failed");
        close(g_sock);
        g_sock = -1;
        return false;
    }

    // 非阻塞 connect + select 超时（防止 UI 卡死）
    int flags = fcntl(g_sock, F_GETFL, 0);
    fcntl(g_sock, F_SETFL, flags | O_NONBLOCK);

    int rc = connect(g_sock, (struct sockaddr*)&addr, sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        OH_LOG_ERROR(LOG_APP, "mqtt connect() failed immediately: %{public}d", errno);
        close(g_sock);
        g_sock = -1;
        return false;
    }

    if (rc != 0) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(g_sock, &fds);
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;

        int ret = select(g_sock + 1, nullptr, &fds, nullptr, &tv);
        if (ret <= 0) {
            OH_LOG_ERROR(LOG_APP, "mqtt connect() timeout or select error: %{public}d", errno);
            close(g_sock);
            g_sock = -1;
            return false;
        }

        int so_error;
        socklen_t len = sizeof(so_error);
        getsockopt(g_sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
        if (so_error != 0) {
            OH_LOG_ERROR(LOG_APP, "mqtt connect() async error: %{public}d", so_error);
            close(g_sock);
            g_sock = -1;
            return false;
        }
    }

    // connect 成功，恢复阻塞模式
    fcntl(g_sock, F_SETFL, flags & ~O_NONBLOCK);

    // 构建 CONNECT 报文
    uint8_t buf[512];
    int idx = 0;
    buf[idx++] = 0x10; // CONNECT 固定报头

    int cid_len = static_cast<int>(strlen(client_id));
    int payload_len = 2 + 4 + 1 + 1 + 2 + 2 + cid_len;
    int rem_len = encode_remaining_length(&buf[idx], payload_len);
    idx += rem_len;

    buf[idx++] = 0x00; buf[idx++] = 0x04;
    buf[idx++] = 'M'; buf[idx++] = 'Q'; buf[idx++] = 'T'; buf[idx++] = 'T';
    buf[idx++] = 0x04;
    buf[idx++] = 0x02; // Clean Session
    buf[idx++] = 0x00; buf[idx++] = 0x3C;

    buf[idx++] = (cid_len >> 8) & 0xFF;
    buf[idx++] = cid_len & 0xFF;
    memcpy(&buf[idx], client_id, cid_len);
    idx += cid_len;

    if (!send_all(g_sock, buf, idx)) {
        OH_LOG_ERROR(LOG_APP, "mqtt send CONNECT failed");
        close(g_sock);
        g_sock = -1;
        return false;
    }

    uint8_t ack[4];
    if (!recv_all(g_sock, ack, 4, 5000)) {
        OH_LOG_ERROR(LOG_APP, "mqtt recv CONNACK failed / timeout");
        close(g_sock);
        g_sock = -1;
        return false;
    }

    if (ack[0] != 0x20 || ack[1] != 0x02 || ack[3] != 0x00) {
        OH_LOG_ERROR(LOG_APP, "mqtt CONNACK invalid");
        close(g_sock);
        g_sock = -1;
        return false;
    }

    OH_LOG_INFO(LOG_APP, "mqtt CONNECT success, client_id=%{public}s", client_id);
    return true;
}

bool mqtt_init(const char* host, int port, const char* client_id, const char* device_id) {
    if (g_sock >= 0) {
        mqtt_close();
    }
    g_device_id = (device_id && device_id[0]) ? device_id : "unknown";

    // 保存参数用于自动重连
    if (host) {
        strncpy(g_last_host, host, sizeof(g_last_host) - 1);
        g_last_host[sizeof(g_last_host) - 1] = '\0';
    }
    g_last_port = port;
    if (client_id) {
        strncpy(g_last_client_id, client_id, sizeof(g_last_client_id) - 1);
        g_last_client_id[sizeof(g_last_client_id) - 1] = '\0';
    }
    if (device_id) {
        strncpy(g_last_device_id, device_id, sizeof(g_last_device_id) - 1);
        g_last_device_id[sizeof(g_last_device_id) - 1] = '\0';
    }

    return mqtt_connect_internal(host, port, client_id);
}

bool mqtt_reconnect() {
    if (g_last_host[0] == '\0' || g_last_client_id[0] == '\0') {
        OH_LOG_WARN(LOG_APP, "mqtt_reconnect: no saved params");
        return false;
    }
    if (g_sock >= 0) {
        mqtt_close();
    }
    g_device_id = g_last_device_id;
    OH_LOG_INFO(LOG_APP, "mqtt_reconnect: host=%{public}s, port=%{public}d", g_last_host, g_last_port);
    return mqtt_connect_internal(g_last_host, g_last_port, g_last_client_id);
}

bool mqtt_publish(const char* topic_suffix, const char* payload) {
    if (g_sock < 0 || !topic_suffix || !payload) {
        return false;
    }

    std::string topic_str;
#if USE_NEW_HOST
    topic_str = topic_suffix;   // 新上位机：直接透传完整 topic
#else
    topic_str = "elf2/" + g_device_id + "/" + topic_suffix;  // 旧上位机：拼接前缀
#endif
    const char* topic = topic_str.c_str();
    int tlen = static_cast<int>(topic_str.length());
    int plen = static_cast<int>(strlen(payload));

    int var_len = 2 + tlen + plen;
    uint8_t rem_buf[4];
    int rem_len = encode_remaining_length(rem_buf, var_len);

    uint8_t header[1 + 4];
    header[0] = 0x30;
    memcpy(&header[1], rem_buf, rem_len);
    if (!send_all(g_sock, header, 1 + rem_len)) {
        goto fail;
    }

    uint8_t topic_len[2];
    topic_len[0] = (tlen >> 8) & 0xFF;
    topic_len[1] = tlen & 0xFF;
    if (!send_all(g_sock, topic_len, 2)) {
        goto fail;
    }

    if (!send_all(g_sock, topic, tlen)) {
        goto fail;
    }

    if (!send_all(g_sock, payload, plen)) {
        goto fail;
    }

    return true;

fail:
    OH_LOG_WARN(LOG_APP, "mqtt_publish send failed, errno=%{public}d", errno);
    close(g_sock);
    g_sock = -1;
    return false;
}

bool mqtt_is_connected() {
    return g_sock >= 0;
}

void mqtt_close() {
    if (g_sock >= 0) {
        uint8_t disc[2] = {0xE0, 0x00};
        send_all(g_sock, disc, 2);
        close(g_sock);
        g_sock = -1;
    }
    g_device_id.clear();
}

// ========== NAPI 封装 ==========

// connectMqtt(host: string, port: number, clientId: string, deviceId: string): boolean
napi_value ConnectMqtt(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 4) {
        napi_value result;
        napi_get_boolean(env, false, &result);
        return result;
    }

    char host[64] = {0};
    size_t host_len = 0;
    napi_get_value_string_utf8(env, args[0], host, sizeof(host), &host_len);

    int32_t port = 1883;
    napi_get_value_int32(env, args[1], &port);

    char client_id[64] = {0};
    size_t cid_len = 0;
    napi_get_value_string_utf8(env, args[2], client_id, sizeof(client_id), &cid_len);

    char device_id[64] = {0};
    size_t did_len = 0;
    napi_get_value_string_utf8(env, args[3], device_id, sizeof(device_id), &did_len);

    bool ok = mqtt_init(host, port, client_id, device_id);
    napi_value result;
    napi_get_boolean(env, ok, &result);
    return result;
}

// reconnectMqtt(): boolean
napi_value ReconnectMqtt(napi_env env, napi_callback_info info) {
    (void)env;
    (void)info;
    bool ok = mqtt_reconnect();
    napi_value result;
    napi_get_boolean(env, ok, &result);
    return result;
}

// publishMqtt(topicSuffix: string, payload: string): boolean
napi_value PublishMqtt(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 2) {
        napi_value result;
        napi_get_boolean(env, false, &result);
        return result;
    }

    char topic_suffix[128] = {0};
    size_t ts_len = 0;
    napi_get_value_string_utf8(env, args[0], topic_suffix, sizeof(topic_suffix), &ts_len);

    char payload[2048] = {0};
    size_t p_len = 0;
    napi_get_value_string_utf8(env, args[1], payload, sizeof(payload), &p_len);

    bool ok = mqtt_publish(topic_suffix, payload);
    napi_value result;
    napi_get_boolean(env, ok, &result);
    return result;
}

// disconnectMqtt(): void
napi_value DisconnectMqtt(napi_env env, napi_callback_info info) {
    (void)env;
    (void)info;
    mqtt_close();
    return nullptr;
}
