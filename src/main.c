#include <errno.h>
#include <main.h>
#include <modules.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>

#define MAX_EVENTS 16

module_t modules[MOD_SIZE];
size_t modules_cnt;

static void init (int epoll_fd) { // 注册的顺序，决定输出的顺序
    modules_cnt = 0;
    init_network (epoll_fd);
    init_memory (epoll_fd);
    init_cpu (epoll_fd);
    init_date (epoll_fd);

    init_stdin (epoll_fd);
    init_timer (epoll_fd); // 放到后面，因为 timer 初始化完成后要进行一次 output
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
    // 主事件循环
    struct epoll_event events[MAX_EVENTS];
    while (1) {
        int nfds = epoll_wait (epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror ("epoll_wait");
            exit (EXIT_FAILURE);
        }
        for (int i = 0; i < nfds; i++)
            modules[events[i].data.u64].update ();
    }

    for (size_t i = 0; i < modules_cnt; i++) {
        if (modules[i].fds) {
            for (int j = 0; modules[i].fds[j] != -1; j++)
                close (modules[i].fds[j]);
            free (modules[i].fds);
        }
    }
    close (epoll_fd);
    return EXIT_SUCCESS;
}
