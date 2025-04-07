# tinystatus

`tinystatus` 是一个小巧的 `i3bar/swaybar` 的后端。

<center><img src="res/img/effect.png" style="zoom:50%;" /></center>

为了尽可能的小，`tinystatus` 并不支持配置文件。

## 注意

**这应仅仅作为一个示例项目**，用户应该在获取代码后修改各个模块中的宏，
以适应自己的设备（像 `dwm` 一样）。

**换句话说，假如代码一字不改，那么编译可能没问题，但运行是必然会出错的。**

**项目的代码并不多，十分建议在 ai 的辅助下阅读代码。**

可以通过如下命令可以将项目转化为一个单文件，喂给 ai

```sh
find src -type f -exec sh -c 'echo // file: {} && cat {}' \; >out.c
```

## 特点

覆盖了大多数 [`i3status-rs`](https://github.com/greshake/i3status-rust) 的核心功能。
如：

- 实时响应输入（鼠标单击、滚轮滑动）
- 实时监控文件
- 实时监控 `dbus`
- 实时监控 `ALSA`

总的来说，由于项目采用 `epoll`，任何可以转换为文件描述符的资源都可以被异步监听。

使用了面向对象的思想，所有模块在初始化时，
将自己的所有信息注册到 `module_t modules[]` 中。
随后有匹配 `module_id` 的事件时，由核心模块调用模块们对应的方法。

## 实现的模块

### 核心模块

- `main.c`：主文件，创建 `epoll` 实例，并监听所有 `init` 中注册的文件描述符。根据子模块注册 `epoll` 项时填入的 `module_id`，选择对应模块的 `update` 方法，来更新模块的 `output`。
- `timer.c`：计时器，每秒激活一次，调用 `interval` 不为 0 的模块的 `update` 方法。
- `stdin.c`：处理标准输入，如按键单击、鼠标滚轮事件。并根据输入 `json` 的 `name` 字段，调用对应模块的 `alter` 方法，改变模块状态。

### 输出模块

| 文件名        | 功能描述                              |
| ------------- | ------------------------------------- |
| `battery.c`   | 电量、百分比、预期放电/充满时间、功耗 |
| `backlight.c` | 显示、调节 LCD 背光亮度               |
| `volume.c`    | 显示、调节音量大小                    |
| `network.c`   | 显示网速和有线/无线网络的链路信息     |
| `gpu.c`       | 显示 GPU 的使用率和显存占用           |
| `cpu.c`       | 处理器使用率和功耗                    |
| `temp.c`      | 处理器封装温度                        |
| `date.c`      | 日期和时间                            |

## 新模块指引

### 最小的模块

一个最小的模块如下：

```c
void init_xxx (int epoll_fd) {
    (void) epoll_fd;
    INIT_BASE; // 可视为构造函数
}
```

随后在 `modules.h` 中声明模块的初始化函数。

最后在 `main.c` 中调用，如：

```c
init_xxx (epoll_fd);
```

### 更多功能

如果想为模块增加功能，请参考 `main.h` 中的 `module_t`，这里定义了模块的数据和方法：

```c
typedef struct {
    char *output;      // 模块的输出
    int *fds;          // 监听的 fd，程序退出时会释放这些 fd
    uint64_t interval; // 确定模块更新的时间间隔，0 表示不随时间更新
    uint64_t state;    // 模块的状态，配合 alter 方法，实现响应鼠标事件
    void (*alter) (size_t, uint64_t); // 改变模块状态
    void (*update) (size_t);          // 子模块的 update 方法
    void (*del) (size_t);             // 析构函数，清理模块内部数据
    union {                           // 模块内部数据，用于存储一些“静态变量”
        void *ptr;
        uint64_t num;
    } data;
} module_t;
```
