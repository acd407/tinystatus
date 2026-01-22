#include <alsa/asoundlib.h>
#include <cJSON.h>
#include <module_base.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <tools.h>
#include <unistd.h>

#define ADC "STO1 ADC"

static int64_t get_volume(snd_mixer_t *handle) {
    snd_mixer_handle_events(handle); // 让 ALSA 处理事件，刷新 ALSA

    // 手动检查音量变化
    snd_mixer_selem_id_t *sid;

    // 设置混音器元素ID - 查找Capture通道
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, "Capture");
    snd_mixer_elem_t *elem = snd_mixer_find_selem(handle, sid);

    if (!elem) {
        snd_mixer_selem_id_set_name(sid, ADC);
        elem = snd_mixer_find_selem(handle, sid);
    }
    if (!elem) {
        fprintf(stderr, "Unable to find Capture or ADC control\n");
        return -2;
    }

    if (snd_mixer_selem_is_active(elem)) {
        int unmuted = 1;
        if (snd_mixer_selem_has_capture_switch(elem)) {
            snd_mixer_selem_get_capture_switch(elem, SND_MIXER_SCHN_FRONT_LEFT, &unmuted);
        }

        if (unmuted) {
            int64_t min, max, volume;
            snd_mixer_selem_get_capture_volume_range(elem, &min, &max);
            snd_mixer_selem_get_capture_volume(elem, SND_MIXER_SCHN_FRONT_LEFT, &volume);
            volume = (volume - min) * 100 / (max - min);
            return (volume + 1) / 5;
        }
        return -1; // 静音
    }
    return -2; // 设备未激活
}

// 设置音量函数
static void set_volume(snd_mixer_t *handle, int volume) {
    snd_mixer_selem_id_t *sid;
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);

    // 查找Capture通道
    snd_mixer_selem_id_set_name(sid, "Capture");
    snd_mixer_elem_t *elem = snd_mixer_find_selem(handle, sid);

    if (!elem) {
        // 如果找不到Capture，尝试ADC
        snd_mixer_selem_id_set_name(sid, ADC);
        elem = snd_mixer_find_selem(handle, sid);
    }

    if (elem && snd_mixer_selem_is_active(elem)) {
        int64_t min, max;
        snd_mixer_selem_get_capture_volume_range(elem, &min, &max);

        // 将0-20范围的音量转换为0-100，再转换为ALSA范围
        int alsa_volume = min + (volume * 5 * (max - min)) / 100;

        // 设置左右声道音量
        snd_mixer_selem_set_capture_volume_all(elem, alsa_volume);
    }
}

// 切换静音函数
static void toggle_mute(snd_mixer_t *handle) {
    snd_mixer_selem_id_t *sid;
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);

    // 查找Capture通道
    snd_mixer_selem_id_set_name(sid, "Capture");
    snd_mixer_elem_t *elem = snd_mixer_find_selem(handle, sid);
    if (!elem) {
        // 如果找不到Capture，尝试ADC
        snd_mixer_selem_id_set_name(sid, ADC);
        elem = snd_mixer_find_selem(handle, sid);
    }
    if (!elem) {
        fprintf(stderr, "Unable to find Capture or ADC control\n");
        return;
    }

    if (snd_mixer_selem_is_active(elem) && snd_mixer_selem_has_capture_switch(elem)) {
        int unmuted;
        snd_mixer_selem_get_capture_switch(elem, SND_MIXER_SCHN_FRONT_LEFT, &unmuted);
        snd_mixer_selem_set_capture_switch_all(elem, !unmuted);
    }
}

struct storage {
    snd_mixer_t *handle;
    int epoll_fd;
    int pfd_fd;
};

static void reload_microphone(int epoll_fd, size_t module_id);

static void update(size_t module_id) {
    modules[module_id].interval = 0;
    snd_mixer_t *handle = ((struct storage *)modules[module_id].data)->handle;
    int64_t volume = get_volume(handle);
    char output_str[] = "󰍭\u2004INF\0";
    if (volume == -2) {
        snprintf(output_str, sizeof(output_str), "󰍱");
        int epoll_fd = ((struct storage *)modules[module_id].data)->epoll_fd;
        modules[module_id].del(module_id);
        reload_microphone(epoll_fd, module_id);
        modules[module_id].interval = 1; // 随时间刷新一次
    } else if (volume == -1) {
        snprintf(output_str, sizeof(output_str), "󰍭");
    } else {
        volume *= 5;
        if (volume == 0) {
            snprintf(output_str, sizeof(output_str), "󰍮");
        } else if (volume < 33) {
            snprintf(output_str, sizeof(output_str), "󰍮\u2004%*lu%%", volume < 10 ? 1 : 2, volume);
        } else if (volume < 67) {
            snprintf(output_str, sizeof(output_str), "󰢳\u2004%2lu%%", volume);
        } else if (volume < 100) {
            snprintf(output_str, sizeof(output_str), "󰍬\u2004%2lu%%", volume);
        } else if (volume < 200) {
            snprintf(output_str, sizeof(output_str), "󰢴\u2004%3lu%%", volume);
        }
    }

    update_json(module_id, output_str, IDLE);
}

static void alter(size_t module_id, uint64_t btn) {
    snd_mixer_t *handle = ((struct storage *)modules[module_id].data)->handle;

    switch (btn) {
    case 2: // middle button
        system("pavucontrol -t 4 &");
        break;
    case 3: // right button
        toggle_mute(handle);
        break;
    case 4: // up
    {
        int64_t current_volume = get_volume(handle);
        if (current_volume >= 0 && current_volume < 20) {
            set_volume(handle, current_volume + 1); // 增加1（相当于5%）
        }
    } break;
    case 5: // down
    {
        int64_t current_volume = get_volume(handle);
        if (current_volume > 0 && current_volume <= 20) {
            set_volume(handle, current_volume - 1); // 减少1（相当于5%）
        }
    } break;
    }
}

static void del(size_t module_id) {
    struct storage *storage = modules[module_id].data;
    epoll_ctl(storage->epoll_fd, EPOLL_CTL_DEL, storage->pfd_fd, NULL);
    if (storage->handle)
        snd_mixer_close(storage->handle);
    free(storage);
}

static void reload_microphone(int epoll_fd, size_t module_id) {
    snd_mixer_t *handle = NULL;
    int err;

    // 打开默认混音器
    if ((err = snd_mixer_open(&handle, 0)) < 0) {
        fprintf(stderr, "无法打开混音器: %s\n", snd_strerror(err));
        exit(EXIT_FAILURE);
    }

    // 加载混音器
    if ((err = snd_mixer_attach(handle, "default")) < 0 || (err = snd_mixer_selem_register(handle, NULL, NULL)) < 0 ||
        (err = snd_mixer_load(handle)) < 0) {
        fprintf(stderr, "混音器初始化失败: %s\n", snd_strerror(err));
        exit(EXIT_FAILURE);
    }

    // 获取 ALSA 混音器的文件描述符
    struct pollfd pfd;
    if (snd_mixer_poll_descriptors(handle, &pfd, 1) != 1) {
        fprintf(stderr, "无法获取混音器文件描述符\n");
        exit(EXIT_FAILURE);
    }

    // 将文件描述符添加到 epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.u64 = module_id;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, pfd.fd, &ev) == -1) {
        perror("epoll_ctl failed");
        exit(EXIT_FAILURE);
    }

    modules[module_id].update = update;
    modules[module_id].alter = alter;
    modules[module_id].data = malloc(sizeof(struct storage));
    ((struct storage *)modules[module_id].data)->handle = handle; // 保存混音器句柄
    ((struct storage *)modules[module_id].data)->epoll_fd = epoll_fd;
    ((struct storage *)modules[module_id].data)->pfd_fd = pfd.fd;
    modules[module_id].del = del;
}

void init_microphone(int epoll_fd) {
    INIT_BASE;
    reload_microphone(epoll_fd, module_id);
    UPDATE_Q(module_id);
}