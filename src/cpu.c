#include <cJSON.h>
#include <math.h>
#include <module_base.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <tools.h>

#define PACKAGE "/sys/class/powercap/intel-rapl:0/energy_uj"
#define CORE "/sys/class/powercap/intel-rapl:0:0/energy_uj"
#define SVI2_P_Core "/sys/class/hwmon/hwmon3/power1_input"
#define SVI2_P_SoC "/sys/class/hwmon/hwmon3/power2_input"
// #define USE_RAPL

static double get_usage (size_t module_id) {
    uint64_t *prev_idle = &((uint64_t *) modules[module_id].data.ptr)[0];
    uint64_t *prev_total = &((uint64_t *) modules[module_id].data.ptr)[1];
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
    uint64_t total_diff = total - *prev_total;
    uint64_t idle_diff = total_idle - *prev_idle;

    double cpu_usage;
    if (*prev_total != 0 && total_diff != 0) {
        cpu_usage = 100.0f * (total_diff - idle_diff) / total_diff;
    } else {
        cpu_usage = 0.0f;
    }

    *prev_idle = total_idle;
    *prev_total = total;

    return cpu_usage;
}

#ifdef USE_RAPL
static double get_power (size_t module_id) {
    uint64_t *previous_energy = &((uint64_t *) modules[module_id].data.ptr)[2];

    uint64_t energy = read_uint64_file (PACKAGE);

    if (!*previous_energy)
        *previous_energy = energy;
    uint64_t energy_diff = energy - *previous_energy;
    double power = (double) energy_diff / 1e6;

    *previous_energy = energy;
    return power;
}
#else
static double get_power (size_t module_id) {
    (void) module_id;
    uint64_t uwatt_core = read_uint64_file (SVI2_P_Core);
    uint64_t uwatt_soc = read_uint64_file (SVI2_P_SoC);
    return (uwatt_core + uwatt_soc) / 1e6;
}
#endif

static void alter (size_t module_id, uint64_t btn) {
    switch (btn) {
    case 3: // right button
        modules[module_id].state ^= 1;
        break;
    default:
        return;
    }
    modules[module_id].update (module_id);
}

static void update (size_t module_id) {
    char output_str[] = "4bit\u20042.3%";

    char *icons[] = {
        "\xf3\xb0\xbe\x86", // 󰾆
        "\xf3\xb0\xbe\x85", // 󰾅
        "\xf3\xb0\x93\x85"  // 󰓅
    };
    double usage = get_usage (module_id);
    size_t idx = usage / 101 * sizeof (icons) / sizeof (char *);
    for (size_t i = 0; icons[idx][i]; i++)
        output_str[i] = icons[idx][i];

    if (modules[module_id].state) {
        double power = get_power (module_id);
        snprintf (
            output_str + 4, sizeof (output_str) - 4, "\u2004%3.*fW", power < 10,
            power
        );
    } else {
        snprintf (
            output_str + 4, sizeof (output_str) - 4, "\u2004%3.*f%%",
            usage < 10, usage
        );
    }

    char *colors[] = {IDLE, WARNING, CRITICAL};
    if (usage < 30)
        idx = 0;
    else if (usage < 60)
        idx = 1;
    else
        idx = 2;

    cJSON *json = cJSON_CreateObject ();

    char name[] = "A";
    *name += module_id;

    cJSON_AddStringToObject (json, "name", name);
    cJSON_AddFalseToObject (json, "separator");
    cJSON_AddNumberToObject (json, "separator_block_width", 0);
    cJSON_AddStringToObject (json, "markup", "pango");
    cJSON_AddStringToObject (json, "full_text", output_str);
    cJSON_AddStringToObject (json, "color", colors[idx]);

    if (modules[module_id].output) {
        free (modules[module_id].output);
    }
    modules[module_id].output = cJSON_PrintUnformatted (json);

    cJSON_Delete (json);
}

static void del (size_t module_id) {
    free (modules[module_id].data.ptr);
}

void init_cpu (int epoll_fd) {
    (void) epoll_fd;
    INIT_BASE;

    modules[module_id].alter = alter;
    modules[module_id].update = update;
    modules[module_id].interval = 1;
    modules[module_id].data.ptr = malloc (sizeof (uint64_t) * 3);
    ((uint64_t *) modules[module_id].data.ptr)[0] = 0;
    ((uint64_t *) modules[module_id].data.ptr)[1] = 0;
    ((uint64_t *) modules[module_id].data.ptr)[2] = 0;
    modules[module_id].del = del;

    UPDATE_Q (module_id);
}
