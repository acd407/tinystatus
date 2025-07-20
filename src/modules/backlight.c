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
#define BRIGHTNESS "/sys/class/backlight/amdgpu_bl0/brightness"
#define MAX_BRIGHTNESS "/sys/class/backlight/amdgpu_bl0/max_brightness"

#define BUF_LEN (5 * (sizeof (struct inotify_event)))

static const char *icons[] = {
    "\ue3d5", // 
    "\ue3d4", // 
    "\ue3d3", // 
    "\ue3d2", // 
    "\ue3d1", // 
    "\ue3d0", // 
    "\ue3cf", // 
    "\ue3ce", // 
    "\ue3cd", // 
    "\ue3cc", // 
    "\ue3cb", // 
    "\ue3ca", // 
    "\ue3c9", // 
    "\ue3c8", // 
    "\ue3e3", // 
};

static void update (size_t module_id) {
    // 读取 inotify 事件
    char buffer[BUF_LEN];
    ssize_t len = read (modules[module_id].data.num, buffer, BUF_LEN);
    if (len == -1 && errno != EAGAIN) {
        perror ("read");
        exit (EXIT_FAILURE);
    }

    // 处理事件
    uint64_t brightness =
        read_uint64_file (BRIGHTNESS) * 100 / read_uint64_file (MAX_BRIGHTNESS);
    assert (brightness <= 100);
    brightness = (brightness + 1) / 5 * 5;
    size_t idx = ARRAY_SIZE (icons) * brightness_percent / 101;

    char output_str[] = "\ue3e0\u2004100%";
    snprintf (
        output_str, sizeof (output_str), "%s\u2004%*ld%%", icons[idx],
        brightness == 100 ? 3 : 2, brightness
    );

    update_json (module_id, output_str, IDLE);
}

static void alter (size_t module_id, uint64_t btn) {
    (void) module_id;
    switch (btn) {
    case 4: // up
        system ("~/.bin/wm/backlight i >/dev/null &");
        break;
    case 5: // down
        system ("~/.bin/wm/backlight d >/dev/null &");
        break;
    }
}

void init_backlight (int epoll_fd) {
    INIT_BASE;

    // 初始化 inotify
    int inotify_fd = inotify_init1 (IN_NONBLOCK);
    if (inotify_fd == -1) {
        perror ("inotify_init1");
        modules_cnt--;
        return;
    }

    // 添加需要监控的文件
    if (inotify_add_watch (inotify_fd, BRIGHTNESS, IN_MODIFY) == -1) {
        perror ("inotify_add_watch");
        close (inotify_fd);
        modules_cnt--;
        return;
    }

    // 将 inotify 文件描述符添加到 epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.u64 = module_id;
    if (epoll_ctl (epoll_fd, EPOLL_CTL_ADD, inotify_fd, &ev) == -1) {
        perror ("epoll_ctl");
        close (inotify_fd);
        modules_cnt--;
        return;
    }

    modules[module_id].data.num = inotify_fd;
    modules[module_id].update = update;
    modules[module_id].alter = alter;

    UPDATE_Q (module_id);
}
