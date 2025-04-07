# tinystatus

`tinystatus` 是一个小巧的 `i3bar/swaybar` 的后端。

<center><img src="res/img/effect.png" style="zoom:50%;" /></center>

为了尽可能的小，`tinystatus` 并不支持配置文件。
这应仅仅作为一个示例项目，用户应该在获取代码后修改各个模块中的宏，
以适应自己的设备（像 `dwm` 一样）。

## 特点

覆盖了大多数 [`i3status-rs`](https://github.com/greshake/i3status-rust) 的功能。
如：

- 实时响应输入（鼠标单击、滚轮滑动）
- 实时监控文件
- 实时监控 `dbus`
- 实时监控 `ALSA`

总的来说，由于项目采用 `epoll`，任何可以转换为 `fd` 的资源都可以被异步监听。

## 实现的模块

| 文件名        | 功能描述                                          |
| ------------- | ------------------------------------------------- |
| `battery.c`   | 电池电量百分比、预期使用/充满时间、电池电量、功耗 |
| `backlight.c` | 显示、调节 LCD 背光亮度                           |
| `volume.c`    | 显示、调节音量大小                                |
| `network.c`   | 显示网速和有线/无线网络的链路信息                 |
| `gpu.c`       | 显示 GPU 的使用率和显存占用                       |
| `cpu.c`       | 处理器使用率和功耗                                |
| `temp.c`      | 处理器封装温度                                    |
| `date.c`      | 日期和时间                                        |
