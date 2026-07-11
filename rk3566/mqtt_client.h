#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <string>

// 初始化 MQTT 连接（长连接，保持后台 socket）
// host: broker IP 地址, port: broker 端口
// client_id: MQTT 客户端 ID, device_id: 设备标识（用于 topic 组装）
bool mqtt_init(const char* host, int port, const char* client_id, const char* device_id);

// 使用上次保存的参数自动重连
bool mqtt_reconnect();

// 发布消息到 topic
// 旧上位机：自动拼接为 elf2/{device_id}/{topic_suffix}
// 新上位机：topic_suffix 直接作为完整 topic 透传（不拼接前缀）
// 通过 mqtt_client.cpp 顶部 #define USE_NEW_HOST 切换
// payload: JSON 字符串（或任意文本）
// 返回 true 表示报文已写入 socket（不保证 broker 一定收到）
bool mqtt_publish(const char* topic_suffix, const char* payload);

// 检查当前 socket 是否仍有效
bool mqtt_is_connected();

// 发送 DISCONNECT 并关闭 socket
void mqtt_close();

#endif
