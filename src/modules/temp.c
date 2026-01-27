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
#include <unistd.h>

#define PACKAGE "/sys/class/hwmon/hwmon1/temp1_input"

static void update(size_t module_id) {
    char output_str[] = "ico\u200435.3";

    // 从模块数据中获取PACKAGE路径
    char *package_path = (char *)modules[module_id].data;
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
    if (modules[module_id].data) {
        free(modules[module_id].data);
        modules[module_id].data = NULL;
    }
}

// 温度传感器查找策略
typedef struct {
    const char *path_pattern;   // 文件路径模式
    const char *target_content; // 要匹配的文件内容
    const char *regex_pattern;  // 正则表达式模式
    const char *regex_replace;  // 正则表达式替换
} temp_sensor_strategy_t;

// 温度传感器查找策略数组
static const temp_sensor_strategy_t sensor_strategies[] = {
    {.path_pattern = "/sys/class/thermal/thermal_zone*/type",
     .target_content = "x86_pkg_temp",
     .regex_pattern = "type",
     .regex_replace = "temp"},
    {.path_pattern = "/sys/class/hwmon/hwmon*/temp*_label",
     .target_content = "Package id 0",
     .regex_pattern = "temp(.*)_label",
     .regex_replace = "temp\\1_input"},
    {.path_pattern = "/sys/class/hwmon/hwmon*/temp*_label",
     .target_content = "Tctl",
     .regex_pattern = "temp(.*)_label",
     .regex_replace = "temp\\1_input"}
};

// 根据策略查找温度传感器路径
static char *find_temp_sensor_path(const temp_sensor_strategy_t *strategy) {
    // 查找匹配的文件路径
    char *found_path = match_content_path(strategy->path_pattern, strategy->target_content);
    if (found_path) {
        // 使用正则表达式替换路径中的部分
        char *sensor_path = regex(found_path, strategy->regex_pattern, strategy->regex_replace);
        free(found_path);
        return sensor_path;
    }

    return NULL;
}

// 查找 Package 温度传感器路径
static char *find_package_temp_path(void) {
    // 按照策略数组顺序尝试查找
    for (size_t i = 0; i < ARRAY_SIZE(sensor_strategies); i++) {
        char *sensor_path = find_temp_sensor_path(&sensor_strategies[i]);
        if (sensor_path) {
            return sensor_path;
        }
    }

    // 所有策略都失败，返回默认路径
    fprintf(stderr, "Warning: Could not find Package temperature sensor, using default path\n");
    return strdup(PACKAGE);
}

void init_temp(int epoll_fd) {
    (void)epoll_fd;
    INIT_BASE;

    // 查找 Package 温度传感器路径
    char *package_path = find_package_temp_path();

    // 验证路径是否有效
    if (!package_path || access(package_path, R_OK) != 0) {
        fprintf(stderr, "Error: Cannot access temperature sensor at %s\n", package_path ? package_path : "NULL");
        if (package_path) {
            free(package_path);
            package_path = strdup(PACKAGE);
        }
    }

    fprintf(stderr, "Found Package temperature sensor path: %s\n", package_path);

    // 将路径存储在模块数据中
    modules[module_id].data = package_path;

    modules[module_id].update = update;
    modules[module_id].del = cleanup;
    modules[module_id].interval = 1;

    UPDATE_Q(module_id);
}
