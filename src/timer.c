#include <main.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <timer.h>
#include <unistd.h>
#include <utils.h>

static size_t module_id;

static int create_timer () {
    int timer_fd = timerfd_create (CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timer_fd == -1) {
        perror ("timerfd_create");
        exit (EXIT_FAILURE);
    }

    struct itimerspec new_value;
    new_value.it_value.tv_sec = 1;
    new_value.it_value.tv_nsec = 0;
    new_value.it_interval.tv_sec = 1;
    new_value.it_interval.tv_nsec = 0;

    if (timerfd_settime (timer_fd, 0, &new_value, NULL) == -1) {
        perror ("timerfd_settime");
        exit (EXIT_FAILURE);
    }

    return timer_fd;
}

static void sec_update () {
    for (size_t i = 0; i < modules_cnt; i++)
        if (modules[i].sec) {
            // fprintf (stderr, "call %ld update\n", i);
            // fflush (stderr);
            modules[i].update ();
        }
}

static void output () {
    putchar ('[');
    int output_modules_cnt = 0;
    for (size_t i = 0; i < modules_cnt; i++) {
        if (modules[i].output) {
            if (output_modules_cnt > 0)
                putchar (',');
            fputs (modules[i].output, stdout);
            output_modules_cnt++;
        }
    }
    puts ("],"); // 需要一个换行，puts隐含了
}

static void timer_update () {
    uint64_t expirations;
    ssize_t s =
        read (modules[module_id].fds[0], &expirations, sizeof (uint64_t));
    if (s != sizeof (uint64_t)) {
        perror (__FILE__ ": read timerfd");
        return;
    }

    sec_update ();
    output ();

    fflush (stdout);
}

void timer_init (int epoll_fd) {
    module_id = modules_cnt++;

    // 创建并添加计时器到 epoll
    int timer_fd = create_timer ();

    struct epoll_event timer_event;
    timer_event.events = EPOLLIN | EPOLLET;
    timer_event.data.u64 = module_id;
    if (epoll_ctl (epoll_fd, EPOLL_CTL_ADD, timer_fd, &timer_event) == -1) {
        perror (__FILE__ ": epoll_ctl timer");
        exit (EXIT_FAILURE);
    }

    modules[module_id].sec = false; // 避免递归
    modules[module_id].fds = malloc (sizeof (int) * 2);
    modules[module_id].fds[0] = timer_fd;
    modules[module_id].fds[1] = -1;
    modules[module_id].update = timer_update;
    modules[module_id].output = NULL;
}
