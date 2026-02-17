#!/bin/bash

# 安装脚本，用于设置Intel GPU监控工具的setuid权限

echo "Setting up Intel GPU monitoring tools with setuid permissions..."

# 检查是否以root权限运行
if [ "$(id -u)" -ne 0 ]; then
    echo "This script needs to be run with sudo to set setuid permissions."
    echo "Please run: sudo ./install_tools.sh"
    exit 1
fi

# 设置工具的setuid权限
chown root:root i915_gpu_usage
chmod +s i915_gpu_usage

chown root:root i915_vmem
chmod +s i915_vmem

echo "Intel GPU monitoring tools have been set up with setuid permissions."
