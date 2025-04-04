#include <main.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <tools.h>

void format_storage_units (char *buf, double bytes) {
    const char *units = "KMGTPE"; // 单位从 K 开始
    int unit_idx = -1;            // 0=K, 1=M, 2=G, 3=T, 4=P, 5=E

    // 确保最小单位为 K
    while ((bytes >= 1000.0 && unit_idx < 5) || unit_idx < 0) {
        bytes /= 1024;
        unit_idx++;
    }

    // 动态选择格式
    if (bytes >= 100.0) {
        snprintf (buf, 6, " %3.0f%c", round (bytes), units[unit_idx]);
    } else if (bytes >= 10.0) {
        snprintf (buf, 6, "%4.1f%c", bytes, units[unit_idx]);
    } else {
        snprintf (buf, 6, "%4.2f%c", bytes, units[unit_idx]);
    }
}

uint64_t read_uint64_file (char *file) {
    FILE *file_soc = fopen (file, "r");
    if (!file_soc) {
        perror ("fopen");
        exit (EXIT_FAILURE);
    }
    uint64_t ans = 0;
    if (EOF == fscanf (file_soc, "%lu", &ans)) {
        perror ("fscanf");
        exit (EXIT_FAILURE);
    }
    fclose (file_soc);
    return ans;
}
