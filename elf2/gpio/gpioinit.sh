#!/bin/bash
# GPIO 139/116 初始化脚本
# 用于 ELF2 (RK3588) 开发板，开机自动配置 GPIO
# 配合 systemd 服务 gpio139.service 使用

# ===== GPIO 139：输入（光电传感器触发）=====
if [ -d "/sys/class/gpio/gpio139" ]; then
    echo "GPIO 139 already exported"
else
    echo 139 > /sys/class/gpio/export
    echo "GPIO 139 exported"
    sleep 0.1
fi

echo "in" > /sys/class/gpio/gpio139/direction
echo "GPIO 139 direction set to input"
chmod -R 777 /sys/class/gpio/gpio139 2>/dev/null || true

# ===== GPIO 116：输出（蜂鸣器控制）=====
if [ -d "/sys/class/gpio/gpio116" ]; then
    echo "GPIO 116 already exported"
else
    echo 116 > /sys/class/gpio/export
    echo "GPIO 116 exported"
    sleep 0.1
fi

echo "out" > /sys/class/gpio/gpio116/direction
echo "GPIO 116 direction set to output"
chmod -R 777 /sys/class/gpio/gpio116 2>/dev/null || true
