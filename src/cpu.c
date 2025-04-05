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
#define GET_POWER get_power_zenpower

static double get_usage () {
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

__attribute__ ((unused)) static double get_power_rapl () {
    static uint64_t previous_energy = 0;

    FILE *file = fopen (PACKAGE, "r");
    if (!file) {
        perror ("cpu_power: fopen");
        exit (EXIT_FAILURE);
    }
    uint64_t energy;
    if (EOF == fscanf (file, "%lu", &energy)) {
        perror ("cpu_power: fscanf");
        exit (EXIT_FAILURE);
    }
    fclose (file);

    if (!previous_energy)
        previous_energy = energy;
    uint64_t energy_diff = energy - previous_energy;
    double power = (double) energy_diff / 1e6;

    previous_energy = energy;
    return power;
}

__attribute__ ((unused)) static double get_power_zenpower () {
    FILE *file_core = fopen (SVI2_P_Core, "r");
    if (!file_core) {
        perror ("cpu_power: fopen");
        exit (EXIT_FAILURE);
    }
    uint64_t uwatt_core = 0;
    if (EOF == fscanf (file_core, "%lu", &uwatt_core)) {
        perror ("cpu_power: fscanf");
        exit (EXIT_FAILURE);
    }
    fclose (file_core);

    FILE *file_soc = fopen (SVI2_P_Core, "r");
    if (!file_soc) {
        perror ("cpu_power: fopen");
        exit (EXIT_FAILURE);
    }
    uint64_t uwatt_soc = 0;
    if (EOF == fscanf (file_soc, "%lu", &uwatt_soc)) {
        perror ("cpu_power: fscanf");
        exit (EXIT_FAILURE);
    }
    fclose (file_soc);

    return (uwatt_core + uwatt_soc) / 1e6;
}

static void alter (uint64_t btn) {
    switch (btn) {
    case 3: // right button
        modules[module_id].state ^= 1;
        break;
    default:
        return;
    }
    modules[module_id].update ();
}

static void update () {
    if (modules[module_id].output) {
        free (modules[module_id].output);
    }
    cJSON *json = cJSON_CreateObject ();

    char name[] = "A";
    *name += module_id;

    cJSON_AddStringToObject (json, "name", name);
    cJSON_AddFalseToObject (json, "separator");
    cJSON_AddNumberToObject (json, "separator_block_width", 0);
    cJSON_AddStringToObject (json, "markup", "pango");

    char output_str[] = "4bit\u20042.3%";

    char *icons[] = {
        "\xf3\xb0\xbe\x86", // 󰾆
        "\xf3\xb0\xbe\x85", // 󰾅
        "\xf3\xb0\x93\x85"  // 󰓅
    };
    double usage = get_usage ();
    size_t idx = usage / 101 * sizeof (icons) / sizeof (char *);
    for (size_t i = 0; icons[idx][i]; i++)
        output_str[i] = icons[idx][i];

    if (modules[module_id].state) {
        double power = GET_POWER ();
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
    cJSON_AddStringToObject (json, "full_text", output_str);

    char *colors[] = {IDLE, WARNING, CRITICAL};
    if (usage < 30)
        idx = 0;
    else if (usage < 60)
        idx = 1;
    else
        idx = 2;
    cJSON_AddStringToObject (json, "color", colors[idx]);

    modules[module_id].output = cJSON_PrintUnformatted (json);

    cJSON_Delete (json);
}

void init_cpu (int epoll_fd) {
    (void) epoll_fd;
    INIT_BASE ();

    modules[module_id].alter = alter;
    modules[module_id].update = update;
    modules[module_id].interval = 1; // 等 init_timer 调用 cpu 的 update

    UPDATE_Q ();
}
