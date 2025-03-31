#include <errno.h>
#include <main.h>
#include <network.h>
#include <stdin.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <timer.h>
#include <unistd.h>

#define MAX_EVENTS 10

module_t modules[MOD_SIZE];
size_t modules_cnt;

static void init (int epoll_fd) { // 调整注册的顺序，实现输出的顺序
    modules_cnt = 0;
    network_init (epoll_fd);
    timer_init (epoll_fd);
    stdin_init (epoll_fd);
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
        for (int i = 0; i < nfds; i++) {
            // fprintf (stderr, "call %ld update\n", events[i].data.u64);
            // fflush (stderr);
            modules[events[i].data.u64].update ();
        }
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
