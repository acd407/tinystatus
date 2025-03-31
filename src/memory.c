#include <cJSON.h>
#include <main.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tools.h>

static size_t module_id;

static void update_memory_usage (uint64_t *used, double *percent) {
    FILE *fp;
    char buffer[BUF_SIZE];
    char *token;

    fp = fopen ("/proc/meminfo", "r");
    if (!fp) {
        perror ("Failed to open /proc/meminfo");
        exit (EXIT_FAILURE);
    }

    uint64_t total, avaliable;
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

static void memory_update () {
    if (modules[module_id].output) {
        free (modules[module_id].output);
    }

    cJSON *json = cJSON_CreateObject ();

    // MOD_SZIE只有16，故name只有2位，在此，随便给几位
    char name_buffer[4 + 1];
    snprintf (name_buffer, sizeof (name_buffer), "%ld", module_id);
    // 添加键值对到JSON对象
    cJSON_AddStringToObject (json, "name", name_buffer);
    cJSON_AddFalseToObject (json, "separator");
    cJSON_AddNumberToObject (json, "separator_block_width", 0);
    cJSON_AddStringToObject (json, "markup", "pango");

    // 󰍛: 4 byte
    // \u2004: 3 byte
    char output_str[] = "󰍛\u20040.00K";
    uint64_t used;
    double usage;
    update_memory_usage (&used, &usage);
    format_storage_units (output_str + 7, used);
    cJSON_AddStringToObject (json, "full_text", output_str);

    char *colors[] = {IDLE, WARNING, CRITICAL};
    char *color = colors[0];
    if (usage > 50)
        color = colors[1];
    if (usage > 80)
        color = colors[2];
    cJSON_AddStringToObject (json, "color", color);

    // 将JSON对象转换为字符串
    modules[module_id].output = cJSON_PrintUnformatted (json);

    // 删除JSON对象并释放内存
    cJSON_Delete (json);
}

void memory_init (int epoll_fd) {
    (void) epoll_fd;
    module_id = modules_cnt++;
    modules[module_id].output = NULL;
    modules[module_id].update = memory_update;
    modules[module_id].sec = true;
    modules[module_id].fds = NULL;
}
