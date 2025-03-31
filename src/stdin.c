#include <cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <module_base.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>

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
static void parse_input (const char *input) {
    fputs (input, stderr);
    const char *idx = input;
    while (*idx && *idx != '{')
        idx++;
    if (!*idx)
        return;
    cJSON *root = cJSON_Parse (idx);
    if (root == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr ();
        if (error_ptr != NULL) {
            fprintf (stderr, "Error before: %s\n", error_ptr);
        }
        exit (EXIT_FAILURE);
    }
    cJSON *name = cJSON_GetObjectItemCaseSensitive (root, "name");
    cJSON *button = cJSON_GetObjectItemCaseSensitive (root, "button");
    // cJSON *event = cJSON_GetObjectItemCaseSensitive (root, "event");
    uint64_t btn_nr = 0;
    if (cJSON_IsNumber (button))
        btn_nr = button->valueint;
    if (btn_nr != 0 && cJSON_IsString (name) && name->valuestring != NULL) {
        uint64_t nr = name->valuestring[0] - 'A';
        if (modules[nr].alter)
            modules[nr].alter (btn_nr);
    }
    cJSON_Delete (root);
}

// 处理标准输入事件
static void update () {
    char buffer[BUF_SIZE];
    ssize_t n = read (modules[module_id].fds[0], buffer, BUF_SIZE - 1);

    if (n == -1) {
        if (errno != EAGAIN) {
            perror ("read stdin");
            exit (EXIT_FAILURE);
        }
        return;
    } else if (n == 0) {
        // EOF，i3bar 可能已关闭
        printf ("i3bar closed the input\n");
        exit (EXIT_SUCCESS);
    }

    buffer[n] = '\0';
    parse_input (buffer);
}

void init_stdin (int epoll_fd) {
    init_base ();

    // 从某种意义上讲，stdin 是实时的，因此下面的代码是在注册 epoll
    set_nonblocking (STDIN_FILENO);

    struct epoll_event stdin_event;
    stdin_event.events = EPOLLIN | EPOLLET; // 边缘触发模式
    stdin_event.data.u64 = module_id;
    if (epoll_ctl (epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &stdin_event) == -1) {
        perror ("epoll_ctl stdin");
        exit (EXIT_FAILURE);
    }

    modules[module_id].fds = malloc (sizeof (int) * 2);
    modules[module_id].fds[0] = STDIN_FILENO;
    modules[module_id].fds[1] = -1;
    modules[module_id].update = update;
}
