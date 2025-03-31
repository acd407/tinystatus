#include <cJSON.h>
#include <main.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tools.h>

// 此模块不支持重入，
// 它统计的是所有ether和wlan接口的流量，本身就不应该重入

static size_t module_id;

// 仅仅使用每秒更新，故此函数不会被外部调用
static void get_network_rate (uint64_t *rx, uint64_t *tx) {
    static uint64_t prev_rx = 0, prev_tx = 0;

    FILE *fp;
    char buffer[BUF_SIZE];

    fp = fopen ("/proc/net/dev", "r");
    if (!fp) {
        perror ("Failed to open /proc/net/dev");
        exit (EXIT_FAILURE);
    }

    // 跳过前两行
    fgets (buffer, sizeof (buffer), fp);
    fgets (buffer, sizeof (buffer), fp);

    bool found = false;

    while (fgets (buffer, sizeof (buffer), fp)) {
        char *line = buffer;
        while (*line == ' ')
            line++; // 跳过前导空格

        // 只接受 wlan 和 ether
        if (*line != 'w' && *line != 'e') {
            continue;
        }

        line = strchr (line, ':');
        if (!line)
            continue;
        line++;

        uint64_t prx, ptx;
        if (sscanf (line, "%lu %*u %*u %*u %*u %*u %*u %*u %lu", &prx, &ptx) ==
            2) {
            *rx += prx;
            *tx += ptx;
            found = true;
        }
    }
    fclose (fp);

    if (!found) {
        perror ("can't find any wlan or ether");
        exit (EXIT_FAILURE);
    }

    *rx -= prev_rx;
    *tx -= prev_tx;
    prev_rx += *rx;
    prev_tx += *tx;
}

static void network_update () {
    if (modules[module_id].output) {
        free (modules[module_id].output);
    }

    cJSON *json = cJSON_CreateObject ();

    // MOD_SZIE只有16，故name只有2位，在此，随便给几位
    char name_buffer[4 + 1];
    snprintf (name_buffer, sizeof (name_buffer), "%ld", module_id);
    // 添加键值对到JSON对象
    cJSON_AddStringToObject (json, "color", "#FCE8C3");
    cJSON_AddStringToObject (json, "name", name_buffer);
    cJSON_AddFalseToObject (json, "separator");
    cJSON_AddNumberToObject (json, "separator_block_width", 0);
    cJSON_AddStringToObject (json, "markup", "pango");

    // 󰈀: 4 byte
    // \u2004: 3 byte
    char output_str[] = "󰈀\u20040.00K 0.00K";
    uint64_t rx, tx;
    get_network_rate (&rx, &tx);
    format_storage_units (output_str + 7, tx);
    // format_storage_units 会调用 snprintf，将 speed[12] 置为\0
    output_str[12] = ' ';
    format_storage_units (output_str + 13, rx);
    cJSON_AddStringToObject (json, "full_text", output_str);

    // 将JSON对象转换为字符串
    modules[module_id].output = cJSON_PrintUnformatted (json);

    // 删除JSON对象并释放内存
    cJSON_Delete (json);
}

// 仅仅使用每秒更新，不实时监听其他文件
void network_init (int epoll_fd) {
    (void) epoll_fd;
    module_id = modules_cnt++;
    modules[module_id].output = NULL;
    modules[module_id].update = network_update;
    modules[module_id].sec = true;
    modules[module_id].fds = NULL;
    network_update ();
}
