#include <cJSON.h>
#include <main.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// 模块不可重入，因为此模块统计的就是所有CPU的使用率

static size_t module_id;

static double get_cpu_usage () {
    static uint64_t prev_idle = 0, prev_total = 0;
    char buffer[BUF_SIZE];
    uint64_t idx, nice, system, idle, iowait, irq, softirq;

    FILE *fp = fopen ("/proc/stat", "r");
    if (!fp) {
        perror ("Failed to open /proc/stat");
        exit (EXIT_FAILURE);
    }
    if (!fgets (buffer, sizeof (buffer), fp)) {
        fclose (fp);
        exit (EXIT_FAILURE);
    }
    fclose (fp);

    if (sscanf (
            buffer, "cpu %lu %lu %lu %lu %lu %lu %lu", &idx, &nice, &system,
            &idle, &iowait, &irq, &softirq
        ) != 7) {
        exit (EXIT_FAILURE);
    }

    uint64_t total = idx + nice + system + idle + iowait + irq + softirq;
    uint64_t total_idle = idle + iowait;
    uint64_t total_diff = total - prev_total;
    uint64_t idle_diff = total_idle - prev_idle;

    double cpu_usage;
    if (prev_total != 0 && total_diff != 0) {
        cpu_usage = 100.0f * (total_diff - idle_diff) / total_diff;
    } else {
        cpu_usage = 0.0f;
    }

    prev_idle = total_idle;
    prev_total = total;

    return cpu_usage;
}

static void cpu_update () {
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
    cJSON_AddNumberToObject (json, "separator_block_width", 0); // 添加数字 0
    cJSON_AddStringToObject (json, "markup", "pango");

    char speed[] = "󰾆\u200422.3%";
    double usage = get_cpu_usage ();
    snprintf (
        speed, sizeof (speed), "󰾆\u2004%4.*f%%", usage >= 10 ? 1 : 2, usage
    );
    cJSON_AddStringToObject (json, "full_text", speed);
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

void cpu_init (int epoll_fd) {
    (void) epoll_fd;
    module_id = modules_cnt++;
    modules[module_id].output = NULL;
    modules[module_id].update = cpu_update;
    modules[module_id].sec = true;
    modules[module_id].fds = NULL;
}
