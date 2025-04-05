#include <cJSON.h>
#include <math.h>
#include <module_base.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <tools.h>

#define Tdie "/sys/class/hwmon/hwmon3/temp1_input"

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

    char output_str[] = "ico\u200435.3\ue33e";

    double temp = read_uint64_file (Tdie) / 1e3;

    char *icons[] = {
        "\uf2cb", // 
        "\uf2ca", // 
        "\uf2c9", // 
        "\uf2c8", // 
        "\uf2c7", // 
    };
    size_t idx = temp / 101 * sizeof (icons) / sizeof (char *);
    for (size_t i = 0; icons[idx][i]; i++)
        output_str[i] = icons[idx][i];
    snprintf (
        output_str + 3, sizeof (output_str) - 3, "\u2004%3.*f\ue33e",
        temp < 10 ? 2 : 1, temp
    );
    cJSON_AddStringToObject (json, "full_text", output_str);

    char *colors[] = {CODE, IDLE, WARNING, CRITICAL};
    if (temp < 30)
        idx = 0;
    else if (temp < 60)
        idx = 1;
    else if (temp < 80)
        idx = 2;
    else
        idx = 3;
    cJSON_AddStringToObject (json, "color", colors[idx]);

    modules[module_id].output = cJSON_PrintUnformatted (json);

    cJSON_Delete (json);
}

void init_temp (int epoll_fd) {
    (void) epoll_fd;
    INIT_BASE ();

    modules[module_id].update = update;
    modules[module_id].interval = 1;

    UPDATE_Q ();
}
