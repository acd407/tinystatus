#include <cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <module_base.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
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
        // EOF，i3bar 可能已关闭
        printf("i3bar closed the input\n");
        exit(EXIT_SUCCESS);
    }
    input[n] = '\0';

    // parse，调用其他模块的 alter
    const char *idx = input;
    while (*idx && *idx != '{')
        idx++;
    if (!*idx) // 开始的行仅有 [
        return;

    fputs(input, stderr);

    cJSON *root = cJSON_Parse(idx);
    if (root == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            fprintf(stderr, "Error before: %s\n", error_ptr);
        }
        exit(EXIT_FAILURE);
    }
    cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
    cJSON *button = cJSON_GetObjectItemCaseSensitive(root, "button");

    uint64_t btn_nr = 0;
    if (cJSON_IsNumber(button))
        btn_nr = button->valueint;
    if (btn_nr != 0 && cJSON_IsString(name) && name->valuestring != NULL) {
        uint64_t nr = name->valuestring[0] - 'A';
        // output 应在模块内的 alter 中调用
        // 因为一些模块可能根本在 alter 后就不想输出
        if (modules[nr].alter)
            modules[nr].alter(nr, btn_nr);
    }
    cJSON_Delete(root);
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