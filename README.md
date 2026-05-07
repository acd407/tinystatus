# tinystatus

`tinystatus` 是一个小巧的 `i3bar/swaybar/i3bar-river` 后端。

<center><img src="res/img/effect.png" style="zoom:50%;" /></center>

**Tips: 项目的代码并不多，十分建议在 AI 的辅助下阅读代码。**

可以通过以下命令将项目转化为单文件，方便与 AI 分享：
```sh
find src -type f -exec sh -c 'echo // file: {} && cat {}' \; >out.c
```

## 编译依赖

```sh
# Arch Linux
sudo pacman -S base-devel dbus libpulse libnl glib2

# Debian/Ubuntu
sudo apt install build-essential libdbus-1-dev libpulse-dev libnl-3-dev libnl-genl-3-dev libnl-route-3-dev libglib2.0-dev
```

pkg-config 检测：
- `dbus-1` — D-Bus 通信
- `libpulse` — PulseAudio 音频管理
- `libnl-3.0` `libnl-genl-3.0` `libnl-route-3.0` — netlink 内核接口通信
- `glib-2.0` — 工具函数

## 架构

主循环基于 `epoll_wait`。所有模块注册到全局数组 `modules[MOD_SIZE]` 中，epoll 事件通过 `data.u64` 索引到对应模块调用其 `update` 方法。

| 核心模块 | 功能 |
|----------|------|
| `main.c` | 创建 epoll 实例，根据 `module_id` 分派事件 |
| `timer.c` | 每秒触发一次，调用 `interval != 0` 的模块 |
| `stdin.c` | 解析 i3bar 点击事件 JSON，按 `name` 分派到 `alter` |

### 输出模块

| 模块 | 功能 | 数据来源 |
|------|------|----------|
| `battery.c` | 电量、百分比、预估时间、功耗 | sysfs |
| `backlight.c` | LCD 背光亮度 | sysfs |
| `pulse.c` | 音量控制、静音切换（输出/输入设备） | PulseAudio async API + eventfd |
| `network.c` | 网速、链路速率、无线信号 | **纯 netlink**（路由/链路/ethtool/nl80211） |
| `memory.c` | 内存使用率 | sysfs |
| `cpu.c` | CPU 使用率和功耗 | sysfs、msr |
| `temp.c` | 温度 | sysfs |
| `date.c` | 日期和时间 | time |

## 新增模块

### 最小模块

```c
void init_xxx(int epoll_fd) {
    INIT_BASE;
    // modules[module_id].update = update;
    // modules[module_id].interval = 1;
    // UPDATE_Q(module_id);
}
```

随后在 `src/include/modules.h` 中声明，在 `src/main.c` 的 `init()` 中调用。

### 功能扩展

参考 `main.h` 中的 `module_t`：

```c
typedef struct {
    char *output;      // 模块输出（由 update_json 管理）
    uint64_t interval; // 更新间隔（秒），0 表示不随定时器更新
    uint64_t state;    // 模块状态，alter 中切换
    void (*alter)(size_t, uint64_t);   // 鼠标点击回调
    void (*update)(size_t);            // 更新回调（timer 或 epoll 触发）
    void (*del)(size_t);               // 退出清理
    void *data;                        // 模块私有数据
} module_t;
```

- **定时更新**：设置 `interval = 1`，timer 每秒调用 `update`
- **epoll 监听**：`init` 中 `epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &(struct epoll_event){.events = EPOLLIN, .data.u64 = module_id})`
- **点击响应**：设置 `alter`，`stdin.c` 根据 `name` 字段首字母查模块索引后调用
- **数据优先走 netlink**：如果获取内核数据，优先使用 `libnl-*` 而非 procfs/sysfs
