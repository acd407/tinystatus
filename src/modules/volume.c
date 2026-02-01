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

#define DAC "DAC1"
#define DACL "DAC1 MIXL DAC1"
#define DACR "DAC1 MIXR DAC1"

static int64_t get_volume(snd_mixer_t *handle) {
    snd_mixer_handle_events(handle); // 让 ALSA 处理事件，刷新 ALSA

    // 手动检查音量变化
    snd_mixer_selem_id_t *sid;

    // 设置混音器元素ID - 查找Master通道
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, "Master");
    snd_mixer_elem_t *elem = snd_mixer_find_selem(handle, sid);

    if (!elem) {
        snd_mixer_selem_id_set_name(sid, DAC);
        elem = snd_mixer_find_selem(handle, sid);
    }
    if (!elem) {
        fprintf(stderr, "Unable to find Master or DAC control\n");
    } else if (snd_mixer_selem_is_active(elem)) {
        int unmuted = true; // 默认代表未静音

        // 检查是否支持静音切换
        if (snd_mixer_selem_has_playback_switch(elem)) {
            snd_mixer_selem_get_playback_switch(elem, SND_MIXER_SCHN_FRONT_LEFT, &unmuted);
        } else {
            // 如果不支持静音切换，检查DAC1的混音器控制器
            snd_mixer_selem_id_set_name(sid, DACL);
            snd_mixer_elem_t *mix_elem = snd_mixer_find_selem(handle, sid);

            if (mix_elem && snd_mixer_selem_is_active(mix_elem) && snd_mixer_selem_has_playback_switch(mix_elem)) {
                snd_mixer_selem_get_playback_switch(mix_elem, SND_MIXER_SCHN_FRONT_LEFT, &unmuted);
            }
        }

        if (unmuted) {
            int64_t min, max;
            // 获取音量范围
            snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
            // 这获取到的并不是最终需要的 0 - 100 的数值
            int64_t volume;
            snd_mixer_selem_get_playback_volume(elem, SND_MIXER_SCHN_FRONT_LEFT, &volume);
            volume = (volume - min) * (uint64_t)100 / (max - min);
            return (volume + 1) / 5;
        } else {
            return -1;
        }
    }
    return -2;
}

struct storage {
    snd_mixer_t *handle;
    int epoll_fd;
    int pfd_fd;
};

static void reload_volume(int epoll_fd, size_t module_id);

static void update(size_t module_id) {
    modules[module_id].interval = 0;
    snd_mixer_t *handle = ((struct storage *)modules[module_id].data)->handle;
    int64_t volume = get_volume(handle);
    char output_str[] = "󰕾\u2004INF\0";
    if (volume == -2) {
        snprintf(output_str, sizeof(output_str), "󰝟");
        int epoll_fd = ((struct storage *)modules[module_id].data)->epoll_fd;
        modules[module_id].del(module_id);
        reload_volume(epoll_fd, module_id);
        modules[module_id].interval = 1; // 随时间刷新一次
    } else if (volume == -1) {
        snprintf(output_str, sizeof(output_str), "󰸈");
    } else {
        volume *= 5;
        if (volume == 0) {
            snprintf(output_str, sizeof(output_str), "󰕿");
        } else if (volume < 34) {
            snprintf(output_str, sizeof(output_str), "󰕿\u2004%*lu%%", volume < 10 ? 1 : 2, volume);
        } else if (volume < 67) {
            snprintf(output_str, sizeof(output_str), "󰖀\u2004%2lu%%", volume);
        } else if (volume < 100) {
            snprintf(output_str, sizeof(output_str), "󰕾\u2004%2lu%%", volume);
        } else if (volume < 200) {
            snprintf(output_str, sizeof(output_str), "󰝝\u2004%3lu%%", volume);
        }
    }

    update_json(module_id, output_str, IDLE);
}

static void set_volume(snd_mixer_t *handle, int64_t volume) {
    snd_mixer_selem_id_t *sid;

    // 设置混音器元素ID - 查找Master通道
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, "Master");
    snd_mixer_elem_t *elem = snd_mixer_find_selem(handle, sid);
    if (!elem) {
        snd_mixer_selem_id_set_name(sid, DAC);
        elem = snd_mixer_find_selem(handle, sid);
    }
    if (!elem) {
        fprintf(stderr, "Unable to find Master or DAC control\n");
        return;
    }

    if (snd_mixer_selem_is_active(elem)) {
        int64_t min, max;
        // 获取音量范围
        snd_mixer_selem_get_playback_volume_range(elem, &min, &max);

        // 将0-20范围的音量值转换为ALSA音量范围
        // get_volume函数中的转换：(volume - min) * 100 / (max - min) -> (volume + 1) / 5
        // 反向转换：volume * 5 - 1 -> (volume - min) * 100 / (max - min)
        int64_t volume_percent = volume * 5; // 转换为0-100范围
        if (volume_percent > 100)
            volume_percent = 100;
        int64_t alsa_volume = min + volume_percent * (max - min) / 100;

        // 设置音量
        snd_mixer_selem_set_playback_volume_all(elem, alsa_volume);
    }
}

static void toggle_mute(snd_mixer_t *handle) {
    snd_mixer_selem_id_t *sid;

    // 设置混音器元素ID - 查找Master通道
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, "Master");
    snd_mixer_elem_t *elem = snd_mixer_find_selem(handle, sid);
    if (!elem) {
        snd_mixer_selem_id_set_name(sid, DAC);
        elem = snd_mixer_find_selem(handle, sid);
    }
    if (!elem) {
        fprintf(stderr, "Unable to find Master or DAC control\n");
        return;
    }

    if (snd_mixer_selem_is_active(elem)) {
        // 检查是否支持静音切换
        if (snd_mixer_selem_has_playback_switch(elem)) {
            int unmuted;
            snd_mixer_selem_get_playback_switch(elem, SND_MIXER_SCHN_FRONT_LEFT, &unmuted);
            // 切换静音状态
            snd_mixer_selem_set_playback_switch_all(elem, !unmuted);
        } else {
            // 尝试使用DAC1 MIXL DAC1控制器
            snd_mixer_selem_id_set_name(sid, DACL);
            snd_mixer_elem_t *mix_elem_l = snd_mixer_find_selem(handle, sid);
            snd_mixer_selem_id_set_name(sid, DACR);
            snd_mixer_elem_t *mix_elem_r = snd_mixer_find_selem(handle, sid);
            if (mix_elem_l && snd_mixer_selem_is_active(mix_elem_l) &&
                snd_mixer_selem_has_playback_switch(mix_elem_l) && mix_elem_r &&
                snd_mixer_selem_is_active(mix_elem_r) && snd_mixer_selem_has_playback_switch(mix_elem_r)) {
                int unmuted;
                snd_mixer_selem_get_playback_switch(mix_elem_l, SND_MIXER_SCHN_FRONT_LEFT, &unmuted);
                // 切换静音状态
                snd_mixer_selem_set_playback_switch_all(mix_elem_l, !unmuted);
                snd_mixer_selem_set_playback_switch_all(mix_elem_r, !unmuted);
            }
            fprintf(stderr, "DAC1: Muted using MIXL/MIXR controls\n");
        }
    }
}

static void alter(size_t module_id, uint64_t btn) {
    snd_mixer_t *handle = ((struct storage *)modules[module_id].data)->handle;

    switch (btn) {
    case 2: // middle button
        system("pwvucontrol -t 4 &");
        break;
    case 3: // right button - 切换静音
        toggle_mute(handle);
        break;
    case 4: // up - 增加音量
    {
        int64_t current_volume = get_volume(handle);
        if (current_volume >= 0) {
            current_volume += 1; // 增加1（相当于5%）
            if (current_volume > 20)
                current_volume = 20;
            set_volume(handle, current_volume);
        }
    } break;
    case 5: // down - 减少音量
    {
        int64_t current_volume = get_volume(handle);
        if (current_volume >= 0) {
            current_volume -= 1; // 减少1（相当于5%）
            if (current_volume < 0)
                current_volume = 0;
            set_volume(handle, current_volume);
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

static void reload_volume(int epoll_fd, size_t module_id) {
    snd_mixer_t *handle = NULL;
    int err;

    // 打开默认混音器
    if ((err = snd_mixer_open(&handle, 0)) < 0) {
        fprintf(stderr, "无法打开混音器: %s\n", snd_strerror(err));
        return;
    }

    // 加载混音器
    if ((err = snd_mixer_attach(handle, "default")) < 0 || (err = snd_mixer_selem_register(handle, NULL, NULL)) < 0 ||
        (err = snd_mixer_load(handle)) < 0) {
        fprintf(stderr, "混音器初始化失败: %s\n", snd_strerror(err));
        snd_mixer_close(handle);
        return;
    }

    // 获取 ALSA 混音器的文件描述符
    struct pollfd pfd;
    if (snd_mixer_poll_descriptors(handle, &pfd, 1) != 1) {
        fprintf(stderr, "无法获取混音器文件描述符\n");
        snd_mixer_close(handle);
        return;
    }

    // 将文件描述符添加到 epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.u64 = module_id;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, pfd.fd, &ev) == -1) {
        perror("epoll_ctl failed");
        snd_mixer_close(handle);
        return;
    }

    modules[module_id].update = update;
    modules[module_id].alter = alter;
    modules[module_id].data = malloc(sizeof(struct storage));
    if (!modules[module_id].data) {
        perror("malloc");
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, pfd.fd, NULL);
        snd_mixer_close(handle);
        return;
    }
    ((struct storage *)modules[module_id].data)->handle = handle; // 保存混音器句柄
    ((struct storage *)modules[module_id].data)->epoll_fd = epoll_fd;
    ((struct storage *)modules[module_id].data)->pfd_fd = pfd.fd;
    modules[module_id].del = del;
}

void init_volume(int epoll_fd) {
    INIT_BASE;

    // 保存原始的modules_cnt，以便在失败时恢复
    size_t original_modules_cnt = modules_cnt - 1;

    // 尝试初始化音量模块
    reload_volume(epoll_fd, module_id);

    // 检查初始化是否成功
    if (!modules[module_id].data) {
        // 初始化失败，恢复modules_cnt
        modules_cnt = original_modules_cnt;
        return;
    }

    UPDATE_Q(module_id);
}
