#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <module_base.h>
#include <tools.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <pulse/pulseaudio.h>

// 设备类型枚举
typedef enum { DEVICE_TYPE_OUTPUT = 0, DEVICE_TYPE_INPUT = 1 } device_type_t;

// 回调函数：获取输出设备信息（包括音量和静音状态）
void get_sink_info_callback(pa_context *c, const pa_sink_info *i, int eol, void *userdata);

// 回调函数：获取输入设备信息（包括音量和静音状态）
void get_source_info_callback(pa_context *c, const pa_source_info *i, int eol, void *userdata);

// 回调函数：获取服务器信息（用于获取默认输出/输入设备名称）
void get_server_info_callback(pa_context *c, const pa_server_info *i, void *userdata);

// 存储结构体，包含PulseAudio上下文和通信fd
struct pulse_storage {
    int event_fd;
    int epoll_fd;
    size_t module_id; // 用于重连时重新注册 epoll
    pthread_t thread_id;
    pa_threaded_mainloop *mainloop;
    pa_context *context;
    char *sink_name;       // 输出设备名
    char *source_name;     // 输入设备名
    uint32_t sink_index;   // 输出设备索引
    uint32_t source_index; // 输入设备索引
    pa_volume_t volume;
    int muted;
    device_type_t device_type; // 设备类型
    int reconnect;             // 标记需重连，由回调设置、update 处理
};

// 初始化PulseAudio连接（前向声明）
int init_pulse_audio(struct pulse_storage *storage);

// 回调函数：获取服务器信息（用于获取默认输出/输入设备名称）
void get_server_info_callback(pa_context *c, const pa_server_info *i, void *userdata) {
    if (!i) {
        fprintf(stderr, "Server info is NULL.\n");
        return;
    }

    struct pulse_storage *storage = (struct pulse_storage *)userdata;

    if (storage->device_type == DEVICE_TYPE_OUTPUT) {
        if (!i->default_sink_name) {
            fprintf(stderr, "Failed to get default sink name.\n");
            return;
        }

        if (storage->sink_name && strcmp(storage->sink_name, i->default_sink_name) == 0)
            return;

        free(storage->sink_name);
        storage->sink_name = strdup(i->default_sink_name);
        if (!storage->sink_name) {
            fprintf(stderr, "strdup failed\n");
            return;
        }

        // 更新默认输出设备索引
        pa_operation *op = pa_context_get_sink_info_by_name(c, storage->sink_name, get_sink_info_callback, userdata);
        if (op)
            pa_operation_unref(op);
    } else if (storage->device_type == DEVICE_TYPE_INPUT) {
        if (!i->default_source_name) {
            fprintf(stderr, "Failed to get default source name.\n");
            return;
        }

        if (storage->source_name && strcmp(storage->source_name, i->default_source_name) == 0)
            return;

        free(storage->source_name);
        storage->source_name = strdup(i->default_source_name);
        if (!storage->source_name) {
            fprintf(stderr, "strdup failed\n");
            return;
        }

        // 更新默认输入设备索引
        pa_operation *op =
            pa_context_get_source_info_by_name(c, storage->source_name, get_source_info_callback, userdata);
        if (op)
            pa_operation_unref(op);
    }
}

// 回调函数：获取输出设备信息（包括音量和静音状态）
void get_sink_info_callback(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
    (void)c;
    if (eol > 0)
        return;
    if (!i) {
        fprintf(stderr, "Sink info is NULL.\n");
        return;
    }

    struct pulse_storage *storage = (struct pulse_storage *)userdata;

    storage->sink_index = i->index;
    storage->volume = pa_cvolume_avg(&i->volume);
    storage->muted = i->mute;

    // 通知主进程音量已更新 - 写入eventfd
    uint64_t val = 1;
    write(storage->event_fd, &val, sizeof(val));
}

// 回调函数：获取输入设备信息（包括音量和静音状态）
void get_source_info_callback(pa_context *c, const pa_source_info *i, int eol, void *userdata) {
    (void)c;
    if (eol > 0)
        return;
    if (!i) {
        fprintf(stderr, "Source info is NULL.\n");
        return;
    }

    struct pulse_storage *storage = (struct pulse_storage *)userdata;

    storage->source_index = i->index;
    storage->volume = pa_cvolume_avg(&i->volume);
    storage->muted = i->mute;

    // 通知主进程音量已更新 - 写入eventfd
    uint64_t val = 1;
    write(storage->event_fd, &val, sizeof(val));
}

// 订阅回调函数：处理音量变化和默认设备切换
void subscribe_callback(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata) {
    pa_subscription_event_type_t facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    pa_subscription_event_type_t type = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

    struct pulse_storage *storage = (struct pulse_storage *)userdata;

    if (facility == PA_SUBSCRIPTION_EVENT_SERVER && type == PA_SUBSCRIPTION_EVENT_CHANGE) {
        // 默认输出/输入设备已更改
        pa_operation *op = pa_context_get_server_info(c, get_server_info_callback, userdata);
        if (op)
            pa_operation_unref(op);
        return;
    }

    if (facility == PA_SUBSCRIPTION_EVENT_SINK && type == PA_SUBSCRIPTION_EVENT_CHANGE) {
        if (storage->device_type == DEVICE_TYPE_OUTPUT &&
            (storage->sink_index == PA_INVALID_INDEX || idx == storage->sink_index)) {
            pa_operation *op = pa_context_get_sink_info_by_index(c, idx, get_sink_info_callback, userdata);
            if (op)
                pa_operation_unref(op);
        }
    }

    if (facility == PA_SUBSCRIPTION_EVENT_SOURCE && type == PA_SUBSCRIPTION_EVENT_CHANGE) {
        if (storage->device_type == DEVICE_TYPE_INPUT &&
            (storage->source_index == PA_INVALID_INDEX || idx == storage->source_index)) {
            pa_operation *op = pa_context_get_source_info_by_index(c, idx, get_source_info_callback, userdata);
            if (op)
                pa_operation_unref(op);
        }
    }
}

// 上下文状态回调函数
void context_state_callback(pa_context *c, void *userdata) {
    struct pulse_storage *storage = userdata;
    switch (pa_context_get_state(c)) {
    case PA_CONTEXT_READY: {
        pa_operation *op1 = pa_context_get_server_info(c, get_server_info_callback, userdata);
        if (!op1) {
            fprintf(stderr, "Failed to get server info.\n");
            return;
        }
        pa_operation_unref(op1);

        pa_context_set_subscribe_callback(c, subscribe_callback, userdata);

        // 根据设备类型订阅相应的事件
        pa_subscription_mask_t mask = PA_SUBSCRIPTION_MASK_SERVER;
        if (storage->device_type == DEVICE_TYPE_OUTPUT) {
            mask |= PA_SUBSCRIPTION_MASK_SINK;
        } else if (storage->device_type == DEVICE_TYPE_INPUT) {
            mask |= PA_SUBSCRIPTION_MASK_SOURCE;
        }

        pa_operation *op2 = pa_context_subscribe(c, mask, NULL, NULL);
        if (op2)
            pa_operation_unref(op2);
        break;
    }

    case PA_CONTEXT_FAILED:
        fprintf(stderr, "PulseAudio context failed, reconnecting...\n");
        storage->reconnect = 1;
        {
            uint64_t val = 1;
            write(storage->event_fd, &val, sizeof(val));
        }
        break;

    case PA_CONTEXT_TERMINATED:
        fprintf(stderr, "PulseAudio context terminated, reconnecting...\n");
        storage->reconnect = 1;
        {
            uint64_t val = 1;
            write(storage->event_fd, &val, sizeof(val));
        }
        break;

    default:
        break;
    }
}

// 初始化PulseAudio连接
int init_pulse_audio(struct pulse_storage *storage) {
    storage->mainloop = pa_threaded_mainloop_new();
    if (!storage->mainloop) {
        fprintf(stderr, "pa_threaded_mainloop_new() failed\n");
        return -1;
    }

    if (pa_threaded_mainloop_start(storage->mainloop) < 0) {
        fprintf(stderr, "pa_threaded_mainloop_start() failed\n");
        pa_threaded_mainloop_free(storage->mainloop);
        return -1;
    }

    pa_mainloop_api *api = pa_threaded_mainloop_get_api(storage->mainloop);
    storage->context = pa_context_new(api, "tinystatus-pulse");
    if (!storage->context) {
        fprintf(stderr, "pa_context_new() failed\n");
        goto cleanup;
    }

    pa_context_set_state_callback(storage->context, context_state_callback, storage);

    // 必须在 locked 状态下调用 connect（因为 mainloop 是 threaded）
    pa_threaded_mainloop_lock(storage->mainloop);
    if (pa_context_connect(storage->context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
        fprintf(stderr, "pa_context_connect() failed\n");
        pa_threaded_mainloop_unlock(storage->mainloop);
        goto cleanup;
    }
    pa_threaded_mainloop_unlock(storage->mainloop);

    return 0;

cleanup:
    if (storage->context) {
        pa_context_unref(storage->context);
        storage->context = NULL;
    }
    if (storage->mainloop) {
        pa_threaded_mainloop_stop(storage->mainloop);
        pa_threaded_mainloop_free(storage->mainloop);
        storage->mainloop = NULL;
    }
    return -1;
}

// 更新函数：读取eventfd并更新显示
static void update(size_t module_id) {
    struct pulse_storage *storage = (struct pulse_storage *)modules[module_id].data;

    uint64_t val;
    ssize_t s = read(storage->event_fd, &val, sizeof(val));
    if (s == -1 && errno != EAGAIN) {
        perror("read eventfd");
    }

    // 处理重连（pulse server 重启等）
    if (storage->reconnect) {
        storage->reconnect = 0;
        int old_fd = storage->event_fd;
        if (storage->mainloop) {
            pa_context_disconnect(storage->context);
            pa_threaded_mainloop_stop(storage->mainloop);
            pa_threaded_mainloop_free(storage->mainloop);
            storage->mainloop = NULL;
            storage->context = NULL;
        }
        init_pulse_audio(storage);
        if (storage->event_fd < 0) {
            fprintf(stderr, "PulseAudio reconnect failed\n");
            update_json(module_id, "N/A", CRITICAL);
            return;
        }
        if (old_fd >= 0)
            epoll_ctl(storage->epoll_fd, EPOLL_CTL_DEL, old_fd, NULL);
        struct epoll_event ev = {.events = EPOLLIN, .data.u64 = module_id};
        epoll_ctl(storage->epoll_fd, EPOLL_CTL_ADD, storage->event_fd, &ev);
    }

    // 格式化输出字符串
    char output_str[64];

    if (storage->device_type == DEVICE_TYPE_INPUT) {
        // 输入设备（麦克风）显示
        if (storage->muted) {
            snprintf(output_str, sizeof(output_str), "󰍭");
            update_json(module_id, output_str, WARNING);
        } else {
            snprintf(output_str, sizeof(output_str), "󰍬");
            update_json(module_id, output_str);
        }
    } else {
        // 输出设备（扬声器/耳机）显示
        float volume_percent = (float)storage->volume / PA_VOLUME_NORM * 100.0f;
        // 对显示的音量进行标准化，四舍五入到5的倍数
        int display_percent = ((int)(volume_percent + 2.5) / 5) * 5; // 四舍五入到5的倍数

        if (storage->muted) {
            snprintf(output_str, sizeof(output_str), "󰸈");
            update_json(module_id, output_str, WARNING);
        } else if (display_percent < 34) {
            snprintf(output_str, sizeof(output_str), "󰕿" SEP "%d%%", display_percent);
            update_json(module_id, output_str);
        } else if (display_percent < 67) {
            snprintf(output_str, sizeof(output_str), "󰖀" SEP "%d%%", display_percent);
            update_json(module_id, output_str);
        } else {
            snprintf(output_str, sizeof(output_str), "󰕾" SEP "%d%%", display_percent);
            update_json(module_id, output_str);
        }
    }
}

// 修改音量的函数（响应鼠标点击）- 输出和输入设备
static void alter(size_t module_id, uint64_t btn) {
    struct pulse_storage *storage = (struct pulse_storage *)modules[module_id].data;

    switch (btn) {
    case 2:
        system(storage->device_type == DEVICE_TYPE_INPUT ? "pwvucontrol -t 3 &" : "pwvucontrol &");
        break;
    case 3:
        system(
            storage->device_type == DEVICE_TYPE_INPUT ? "pactl set-source-mute @DEFAULT_SOURCE@ toggle"
                                                      : "pactl set-sink-mute @DEFAULT_SINK@ toggle"
        );
        break;
    case 4:
        system(
            storage->device_type == DEVICE_TYPE_INPUT ? "pactl set-source-volume @DEFAULT_SOURCE@ +5%"
                                                      : "pactl set-sink-volume @DEFAULT_SINK@ +5%"
        );
        break;
    case 5:
        system(
            storage->device_type == DEVICE_TYPE_INPUT ? "pactl set-source-volume @DEFAULT_SOURCE@ -5%"
                                                      : "pactl set-sink-volume @DEFAULT_SINK@ -5%"
        );
        break;
    }
}

// 删除函数：清理资源
static void del(size_t module_id) {
    struct pulse_storage *storage = (struct pulse_storage *)modules[module_id].data;

    // 断开PulseAudio连接
    if (storage->context) {
        pa_threaded_mainloop_lock(storage->mainloop);
        pa_context_disconnect(storage->context);
        pa_threaded_mainloop_unlock(storage->mainloop);
    }

    if (storage->mainloop) {
        pa_threaded_mainloop_stop(storage->mainloop);
        pa_threaded_mainloop_free(storage->mainloop);
    }

    // 关闭eventfd
    close(storage->event_fd);

    // 从epoll中移除
    epoll_ctl(storage->epoll_fd, EPOLL_CTL_DEL, storage->event_fd, NULL);

    // 释放内存
    free(storage->sink_name);
    free(storage->source_name);
    free(storage);
}

// 初始化pulse模块 - 输出设备
void init_pulse_output(int epoll_fd) {
    INIT_BASE;

    // 创建存储结构体
    struct pulse_storage *storage = malloc(sizeof(struct pulse_storage));
    if (!storage) {
        perror("malloc");
        modules_cnt--;
        return;
    }

    // 创建eventfd用于线程间通信
    storage->event_fd = eventfd(0, EFD_CLOEXEC);
    if (storage->event_fd == -1) {
        perror("eventfd");
        free(storage);
        modules_cnt--;
        return;
    }

    // 初始化存储结构体
    storage->epoll_fd = epoll_fd;
    storage->sink_name = NULL;
    storage->source_name = NULL;
    storage->sink_index = PA_INVALID_INDEX;
    storage->source_index = PA_INVALID_INDEX;
    storage->volume = 0;
    storage->muted = 0;
    storage->mainloop = NULL;
    storage->context = NULL;
    storage->device_type = DEVICE_TYPE_OUTPUT;

    // 将eventfd添加到epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.u64 = module_id;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, storage->event_fd, &ev) == -1) {
        perror("epoll_ctl");
        close(storage->event_fd);
        free(storage);
        modules_cnt--;
        return;
    }

    // 初始化PulseAudio连接
    if (init_pulse_audio(storage) < 0) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, storage->event_fd, NULL);
        close(storage->event_fd);
        free(storage);
        modules_cnt--;
        return;
    }

    // 设置模块回调函数
    modules[module_id].data = storage;
    storage->module_id = module_id;
    modules[module_id].update = update;
    modules[module_id].alter = alter;
    modules[module_id].del = del;

    // 立即更新一次
    UPDATE_Q(module_id);
}

// 初始化pulse模块 - 输入设备
void init_pulse_input(int epoll_fd) {
    INIT_BASE;

    // 创建存储结构体
    struct pulse_storage *storage = malloc(sizeof(struct pulse_storage));
    if (!storage) {
        perror("malloc");
        modules_cnt--;
        return;
    }

    // 创建eventfd用于线程间通信
    storage->event_fd = eventfd(0, EFD_CLOEXEC);
    if (storage->event_fd == -1) {
        perror("eventfd");
        free(storage);
        modules_cnt--;
        return;
    }

    // 初始化存储结构体
    storage->epoll_fd = epoll_fd;
    storage->sink_name = NULL;
    storage->source_name = NULL;
    storage->sink_index = PA_INVALID_INDEX;
    storage->source_index = PA_INVALID_INDEX;
    storage->volume = 0;
    storage->muted = 0;
    storage->mainloop = NULL;
    storage->context = NULL;
    storage->device_type = DEVICE_TYPE_INPUT;

    // 将eventfd添加到epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.u64 = module_id;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, storage->event_fd, &ev) == -1) {
        perror("epoll_ctl");
        close(storage->event_fd);
        free(storage);
        modules_cnt--;
        return;
    }

    // 初始化PulseAudio连接
    if (init_pulse_audio(storage) < 0) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, storage->event_fd, NULL);
        close(storage->event_fd);
        free(storage);
        modules_cnt--;
        return;
    }

    // 设置模块回调函数
    modules[module_id].data = storage;
    storage->module_id = module_id;
    modules[module_id].update = update;
    modules[module_id].alter = alter;
    modules[module_id].del = del;

    // 立即更新一次
    UPDATE_Q(module_id);
}

// 初始化pulse模块（输出和输入设备）
void init_pulse(int epoll_fd) {
    init_pulse_input(epoll_fd);
    init_pulse_output(epoll_fd);
}
