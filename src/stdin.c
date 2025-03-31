#include <errno.h>
#include <fcntl.h>
#include <main.h>
#include <stdbool.h>
#include <stdin.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>

static size_t module_id;

// 设置非阻塞 IO
static void set_nonblocking (int fd) {
    int flags = fcntl (fd, F_GETFL, 0);
    if (flags == -1) {
        perror ("fcntl F_GETFL");
        exit (EXIT_FAILURE);
    }
    if (fcntl (fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror ("fcntl F_SETFL");
        exit (EXIT_FAILURE);
    }
}

// 解析 i3bar 输入
static void parse_i3bar_input (const char *input) {
    // i3bar 输入通常是 JSON 格式
    fputs (input, stderr);
}

// 处理标准输入事件
static void handle_stdin_event () {
    char buffer[BUF_SIZE];
    ssize_t n = read (modules[module_id].fds[0], buffer, BUF_SIZE - 1);

    if (n == -1) {
        if (errno != EAGAIN) {
            perror ("read stdin");
        }
        return;
    } else if (n == 0) {
        // EOF，i3bar 可能已关闭
        printf ("i3bar closed the input\n");
        exit (EXIT_SUCCESS);
    }

    buffer[n] = '\0';
    parse_i3bar_input (buffer);
}

void stdin_init (int epoll_fd) {
    module_id = modules_cnt++;

    set_nonblocking (STDIN_FILENO);

    struct epoll_event stdin_event;
    stdin_event.events = EPOLLIN | EPOLLET; // 边缘触发模式
    stdin_event.data.u64 = module_id;
    if (epoll_ctl (epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &stdin_event) == -1) {
        perror ("epoll_ctl stdin");
        exit (EXIT_FAILURE);
    }

    modules[module_id].sec = false;
    modules[module_id].fds = malloc (sizeof (int) * 2);
    modules[module_id].fds[0] = STDIN_FILENO;
    modules[module_id].fds[1] = -1;
    modules[module_id].update = handle_stdin_event;
    modules[module_id].output = NULL;
}
