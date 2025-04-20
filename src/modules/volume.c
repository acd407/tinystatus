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

int64_t get_volume (snd_mixer_t *handle) {
    snd_mixer_handle_events (handle); // 让 ALSA 处理事件，刷新 ALSA

    // 手动检查音量变化
    snd_mixer_selem_id_t *sid;

    // 设置混音器元素ID - 查找Master通道
    snd_mixer_selem_id_alloca (&sid);
    snd_mixer_selem_id_set_index (sid, 0);
    snd_mixer_selem_id_set_name (sid, "Master");
    snd_mixer_elem_t *elem = snd_mixer_find_selem (handle, sid);
    if (!elem) {
        fprintf (stderr, "Unable to find Master control\n");
        snd_mixer_close (handle);
    } else if (snd_mixer_selem_is_active (elem)) {
        int unmuted = true; // 默认代表未静音
        if (snd_mixer_selem_has_playback_switch (elem)) {
            snd_mixer_selem_get_playback_switch (
                elem, SND_MIXER_SCHN_FRONT_LEFT, &unmuted
            );
        }
        if (unmuted) {
            int64_t min, max;
            // 获取音量范围
            snd_mixer_selem_get_playback_volume_range (elem, &min, &max);
            // 这获取到的并不是最终需要的 0 - 100 的数值
            int64_t volume;
            snd_mixer_selem_get_playback_volume (
                elem, SND_MIXER_SCHN_FRONT_LEFT, &volume
            );
            volume = (volume - min) * (uint64_t) 100 / (max - min);
            return (volume + 1) / 5;
        } else {
            return -1;
        }
    }
    return -2;
}

static void update (size_t module_id) {
    int64_t volume = get_volume (modules[module_id].data.ptr);
    char output_str[] = "󰕾\u2004INF\0";
    if (volume == -2) {
        snprintf (output_str, sizeof (output_str), "󰸈");
        modules[module_id].interval = 1;
    } else {
        modules[module_id].interval = 0;
        if (volume == -1) {
            snprintf (output_str, sizeof (output_str), "󰸈");
        } else {
            volume *= 5;
            if (volume == 0) {
                snprintf (output_str, sizeof (output_str), "󰕿");
            } else if (volume < 34) {
                snprintf (
                    output_str, sizeof (output_str), "󰕿\u2004%*lu%%",
                    volume < 10 ? 1 : 2, volume
                );
            } else if (volume < 67) {
                snprintf (
                    output_str, sizeof (output_str), "󰖀\u2004%2lu%%", volume
                );
            } else if (volume < 100) {
                snprintf (
                    output_str, sizeof (output_str), "󰕾\u2004%2lu%%", volume
                );
            } else if (volume < 200) {
                snprintf (
                    output_str, sizeof (output_str), "󰝝\u2004%3lu%%", volume
                );
            }
        }
    }
    update_json (module_id, output_str, IDLE);
}

static void alter (size_t module_id, uint64_t btn) {
    (void) module_id;
    switch (btn) {
    case 2: // middle button
        system ("pavucontrol -t 3 &");
        break;
    case 3: // right button
        system ("~/.bin/wm/volume t &");
        break;
    case 4: // up
        system ("~/.bin/wm/volume i &");
        break;
    case 5: // down
        system ("~/.bin/wm/volume d &");
        break;
    }
}

void init_volume (int epoll_fd) {
    INIT_BASE;
    snd_mixer_t *handle;
    int err;

    // 打开默认混音器
    if ((err = snd_mixer_open (&handle, 0)) < 0) {
        fprintf (stderr, "无法打开混音器: %s\n", snd_strerror (err));
        modules_cnt--;
        return;
    }

    // 加载混音器
    if ((err = snd_mixer_attach (handle, "default")) < 0 ||
        (err = snd_mixer_selem_register (handle, NULL, NULL)) < 0 ||
        (err = snd_mixer_load (handle)) < 0) {
        fprintf (stderr, "混音器初始化失败: %s\n", snd_strerror (err));
        snd_mixer_close (handle);
        modules_cnt--;
        return;
    }

    // 获取 ALSA 混音器的文件描述符
    struct pollfd pfd;
    if (snd_mixer_poll_descriptors (handle, &pfd, 1) != 1) {
        fprintf (stderr, "无法获取混音器文件描述符\n");
        snd_mixer_close (handle);
        modules_cnt--;
        return;
    }

    // 将文件描述符添加到 epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.u64 = module_id;
    if (epoll_ctl (epoll_fd, EPOLL_CTL_ADD, pfd.fd, &ev) == -1) {
        perror ("epoll_ctl failed");
        snd_mixer_close (handle);
        modules_cnt--;
        return;
    }

    modules[module_id].update = update;
    modules[module_id].alter = alter;
    // 保存混音器句柄
    modules[module_id].data.ptr = handle;
    modules[module_id].interval = 1;

    UPDATE_Q (module_id);
}
