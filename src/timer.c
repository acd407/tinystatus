#include <module_base.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <tools.h>
#include <unistd.h>

static int create_timer() {
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timer_fd == -1) {
        perror("timerfd_create");
        exit(EXIT_FAILURE);
    }

    struct itimerspec new_value;
    new_value.it_value.tv_sec = 1;
    new_value.it_value.tv_nsec = 0;
    new_value.it_interval.tv_sec = 1;
    new_value.it_interval.tv_nsec = 0;

    if (timerfd_settime(timer_fd, 0, &new_value, NULL) == -1) {
        perror("timerfd_settime");
        exit(EXIT_FAILURE);
    }

    return timer_fd;
}

struct timer_data {
    int timerfd;
    int counter;
};

static void update(size_t module_id) {
    struct timer_data *td = modules[module_id].data;
    uint64_t expirations;
    ssize_t s = read(td->timerfd, &expirations, sizeof(uint64_t));
    if (td->counter > 0 && s == -1) {
        perror("read timerfd");
        exit(EXIT_FAILURE);
    }
    td->counter += s == -1 ? 0 : expirations;

    for (size_t i = 0; i < modules_cnt; i++)
        if (modules[i].interval && td->counter % modules[i].interval == 0)
            modules[i].update(i);
}

void init_timer(int epoll_fd) {
    INIT_BASE;

    // 从某种意义上讲，timer 是实时的，因此下面的代码是在注册 epoll
    int timer_fd = create_timer();

    struct epoll_event timer_event;
    timer_event.events = EPOLLIN | EPOLLET;
    timer_event.data.u64 = module_id;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timer_fd, &timer_event) == -1) {
        perror("epoll_ctl timer");
        exit(EXIT_FAILURE);
    }

    struct timer_data *td = malloc(sizeof(struct timer_data));
    td->timerfd = timer_fd;
    td->counter = 0;
    modules[module_id].data = td;
    modules[module_id].update = update;

    UPDATE_Q(module_id);
}