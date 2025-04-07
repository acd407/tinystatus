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
    char *color = colors[0];
    if (usage > 50)
        color = colors[1];
    if (usage > 80)
        color = colors[2];

    cJSON *json = cJSON_CreateObject ();

    char name[] = "A";
    *name += module_id;

    cJSON_AddStringToObject (json, "name", name);
    cJSON_AddFalseToObject (json, "separator");
    cJSON_AddNumberToObject (json, "separator_block_width", 0);
    cJSON_AddStringToObject (json, "markup", "pango");
    cJSON_AddStringToObject (json, "color", color);
    cJSON_AddStringToObject (json, "full_text", output_str);

    if (modules[module_id].output) {
        free (modules[module_id].output);
    }
    modules[module_id].output = cJSON_PrintUnformatted (json);

    cJSON_Delete (json);
}

void init_memory (int epoll_fd) {
    (void) epoll_fd;
    INIT_BASE;

    modules[module_id].update = update;
    modules[module_id].interval = 2;

    UPDATE_Q (module_id);
}
