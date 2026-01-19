#include <cJSON.h>
#include <math.h>
#include <module_base.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <tools.h>
#include <limits.h>

#define PACKAGE "/sys/class/hwmon/hwmon1/temp1_input"

static void update(size_t module_id) {
    char output_str[] = "ico\u200435.3";

    // 从模块数据中获取PACKAGE路径
    char *package_path = (char *)modules[module_id].data.ptr;
    if (!package_path) {
        update_json(module_id, "N/A", CRITICAL);
        return;
    }

    double temp = read_uint64_file(package_path) / 1e3;

    char *icons[] = {
        "", "", "", "", "",
    };
    size_t icon_idx = ARRAY_SIZE(icons) * (temp - 40) / 61;
    icon_idx = min(icon_idx, ARRAY_SIZE(icons) - 1);
    snprintf(output_str, sizeof(output_str), "%s\u2004%3.*f", icons[icon_idx], temp < 10 ? 2 : 1, temp);

    char *colors[] = {COOL, IDLE, WARNING, CRITICAL};
    if (temp < 30)
        icon_idx = 0;
    else if (temp < 60)
        icon_idx = 1;
    else if (temp < 80)
        icon_idx = 2;
    else
        icon_idx = 3;

    update_json(module_id, output_str, colors[icon_idx]);
}

static void cleanup(size_t module_id) {
    // 释放分配的PACKAGE路径内存
    if (modules[module_id].data.ptr) {
        free(modules[module_id].data.ptr);
        modules[module_id].data.ptr = NULL;
    }
}

void init_temp(int epoll_fd) {
    (void)epoll_fd;
    INIT_BASE;

    char *package_path = NULL;

    // 查找Package温度传感器路径
    char *label_path = match_content_path("/sys/class/hwmon/hwmon*/temp*_label", "Package id 0");
    if (label_path) {
        package_path = regex(label_path, "temp(.*)_label", "temp\\1_input");
        free(label_path);
        fprintf(stderr, "Found Package temperature sensor path: %s\n", package_path);
    } else {
        fprintf(stderr, "Failed to find Package temperature sensor\n");
        package_path = strdup(PACKAGE);
    }

    // 将路径存储在模块数据中
    modules[module_id].data.ptr = package_path;

    modules[module_id].update = update;
    modules[module_id].del = cleanup;
    modules[module_id].interval = 1;

    UPDATE_Q(module_id);
}
