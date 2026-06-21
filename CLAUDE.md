# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 编译

```sh
make              # 编译主程序 tinystatus 和工具程序 (i915_gpu_usage, i915_vmem)
make clean        # 清理
DEBUG=1 make      # 开启 AddressSanitizer 调试模式
```

依赖: `dbus-1`, `libpulse`, `libnl-3.0`, `libnl-genl-3.0`, `libnl-route-3.0` (通过 pkg-config 检测)。

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

## network 模块设计（参考实现）

`src/modules/network.c` 使用 4 个独立的 netlink socket，各司其职：

| socket | 协议 | 用途 | 复用方式 |
|--------|------|------|----------|
| `nlsock` | `NETLINK_GENERIC` → nl80211 | 无线信号强度(dBm)、TX 码率 | init 时创建，接口变更时销毁重建 |
| `eth_sock` | `NETLINK_ETHTOOL` | 链接速率(Mbps) | 首次 `get_link_speed()` 懒创建，长期复用 |
| `rtnl_sock` | `NETLINK_ROUTE` | 路由/链路事件监控 | init 时创建，非阻塞只收不发，epoll 触发 |
| `stat_sock` | `NETLINK_ROUTE` | 流量统计 + 路由发现 | init 时创建，请求-响应式使用 |

关键设计要点：
- 路由发现是**事件驱动**的：`rtnl_sock` 监听 `RTMGRP_IPV4_ROUTE | IPV6_ROUTE | LINK` 多播组，epoll 触发 `update()` 后立即重新发现，而非每秒轮询
- **只跟踪默认路由**对应的接口，不遍历所有接口。`discover_default_route()` 扫描 `RT_TABLE_MAIN` 中 dst 前缀长度为 0 的路由
- 流量统计使用 `rtnl_link_get_kernel()` 进行**单接口查询**，而非全量 link cache dump
- 链接速率通过 `ETHTOOL_MSG_LINKMODES_GET` 查询，替代 sysfs 读取
- 所有 netlink 回调（`station_info_cb`、`link_speed_cb`）只在 socket 初始化时注册一次，不每秒重复设置

## 模块 data 结构体设计规则

1. **持久配置**（sockets、delta 基准、cache）放在结构体上半部分
2. **回调暂存区**用 `buf_` 前缀，放在结构体末尾，表明它们是每次查询的临时缓冲区而非持久状态
3. 函数参数取舍：
   - 修改了 `data` 字段的 → 传 `data`
   - 只读已有状态的 helper → 传 `data`
   - 纯计算/格式化，不涉及 data 字段的 → 传明确参数
   - helper 需要特定依赖（如 socket）→ 若该依赖在 data 中则传 data，否则显式传参

## 新增模块

参考 README.md 中的"新模块指引"。基本步骤：
1. 在 `src/modules/` 下实现 `init_xxx` 函数，使用 `INIT_BASE` 注册
2. 在 `src/include/modules.h` 中声明
3. 在 `src/main.c` 的 `init()` 函数中调用

若模块需要 epoll 监听，在 `init_xxx` 中调用 `epoll_ctl` 注册文件描述符，并将 `module_id` 存入 `events.data.u64`。若需要定时更新，设置 `modules[module_id].interval = 1;`（timer 每秒触发一次）。若需要响应点击事件，设置 `modules[module_id].alter = alter;`。

新增模块优先使用 netlink 而非 procfs/sysfs 获取内核数据，libnl 库已作为依赖引入。