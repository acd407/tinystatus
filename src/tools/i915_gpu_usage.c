#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <time.h>

// 来自 i915_drm.h
#define I915_PMU_ENGINE_BUSY(class, instance) (((class) << 16) | ((instance) << 8) | 0x0)

#define I915_ENGINE_CLASS_RENDER 0

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

static uint64_t nsec_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

int main(void) {
    struct perf_event_attr pe;
    uint64_t type;
    int fd;

    /* 读取 i915 perf type */
    FILE *f = fopen("/sys/bus/event_source/devices/i915/type", "r");
    if (!f) {
        perror("open i915 type");
        return 1;
    }
    fscanf(f, "%lu", &type);
    fclose(f);

    memset(&pe, 0, sizeof(pe));
    pe.type = type;
    pe.size = sizeof(pe);
    pe.config = I915_PMU_ENGINE_BUSY(I915_ENGINE_CLASS_RENDER, 0);
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;

    fd = perf_event_open(&pe, -1, 0, -1, 0);
    if (fd < 0) {
        perror("perf_event_open");
        return 1;
    }

    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);

    uint64_t prev_value = 0;
    uint64_t prev_time = nsec_now();
    read(fd, &prev_value, sizeof(prev_value));

    while (1) {
        uint64_t curr_value = 0;
        uint64_t curr_time = nsec_now();
        read(fd, &curr_value, sizeof(curr_value));

        uint64_t busy_delta = curr_value - prev_value;
        uint64_t time_delta = curr_time - prev_time;

        double util = (double)busy_delta / (double)time_delta * 100.0;
        if (util > 100.0)
            util = 100.0;

        printf("%f\n", util);
        fflush(stdout);

        prev_value = curr_value;
        prev_time = curr_time;
        sleep(1);
    }

    close(fd);
    return 0;
}
