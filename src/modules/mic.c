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

static int64_t get_volume (snd_mixer_t *handle) {
    snd_mixer_handle_events (handle); // 让 ALSA 处理事件，刷新 ALSA

    // 手动检查音量变化
    snd_mixer_selem_id_t *sid;

    // 设置混音器元素ID - 查找Capture通道
    snd_mixer_selem_id_alloca (&sid);
    snd_mixer_selem_id_set_index (sid, 0);
    snd_mixer_selem_id_set_name (sid, "Capture");
    snd_mixer_elem_t *elem = snd_mixer_find_selem (handle, sid);
    if (!elem) {
        fprintf (stderr, "未找到输入设备控制\n");
        return -2;
    }

    if (snd_mixer_selem_is_active (elem)) {
        int unmuted = 1;
        if (snd_mixer_selem_has_capture_switch (elem)) {
            snd_mixer_selem_get_capture_switch (
                elem, SND_MIXER_SCHN_FRONT_LEFT, &unmuted
            );
        }

        if (unmuted) {
            int64_t min, max, volume;
            snd_mixer_selem_get_capture_volume_range (elem, &min, &max);
            snd_mixer_selem_get_capture_volume (
                elem, SND_MIXER_SCHN_FRONT_LEFT, &volume
            );
            volume = (volume - min) * 100 / (max - min);
            return (volume + 1) / 5;
        }
        return -1; // 静音
    }
    return -2; // 设备未激活
}

struct storage {
    snd_mixer_t *handle;
    int epoll_fd;
    int pfd_fd;
};

static void reload_microphone (int epoll_fd, size_t module_id);

static void update (size_t module_id) {
    modules[module_id].interval = 0;
    snd_mixer_t *handle =
        ((struct storage *) modules[module_id].data.ptr)->handle;
    int64_t volume = get_volume (handle);
    char output_str[] = "󰍭\u2004INF\0";
    if (volume == -2) {
        snprintf (output_str, sizeof (output_str), "󰍱");
        int epoll_fd =
            ((struct storage *) modules[module_id].data.ptr)->epoll_fd;
        modules[module_id].del (module_id);
        reload_microphone (epoll_fd, module_id);
        modules[module_id].interval = 1; // 随时间刷新一次
    } else if (volume == -1) {
        snprintf (output_str, sizeof (output_str), "󰍭");
    } else {
        volume *= 5;
        if (volume == 0) {
            snprintf (output_str, sizeof (output_str), "󰍮");
        } else if (volume < 33) {
            snprintf (
                output_str, sizeof (output_str), "󰍮\u2004%*lu%%",
                volume < 10 ? 1 : 2, volume
            );
        } else if (volume < 67) {
            snprintf (
                output_str, sizeof (output_str), "󰢳\u2004%2lu%%", volume
            );
        } else if (volume < 100) {
            snprintf (
                output_str, sizeof (output_str), "󰍬\u2004%2lu%%", volume
            );
        } else if (volume < 200) {
            snprintf (
                output_str, sizeof (output_str), "󰢴\u2004%3lu%%", volume
            );
        }
    }

    update_json (module_id, output_str, IDLE);
}

static void alter (size_t module_id, uint64_t btn) {
    (void) module_id;
    switch (btn) {
    case 2: // middle button
        system ("pavucontrol -t 4 &");
        break;
    case 3: // right button
        system ("~/.bin/wm/volume m t &");
        break;
    case 4: // up
        system ("~/.bin/wm/volume m i &");
        break;
    case 5: // down
        system ("~/.bin/wm/volume m d &");
        break;
    }
}

static void del (size_t module_id) {
    struct storage *storage = modules[module_id].data.ptr;
    epoll_ctl (storage->epoll_fd, EPOLL_CTL_DEL, storage->pfd_fd, NULL);
    if (storage->handle)
        snd_mixer_close (storage->handle);
    free (storage);
}

static void reload_microphone (int epoll_fd, size_t module_id) {
    snd_mixer_t *handle = NULL;
    int err;

    // 打开默认混音器
    if ((err = snd_mixer_open (&handle, 0)) < 0) {
        fprintf (stderr, "无法打开混音器: %s\n", snd_strerror (err));
        exit (EXIT_FAILURE);
    }

    // 加载混音器
    if ((err = snd_mixer_attach (handle, "default")) < 0 ||
        (err = snd_mixer_selem_register (handle, NULL, NULL)) < 0 ||
        (err = snd_mixer_load (handle)) < 0) {
        fprintf (stderr, "混音器初始化失败: %s\n", snd_strerror (err));
        exit (EXIT_FAILURE);
    }

    // 获取 ALSA 混音器的文件描述符
    struct pollfd pfd;
    if (snd_mixer_poll_descriptors (handle, &pfd, 1) != 1) {
        fprintf (stderr, "无法获取混音器文件描述符\n");
        exit (EXIT_FAILURE);
    }

    // 将文件描述符添加到 epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.u64 = module_id;
    if (epoll_ctl (epoll_fd, EPOLL_CTL_ADD, pfd.fd, &ev) == -1) {
        perror ("epoll_ctl failed");
        exit (EXIT_FAILURE);
    }

    modules[module_id].update = update;
    modules[module_id].alter = alter;
    modules[module_id].data.ptr = malloc (sizeof (struct storage));
    ((struct storage *) modules[module_id].data.ptr)->handle =
        handle; // 保存混音器句柄
    ((struct storage *) modules[module_id].data.ptr)->epoll_fd = epoll_fd;
    ((struct storage *) modules[module_id].data.ptr)->pfd_fd = pfd.fd;
    modules[module_id].del = del;
}

void init_microphone (int epoll_fd) {
    INIT_BASE;
    reload_microphone (epoll_fd, module_id);
    UPDATE_Q (module_id);
}
