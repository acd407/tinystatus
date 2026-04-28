#include <errno.h>
#include <fcntl.h>
#include <module_base.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

struct stdin_data {
    int fd;
};

// 设置非阻塞 IO
static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        exit(EXIT_FAILURE);
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL");
        exit(EXIT_FAILURE);
    }
}

static void update(size_t module_id) {
    char input[BUF_SIZE];
    struct stdin_data *data = (struct stdin_data *)modules[module_id].data;
    ssize_t n = read(data->fd, input, BUF_SIZE - 1);

    if (n == -1) {
        if (errno != EAGAIN) {
            perror("read stdin");
            exit(EXIT_FAILURE);
        }
        return;
    } else if (n == 0) {
        printf("i3bar closed the input\n");
        exit(EXIT_SUCCESS);
    }
    input[n] = '\0';

    // 找到 JSON 对象的开始
    const char *p = input;
    while (*p && *p != '{')
        p++;
    if (!*p)
        return;

    fputs(input, stderr);

    // 解析 button: number
    uint64_t btn = 0;
    const char *btn_str = strstr(p, "\"button\":");
    if (btn_str) {
        const char *val = btn_str + 8; // skip "button":"
        while (*val == ':' || *val == ' ')
            val++;
        btn = strtoul(val, NULL, 10);
    }

    // 解析 name: single-char string like "A"
    uint64_t nr = 0;
    const char *name_str = strstr(p, "\"name\":\"");
    if (name_str) {
        nr = name_str[7] - 'A';
    }

    if (btn != 0 && nr < MOD_SIZE && modules[nr].alter)
        modules[nr].alter(nr, btn);
}

static void del(size_t module_id) {
    struct stdin_data *data = (struct stdin_data *)modules[module_id].data;
    if (data) {
        free(data);
    }
}

void init_stdin(int epoll_fd) {
    INIT_BASE;

    set_nonblocking(STDIN_FILENO);

    struct epoll_event stdin_event;
    stdin_event.events = EPOLLIN | EPOLLET; // 边缘触发模式
    stdin_event.data.u64 = module_id;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &stdin_event) == -1) {
        perror("epoll_ctl stdin");
        exit(EXIT_FAILURE);
    }

    // 分配并初始化结构体
    struct stdin_data *data = malloc(sizeof(struct stdin_data));
    if (!data) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    data->fd = STDIN_FILENO;

    modules[module_id].data = data;
    modules[module_id].update = update;
    modules[module_id].del = del;

    UPDATE_Q(module_id);
}