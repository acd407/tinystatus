# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 编译

```sh
make              # 编译主程序 tinystatus 和工具程序 (i915_gpu_usage, i915_vmem)
make clean        # 清理
DEBUG=1 make      # 开启 AddressSanitizer 调试模式
```

依赖: `libcjson`, `dbus-1`, `libpulse`, `libnl-3.0`, `libnl-genl-3.0`, `glib-2.0` (通过 pkg-config 检测)。

## 架构概述

### 事件驱动核心

主循环基于 `epoll_wait`，每个 epoll 事件通过 `events[i].data.u64` 索引模块，调用对应 `update` 方法。

### 模块注册机制

所有模块在 `init()` 中按顺序注册到全局数组 `modules[MOD_SIZE]`，注册顺序决定输出顺序。模块 ID 等于注册时的 `modules_cnt` 值（从 0 开始），即 'A' + ID。

每个模块通过 `INIT_BASE` 宏初始化 `module_t` 结构体，其中 `data` 字段指向模块私有数据结构。

### 模块结构

参见 `src/include/main.h` 中的 `module_t`：
- `update` — epoll 事件触发或定时器触发时调用
- `alter` — 鼠标点击事件时调用（由 stdin.c 从 i3bar 点击事件 JSON 中解析出 `name` 和 `button`）
- `del` — 程序退出时清理

### stdin 模块的 alter 分派

`stdin.c:70` 用 `name->valuestring[0] - 'A'` 作为模块索引，因此模块名首字母必须按注册顺序对应 'A', 'B', 'C'...。

### 外部工具程序

`src/tools/` 下的 `i915_gpu_usage` 和 `i915_vmem` 是独立编译的二进制程序，被 `intel_gpu.c` 通过 `TOOLS_DIR` 宏指定路径调用。

### 网络模块的无线状态

无线信号强度和速率通过 netlink (libnl) 查询 nl80211 内核接口，不使用 `/proc/net/wireless`。

## 新增模块

参考 README.md 中的"新模块指引"。基本步骤：
1. 在 `src/modules/` 下实现 `init_xxx` 函数，使用 `INIT_BASE` 注册
2. 在 `src/include/modules.h` 中声明
3. 在 `src/main.c` 的 `init()` 函数中调用

若模块需要 epoll 监听，在 `init_xxx` 中调用 `epoll_ctl` 注册文件描述符，并将 `module_id` 存入 `events.data.u64`。若需要定时更新，设置 `modules[module_id].interval = 1;`（timer 每秒触发一次）。若需要响应点击事件，设置 `modules[module_id].alter = alter;`。