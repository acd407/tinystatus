#include "cJSON.h"
#include <linux/limits.h>
#include <module_base.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <tools.h>
#include <unistd.h>
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <unistd.h>
#define BRIGHTNESS "/sys/class/backlight/amdgpu_bl0/brightness"

#define BUF_LEN (10 * (sizeof (struct inotify_event) + NAME_MAX + 1))

static int create_inotify () {
    int inotify_fd;
    int wd;

    // 初始化 inotify
    inotify_fd = inotify_init1 (IN_NONBLOCK);
    if (inotify_fd == -1) {
        perror ("inotify_init1");
        exit (EXIT_FAILURE);
    }

    // 添加需要监控的文件
    wd = inotify_add_watch (inotify_fd, BRIGHTNESS, IN_MODIFY);
    if (wd == -1) {
        perror ("inotify_add_watch");
        close (inotify_fd);
        exit (EXIT_FAILURE);
    }

    return inotify_fd;
}

static int read_brightness_value () {
    char buffer[32];
    // 使用 pread 避免改变文件偏移量
    ssize_t len =
        pread (modules[module_id].fds[1], buffer, sizeof (buffer) - 1, 0);
    if (len == -1) {
        perror ("pread");
        exit (EXIT_FAILURE);
    }

    buffer[len] = '\0'; // 确保字符串以 NULL 结尾
    int retval = atoi (buffer);
    if (retval == 0 && buffer[0] != 0)
        exit (EXIT_FAILURE);
    return retval;
}

static void update () {
    if (modules[module_id].output) {
        free (modules[module_id].output);
    }
    cJSON *json = cJSON_CreateObject ();

    char name[] = "A";
    *name += module_id;

    cJSON_AddStringToObject (json, "name", name);
    cJSON_AddFalseToObject (json, "separator");
    cJSON_AddNumberToObject (json, "separator_block_width", 0);
    cJSON_AddStringToObject (json, "markup", "pango");
    cJSON_AddStringToObject (json, "color", IDLE);

    // 读取 inotify 事件
    char buffer[BUF_LEN];
    struct inotify_event *events = (struct inotify_event *) buffer;
    ssize_t len = read (modules[module_id].fds[0], buffer, BUF_LEN);
    if (len == -1 && errno != EAGAIN) {
        perror ("read");
        exit (EXIT_FAILURE);
    }
    // 处理事件
    int brightness_val = -1;
    brightness_val = read_brightness_value ();
    assert (brightness_val >= 0);
    char output_str[] = "\u200422%";
    snprintf (
        output_str, sizeof (output_str), "\u2004%2d%%",
        brightness_val * 100 / 255
    );
    cJSON_AddStringToObject (json, "full_text", output_str);

    modules[module_id].output = cJSON_PrintUnformatted (json);

    cJSON_Delete (json);
}

void init_backlight (int epoll_fd) {
    INIT_BASE ();

    int inotify_fd = create_inotify ();

    // 将 inotify 文件描述符添加到 epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.u64 = module_id;
    if (epoll_ctl (epoll_fd, EPOLL_CTL_ADD, inotify_fd, &ev) == -1) {
        perror ("epoll_ctl");
        close (inotify_fd);
        close (epoll_fd);
        exit (EXIT_FAILURE);
    }

    int file_fd = open (BRIGHTNESS, O_RDONLY);
    if (file_fd == -1) {
        perror ("open");
        exit (EXIT_FAILURE);
    }
    modules[module_id].fds = malloc (sizeof (int) * 3);
    modules[module_id].fds[0] = inotify_fd;
    modules[module_id].fds[1] = file_fd;
    modules[module_id].fds[2] = -1;
    modules[module_id].update = update;

    UPDATE_Q ();
}
