#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cJSON.h>
#include <module_base.h>
#include <tools.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <pulse/pulseaudio.h>

// 回调函数：获取输出设备信息（包括音量和静音状态）
void get_sink_info_callback(pa_context *c, const pa_sink_info *i, int eol, void *userdata);

// 回调函数：获取服务器信息（用于获取默认输出设备名称）
void get_server_info_callback(pa_context *c, const pa_server_info *i, void *userdata);

// 存储结构体，包含PulseAudio上下文和通信fd
struct pulse_storage {
    int event_fd;
    int epoll_fd;
    pthread_t thread_id;
    pa_threaded_mainloop *mainloop;
    pa_context *context;
    char *sink_name;
    uint32_t sink_index;
    pa_volume_t volume;
    int muted;
};

// 用于alter操作的结构体
struct pulse_alter_data {
    struct pulse_storage *storage;
    int operation_type; // 0: mute toggle, 1: volume up, 2: volume down
};

// 回调函数：获取服务器信息（用于获取默认输出设备名称）
void get_server_info_callback(pa_context *c, const pa_server_info *i, void *userdata) {
    if (!i || !i->default_sink_name) {
        fprintf(stderr, "Failed to get default sink name.\n");
        return;
    }

    struct pulse_storage *storage = (struct pulse_storage *)userdata;

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

// 订阅回调函数：处理音量变化和默认设备切换
void subscribe_callback(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata) {
    pa_subscription_event_type_t facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    pa_subscription_event_type_t type = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

    struct pulse_storage *storage = (struct pulse_storage *)userdata;

    if (facility == PA_SUBSCRIPTION_EVENT_SERVER && type == PA_SUBSCRIPTION_EVENT_CHANGE) {
        // 默认输出设备已更改
        pa_operation *op = pa_context_get_server_info(c, get_server_info_callback, userdata);
        if (op)
            pa_operation_unref(op);
        return;
    }

    if (facility == PA_SUBSCRIPTION_EVENT_SINK && type == PA_SUBSCRIPTION_EVENT_CHANGE) {
        if (storage->sink_index == PA_INVALID_INDEX || idx == storage->sink_index) {
            pa_operation *op = pa_context_get_sink_info_by_index(c, idx, get_sink_info_callback, userdata);
            if (op)
                pa_operation_unref(op);
        }
    }
}

// 上下文状态回调函数
void context_state_callback(pa_context *c, void *userdata) {
    switch (pa_context_get_state(c)) {
    case PA_CONTEXT_READY: {
        pa_operation *op1 = pa_context_get_server_info(c, get_server_info_callback, userdata);
        if (!op1) {
            fprintf(stderr, "Failed to get server info.\n");
            return;
        }
        pa_operation_unref(op1);

        pa_context_set_subscribe_callback(c, subscribe_callback, userdata);
        pa_operation *op2 =
            pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER, NULL, NULL);
        if (op2)
            pa_operation_unref(op2);
        break;
    }

    case PA_CONTEXT_FAILED:
        fprintf(stderr, "PulseAudio context failed.\n");
        break;

    case PA_CONTEXT_TERMINATED:
        fprintf(stderr, "PulseAudio context terminated.\n");
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

    // 读取eventfd以清空通知
    uint64_t val;
    ssize_t s = read(storage->event_fd, &val, sizeof(val));
    if (s != sizeof(val)) {
        perror("read eventfd");
        return;
    }

    // 格式化输出字符串
    char output_str[64];
    float volume_percent = (float)storage->volume / PA_VOLUME_NORM * 100.0f;
    // 对显示的音量进行标准化，四舍五入到5的倍数
    int display_percent = ((int)(volume_percent + 2.5) / 5) * 5; // 四舍五入到5的倍数

    if (storage->muted) {
        snprintf(output_str, sizeof(output_str), "󰸈");
        update_json(module_id, output_str, CRITICAL);
    } else if (display_percent < 34) {
        snprintf(output_str, sizeof(output_str), "󰕿\u2004%d%%", display_percent);
        update_json(module_id, output_str, IDLE);
    } else if (display_percent < 67) {
        snprintf(output_str, sizeof(output_str), "󰖀\u2004%d%%", display_percent);
        update_json(module_id, output_str, IDLE);
    } else {
        snprintf(output_str, sizeof(output_str), "󰕾\u2004%d%%", display_percent);
        update_json(module_id, output_str, IDLE);
    }
}

// 静音操作回调
void set_sink_mute_callback(pa_context *c, int success, void *userdata) {
    (void)c;
    struct pulse_alter_data *alter_data = (struct pulse_alter_data *)userdata;
    if (!success) {
        fprintf(stderr, "Failed to mute/unmute sink\n");
    }
    // 触发一次更新以反映新状态
    struct pulse_storage *storage = alter_data->storage;
    uint64_t val = 1;
    write(storage->event_fd, &val, sizeof(val));

    // 现在可以安全地释放alter_data了
    free(alter_data);
}

// 音量设置回调
void set_sink_volume_callback(pa_context *c, int success, void *userdata) {
    (void)c;
    struct pulse_alter_data *alter_data = (struct pulse_alter_data *)userdata;
    if (!success) {
        fprintf(stderr, "Failed to set sink volume\n");
    }
    // 触发一次更新以反映新状态
    struct pulse_storage *storage = alter_data->storage;
    uint64_t val = 1;
    write(storage->event_fd, &val, sizeof(val));

    // 现在可以安全地释放alter_data了
    free(alter_data);
}

// 获取当前音量信息的回调，用于修改操作
void get_current_sink_info_for_change(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
    if (eol > 0)
        return;
    if (!i)
        return;

    struct pulse_alter_data *alter_data = (struct pulse_alter_data *)userdata;
    struct pulse_storage *storage = alter_data->storage;

    switch (alter_data->operation_type) {
    case 0: // mute toggle
    {
        pa_context_set_sink_mute_by_name(c, storage->sink_name, !i->mute, set_sink_mute_callback, alter_data);
        break;
    }
    case 1: // volume up
    {
        pa_cvolume cvol = i->volume;
        // 将当前音量转换为百分比并四舍五入到5的倍数
        int current_percent = (int)(pa_cvolume_avg(&cvol) * 100.0 / PA_VOLUME_NORM);
        int rounded_current = ((current_percent + 2) / 5) * 5; // 四舍五入到5的倍数
        int new_percent = rounded_current + 5;                 // 增加5%

        // 限制最大音量不超过150%
        if (new_percent > 150) {
            new_percent = 150;
        }

        pa_volume_t new_volume = (pa_volume_t)(new_percent * PA_VOLUME_NORM / 100.0);
        pa_cvolume_set(&cvol, cvol.channels, new_volume);
        pa_context_set_sink_volume_by_name(c, storage->sink_name, &cvol, set_sink_volume_callback, alter_data);
        break;
    }
    case 2: // volume down
    {
        pa_cvolume cvol = i->volume;
        // 将当前音量转换为百分比并四舍五入到5的倍数
        int current_percent = (int)(pa_cvolume_avg(&cvol) * 100.0 / PA_VOLUME_NORM);
        int rounded_current = ((current_percent + 2) / 5) * 5; // 四舍五入到5的倍数
        int new_percent = rounded_current - 5;                 // 减少5%

        // 音量不能低于0
        if (new_percent < 0) {
            new_percent = 0;
        }

        pa_volume_t new_volume = (pa_volume_t)(new_percent * PA_VOLUME_NORM / 100.0);
        pa_cvolume_set(&cvol, cvol.channels, new_volume);
        pa_context_set_sink_volume_by_name(c, storage->sink_name, &cvol, set_sink_volume_callback, alter_data);
        break;
    }
    }
}

// 修改音量的函数（响应鼠标点击）
static void alter(size_t module_id, uint64_t btn) {
    struct pulse_storage *storage = (struct pulse_storage *)modules[module_id].data;

    switch (btn) {
    case 2: // middle button - 打开音量控制
        system("pwvucontrol &");
        break;
    case 3: // right button - 切换静音
    case 4: // wheel up - 增加音量
    case 5: // wheel down - 减少音量
        if (storage->context && storage->sink_name) {
            // 创建alter数据结构
            struct pulse_alter_data *alter_data = malloc(sizeof(struct pulse_alter_data));
            if (!alter_data) {
                perror("malloc");
                return;
            }
            alter_data->storage = storage;

            // 根据按钮设置操作类型
            alter_data->operation_type = btn - 3;

            pa_operation *op = pa_context_get_sink_info_by_name(
                storage->context, storage->sink_name, get_current_sink_info_for_change, alter_data
            );
            if (op)
                pa_operation_unref(op);
        }
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
    free(storage);
}

// 初始化pulse模块
void init_pulse(int epoll_fd) {
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
    storage->sink_index = PA_INVALID_INDEX;
    storage->volume = 0;
    storage->muted = 0;
    storage->mainloop = NULL;
    storage->context = NULL;

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
    modules[module_id].update = update;
    modules[module_id].alter = alter;
    modules[module_id].del = del;

    // 立即更新一次
    UPDATE_Q(module_id);
}
