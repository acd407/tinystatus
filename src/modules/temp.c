#include <cJSON.h>
#include <math.h>
#include <module_base.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <tools.h>

#define Tctl "/sys/class/hwmon/hwmon5/temp1_input"

static void update (size_t module_id) {
    char output_str[] = "ico\u200435.3";

    double temp = read_uint64_file (Tctl) / 1e3;

    char *icons[] = {
        "", "", "", "", "",
    };
    size_t idx = ARRAY_SIZE (icons) * (temp - 40) / 61;
    idx = idx > 0 ? idx : 0;
    snprintf (
        output_str, sizeof (output_str), "%s\u2004%3.*f", icons[idx],
        temp < 10 ? 2 : 1, temp
    );

    char *colors[] = {COOL, IDLE, WARNING, CRITICAL};
    if (temp < 30)
        idx = 0;
    else if (temp < 60)
        idx = 1;
    else if (temp < 80)
        idx = 2;
    else
        idx = 3;

    update_json (module_id, output_str, colors[idx]);
}

void init_temp (int epoll_fd) {
    (void) epoll_fd;
    INIT_BASE;

    modules[module_id].update = update;
    modules[module_id].interval = 1;

    UPDATE_Q (module_id);
}
