#include <cJSON.h>
#include <module_base.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tools.h>

// 此模块不支持重入，
// 它统计的是所有ether和wlan接口的流量，本身就不应该重入

// 仅仅使用每秒更新，故此函数不会被外部调用
static void get_rate (uint64_t *rx, uint64_t *tx) {
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

    if (!prev_rx)
        prev_rx = *rx;
    if (!prev_tx)
        prev_tx = *tx;

    *rx -= prev_rx;
    *tx -= prev_tx;
    prev_rx += *rx;
    prev_tx += *tx;
}

static void update () {
    if (modules[module_id].output) {
        free (modules[module_id].output);
    }

    cJSON *json = cJSON_CreateObject ();

    char name[] = "A";
    *name += module_id;
    // 添加键值对到JSON对象
    cJSON_AddStringToObject (json, "name", name);
    cJSON_AddStringToObject (json, "color", IDLE);
    cJSON_AddFalseToObject (json, "separator");
    cJSON_AddNumberToObject (json, "separator_block_width", 0);
    cJSON_AddStringToObject (json, "markup", "pango");

    // 󰈀: 4 byte
    // \u2004: 3 byte
    char output_str[] = "󰈀\u20040.00K\u20040.00K";
    uint64_t rx = 0, tx = 0;
    get_rate (&rx, &tx);
    format_storage_units (output_str + 7, tx);
    // format_storage_units 会调用 snprintf，将 speed[12] 置为\0
    // \u2004 = e28084
    output_str[12] = '\xe2';
    format_storage_units (output_str + 15, rx);
    cJSON_AddStringToObject (json, "full_text", output_str);

    // 将JSON对象转换为字符串
    modules[module_id].output = cJSON_PrintUnformatted (json);

    // 删除JSON对象并释放内存
    cJSON_Delete (json);
}

// 仅仅使用每秒更新，不实时监听其他文件
void init_network (int epoll_fd) {
    (void) epoll_fd;
    init_base ();

    modules[module_id].update = update;
    modules[module_id].sec = true;
    update ();
}
