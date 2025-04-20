#include <cJSON.h>
#include <module_base.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tools.h>

static void get_usage (uint64_t *used, double *percent) {
    FILE *fp;
    char buffer[BUF_SIZE];
    char *token;

    fp = fopen ("/proc/meminfo", "r");
    if (!fp) {
        perror ("Failed to open /proc/meminfo");
        exit (EXIT_FAILURE);
    }

    uint64_t total = 0, avaliable = 0;
    while (fgets (buffer, sizeof (buffer), fp)) {
        if (strncmp (buffer, "MemTotal:", 9) == 0) {
            token = strtok (buffer, " ");
            token = strtok (NULL, " ");
            total = strtoul (token, NULL, 10);
        } else if (strncmp (buffer, "MemAvailable:", 13) == 0) {
            token = strtok (buffer, " ");
            token = strtok (NULL, " ");
            avaliable = strtoul (token, NULL, 10);
        }
    }

    fclose (fp);

    *used = total - avaliable;
    *percent = (double) *used / total;
    *used *= 1024;
}

static void update (size_t module_id) {
    char output_str[] = "\xf3\xb0\x8d\x9b\u20040.00K"; // 󰍛
    uint64_t used;
    double usage;
    get_usage (&used, &usage);
    format_storage_units (output_str + 7, used);

    char *colors[] = {IDLE, WARNING, CRITICAL};
    size_t idx;
    if (usage < 50)
        idx = 0;
    else if (usage < 80)
        idx = 1;
    else
        idx = 2;

    update_json (module_id, output_str, colors[idx]);
}

void init_memory (int epoll_fd) {
    (void) epoll_fd;
    INIT_BASE;

    modules[module_id].update = update;
    modules[module_id].interval = 2;

    UPDATE_Q (module_id);
}
