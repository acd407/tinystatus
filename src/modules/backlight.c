#include <cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <module_base.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/timerfd.h>
#include <tools.h>
#include <unistd.h>

#define BRIGHTNESS "/sys/class/backlight/intel_backlight/brightness"
#define MAX_BRIGHTNESS "/sys/class/backlight/intel_backlight/max_brightness"
#define BUF_LEN 4096

struct backlight_data {
    int inotify_fd;
};

static void update(size_t module_id) {
    char buffer[BUF_LEN];
    struct backlight_data *data = (struct backlight_data *)modules[module_id].data;
    ssize_t len = read(data->inotify_fd, buffer, BUF_LEN);
    if (len == -1 && errno != EAGAIN) {
        perror("read");
        exit(EXIT_FAILURE);
    }

    // 处理事件
    uint64_t brightness_percent = read_uint64_file(BRIGHTNESS) * 100 / read_uint64_file(MAX_BRIGHTNESS);
    assert(brightness_percent <= 100);
    brightness_percent = (brightness_percent + 1) / 5 * 5;

    const char *icons[] = {
        "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    };
    size_t idx = ARRAY_SIZE(icons) * brightness_percent / 101;

    char output_str[] = "\u2004100%";
    snprintf(
        output_str, sizeof(output_str), "%s\u2004%*ld%%", icons[idx], brightness_percent == 100 ? 3 : 2,
        brightness_percent
    );

    update_json(module_id, output_str, IDLE);
}

static void alter(size_t module_id, uint64_t btn) {
    (void)module_id;
    switch (btn) {
    case 4: // up
        system("brightnessctl --class=backlight set +10% >/dev/null &");
        break;
    case 5: // down
        system("brightnessctl --class=backlight set 10%- >/dev/null &");
        break;
    }
}

static void del(size_t module_id) {
    struct backlight_data *data = (struct backlight_data *)modules[module_id].data;
    if (data) {
        if (data->inotify_fd >= 0) {
            close(data->inotify_fd);
        }
        free(data);
    }
}

void init_backlight(int epoll_fd) {
    INIT_BASE;

    // 初始化 inotify
    int inotify_fd = inotify_init1(IN_NONBLOCK);
    if (inotify_fd == -1) {
        perror("inotify_init1");
        modules_cnt--;
        return;
    }

    // 监控亮度文件变化
    int wd = inotify_add_watch(inotify_fd, BRIGHTNESS, IN_MODIFY);
    if (wd == -1) {
        perror("inotify_add_watch");
        close(inotify_fd);
        modules_cnt--;
        return;
    }

    // 分配并初始化结构体
    struct backlight_data *data = malloc(sizeof(struct backlight_data));
    if (!data) {
        perror("malloc");
        close(inotify_fd);
        modules_cnt--;
        return;
    }
    data->inotify_fd = inotify_fd;

    // 注册 epoll 事件
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.u64 = module_id;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, inotify_fd, &ev) == -1) {
        perror("epoll_ctl");
        close(inotify_fd);
        free(data);
        modules_cnt--;
        return;
    }

    modules[module_id].data = data;
    modules[module_id].update = update;
    modules[module_id].alter = alter;
    modules[module_id].del = del;

    UPDATE_Q(module_id);
}