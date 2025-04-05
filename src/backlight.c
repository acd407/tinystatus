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

#define BUF_LEN (5 * (sizeof (struct inotify_event)))

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

static void update (size_t module_id) {
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
    ssize_t len = read (modules[module_id].fds[0], buffer, BUF_LEN);
    if (len == -1 && errno != EAGAIN) {
        perror ("read");
        exit (EXIT_FAILURE);
    }
    // 处理事件
    uint64_t brightness = read_uint64_file (BRIGHTNESS) * 100 / 255;
    assert (brightness <= 100);
    brightness = (brightness + 1) / 5 * 5;
    char *icons[] = {
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
    size_t idx = sizeof (icons) / sizeof (char *) * brightness / 101;
    char output_str[] = "\ue3e0\u2004100%";
    snprintf (
        output_str, sizeof (output_str), "%s\u2004%*ld%%", icons[idx],
        brightness == 100 ? 3 : 2, brightness
    );
    cJSON_AddStringToObject (json, "full_text", output_str);

    modules[module_id].output = cJSON_PrintUnformatted (json);

    cJSON_Delete (json);
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

    modules[module_id].fds = malloc (sizeof (int) * 2);
    modules[module_id].fds[0] = inotify_fd;
    modules[module_id].fds[1] = -1;
    modules[module_id].update = update;
    modules[module_id].alter = alter;

    UPDATE_Q ();
}
