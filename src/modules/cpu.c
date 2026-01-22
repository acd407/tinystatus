#include <cJSON.h>
#include <math.h>
#include <module_base.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tools.h>
#include <limits.h>

#define PACKAGE "/sys/class/powercap/intel-rapl:0/energy_uj"
#define CORE "/sys/class/powercap/intel-rapl:0:0/energy_uj"
#define PSYS "/sys/class/powercap/intel-rapl:1/energy_uj"
#define SVI2_P_Core "/sys/class/hwmon/hwmon3/power1_input"
#define SVI2_P_SoC "/sys/class/hwmon/hwmon3/power2_input"
#define USE_RAPL

// CPU模块数据结构体
typedef struct {
    uint64_t prev_idle;
    uint64_t prev_total;
    uint64_t prev_energy;
    char *package_path;
} cpu_data_t;

static double get_usage(size_t module_id) {
    cpu_data_t *data = (cpu_data_t *)modules[module_id].data;
    char buffer[BUF_SIZE];
    uint64_t idx, nice, system, idle, iowait, irq, softirq;

    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) {
        perror("Failed to open /proc/stat");
        exit(EXIT_FAILURE);
    }
    if (!fgets(buffer, sizeof(buffer), fp)) {
        fclose(fp);
        exit(EXIT_FAILURE);
    }
    fclose(fp);

    if (sscanf(buffer, "cpu %lu %lu %lu %lu %lu %lu %lu", &idx, &nice, &system, &idle, &iowait, &irq, &softirq) != 7) {
        exit(EXIT_FAILURE);
    }

    uint64_t total = idx + nice + system + idle + iowait + irq + softirq;
    uint64_t total_idle = idle + iowait;
    uint64_t total_diff = total - data->prev_total;
    uint64_t idle_diff = total_idle - data->prev_idle;

    double cpu_usage;
    if (data->prev_total != 0 && total_diff != 0) {
        cpu_usage = 100.0f * (total_diff - idle_diff) / total_diff;
    } else {
        cpu_usage = 0.0f;
    }

    data->prev_idle = total_idle;
    data->prev_total = total;

    return cpu_usage;
}

#ifdef USE_RAPL
static double get_power(size_t module_id) {
    cpu_data_t *data = (cpu_data_t *)modules[module_id].data;
    uint64_t energy = read_uint64_file(data->package_path);

    if (!data->prev_energy)
        data->prev_energy = energy;
    uint64_t energy_diff = energy - data->prev_energy;
    double power = (double)energy_diff / 1e6;

    data->prev_energy = energy;
    return power;
}
#else
static double get_power(size_t module_id) {
    (void)module_id;
    uint64_t uwatt_core = read_uint64_file(SVI2_P_Core);
    uint64_t uwatt_soc = read_uint64_file(SVI2_P_SoC);
    return (uwatt_core + uwatt_soc) / 1e6;
}
#endif

static void alter(size_t module_id, uint64_t btn) {
    switch (btn) {
    case 3: // right button
        modules[module_id].state ^= 1;
        break;
    default:
        return;
    }
    modules[module_id].update(module_id);
}

static void update(size_t module_id) {
    const char *icons[] = {"󰾆", "󰾅", "󰓅"};
    const char *colors[] = {IDLE, WARNING, CRITICAL};
    const char *format_active = "\u2004%4.*fW";
    const char *format_inactive = "\u2004%4.*f%%";

    double usage = get_usage(module_id);
    bool is_active = modules[module_id].state;

    size_t icon_idx = (usage * ARRAY_SIZE(icons)) / 101;
    icon_idx = min(icon_idx, ARRAY_SIZE(icons) - 1);
    const char *icon = icons[icon_idx];

    size_t color_idx = (usage < 30) ? 0 : (usage < 60) ? 1 : 2;
    const char *color = colors[color_idx];

    char output_str[32]; // 适当大小的缓冲区

    size_t icon_len = strlen(icon);
    memcpy(output_str, icon, icon_len);

    if (is_active) {
        double power = get_power(module_id);
        snprintf(output_str + icon_len, sizeof(output_str) - icon_len, format_active, power < 10 ? 2 : 1, power);
    } else {
        snprintf(output_str + icon_len, sizeof(output_str) - icon_len, format_inactive, usage < 10 ? 2 : 1, usage);
    }

    update_json(module_id, output_str, color);
}

static void del(size_t module_id) {
    cpu_data_t *data = (cpu_data_t *)modules[module_id].data;
    if (data) {
        if (data->package_path) {
            free(data->package_path);
        }
        free(data);
    }
}

void init_cpu(int epoll_fd) {
    (void)epoll_fd;
    INIT_BASE;

    modules[module_id].alter = alter;
    modules[module_id].update = update;
    modules[module_id].interval = 1;

    // 分配CPU数据结构体
    cpu_data_t *data = malloc(sizeof(cpu_data_t));
    if (!data) {
        perror("malloc");
        modules_cnt--;
        return;
    }

    data->prev_idle = 0;
    data->prev_total = 0;
    data->prev_energy = 0;
    data->package_path = NULL;

    modules[module_id].data = data;
    modules[module_id].del = del;

    // 查找RAPL路径
    // 首先找到匹配的name文件
    char *name_path = match_content_path("/sys/class/powercap/intel-rapl*/name", "package-0");
    if (name_path) {
        data->package_path = regex(name_path, "name", "energy_uj");
        free(name_path);
        fprintf(stderr, "Found RAPL package path: %s\n", data->package_path);
    } else {
        fprintf(stderr, "Failed to find RAPL package path, using default\n");
        data->package_path = strdup(PACKAGE);
        if (!data->package_path) {
            perror("strdup");
            modules_cnt--;
            return;
        }
    }

    UPDATE_Q(module_id);
}