#include <errno.h>
#include <main.h>
#include <modules.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <tools.h>
#include <unistd.h>

#define MAX_EVENTS 16

module_t modules[MOD_SIZE];
size_t modules_cnt;

static void output () {
    putchar ('[');
    int output_modules_cnt = 0;
    for (size_t i = 0; i < modules_cnt; i++) {
        if (modules[i].output) {
            if (output_modules_cnt > 0)
                putchar (',');
            fputs (modules[i].output, stdout);
            fputs (
                ",{\"full_text\":\" \",\"separator\":false,"
                "\"separator_block_width\":0,\"markup\":\"pango\"}",
                stdout
            );
            output_modules_cnt++;
        }
    }
    puts ("],"); // 需要一个换行，puts隐含了
    fflush (stdout);
}

static void init (int epoll_fd) { // 注册的顺序，决定输出的顺序
    // 在注册时同时完成的第一次 update
    modules_cnt = 0;
    init_battery (epoll_fd);
    init_backlight (epoll_fd);
    init_volume (epoll_fd);
    init_network (epoll_fd);
    init_gpu (epoll_fd);
    init_memory (epoll_fd);
    init_cpu (epoll_fd);
    init_temp (epoll_fd);
    init_date (epoll_fd);

    init_stdin (epoll_fd);
    init_timer (epoll_fd); // 要在所有 interval != 0 的模块后面
}

int main () {
    // i3bar 协议头
    printf ("{ \"version\": 1, \"click_events\": true }\n[\n[],\n");
    fflush (stdout);

    // 创建 epoll 实例
    int epoll_fd = epoll_create1 (0);
    if (epoll_fd == -1) {
        perror ("epoll_create1");
        exit (EXIT_FAILURE);
    }

    init (epoll_fd);
    output ();
    // 主事件循环
    struct epoll_event events[MAX_EVENTS];
    while (1) {
        int nfds = epoll_wait (epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror ("epoll_wait");
            continue;
        }
        for (int i = 0; i < nfds; i++)
            modules[events[i].data.u64].update (events[i].data.u64);
        output ();
    }

    for (size_t i = 0; i < modules_cnt; i++) {
        if (modules[i].fds) {
            for (int j = 0; modules[i].fds[j] != -1; j++)
                close (modules[i].fds[j]);
            free (modules[i].fds);
        }
        if (modules[i].del)
            modules[i].del (i);
    }
    close (epoll_fd);
    return EXIT_SUCCESS;
}
