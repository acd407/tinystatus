#include <cJSON.h>
#include <errno.h>
#include <main.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <tools.h>
#include <string.h>

void format_storage_units (char (*buf)[6], double bytes) {
    const char *units = "KMGTPE"; // 单位从 K 开始
    int unit_idx = -1;            // 0=K, 1=M, 2=G, 3=T, 4=P, 5=E

    // 确保最小单位为 K
    while ((bytes >= 1000.0 && unit_idx < 5) || unit_idx < 0) {
        bytes /= 1024;
        unit_idx++;
    }

    // 动态选择格式
    if (bytes >= 100.0) {
        snprintf (*buf, 6, " %3.0f%c", round (bytes), units[unit_idx]);
    } else if (bytes >= 10.0) {
        snprintf (*buf, 6, "%4.1f%c", bytes, units[unit_idx]);
    } else {
        snprintf (*buf, 6, "%4.2f%c", bytes, units[unit_idx]);
    }
}

uint64_t read_uint64_file (char *file) {
    FILE *file_soc = fopen (file, "r");
    if (!file_soc) {
        fprintf (stderr, "fopen: %s: %s", file, strerror (errno));
        // perror ("fopen");
        exit (EXIT_FAILURE);
    }
    uint64_t ans = 0;
    if (EOF == fscanf (file_soc, "%lu", &ans)) {
        perror ("fscanf");
        // exit (EXIT_FAILURE);
    }
    fclose (file_soc);
    return ans;
}

void update_json (size_t module_id, const char *output_str, const char *color) {
    cJSON *json = cJSON_CreateObject ();

    char name[] = "A";
    *name += module_id;

    cJSON_AddStringToObject (json, "name", name);
    cJSON_AddFalseToObject (json, "separator");
    cJSON_AddNumberToObject (json, "separator_block_width", 0);
    cJSON_AddStringToObject (json, "markup", "pango");
    cJSON_AddStringToObject (json, "full_text", output_str);
    cJSON_AddStringToObject (json, "color", color);

    if (modules[module_id].output) {
        free (modules[module_id].output);
    }
    modules[module_id].output = cJSON_PrintUnformatted (json);

    cJSON_Delete (json);
}
