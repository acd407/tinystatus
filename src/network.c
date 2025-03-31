#include <cjson/cJSON.h>
#include <main.h>
#include <network.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utils.h>

static struct {
    unsigned long prev_rx;
    unsigned long prev_tx;
    unsigned long rx;
    unsigned long tx;
} status;

static size_t module_id;

// 仅仅使用每秒更新，故此函数不会被外部调用
static void read_network_bytes () {
    FILE *fp;
    char buffer[BUF_SIZE];
    int found = 0;

    status.rx = 0;
    status.tx = 0;
    fp = fopen ("/proc/net/dev", "r");
    if (!fp) {
        perror ("Failed to open /proc/net/dev");
        exit (-1);
    }

    // 跳过前两行
    fgets (buffer, sizeof (buffer), fp);
    fgets (buffer, sizeof (buffer), fp);

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

        unsigned long rx, tx;
        if (sscanf (line, "%lu %*u %*u %*u %*u %*u %*u %*u %lu", &rx, &tx) ==
            2) {
            status.rx += rx;
            status.tx += tx;
            found = 1;
        }
    }

    if (!found) {
        perror ("can't find any wlan or ether");
        exit (-1);
    }
    fclose (fp);
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
    cJSON_AddFalseToObject (json, "separator"); // 添加布尔值 false
    cJSON_AddNumberToObject (json, "separator_block_width", 0); // 添加数字 0
    cJSON_AddStringToObject (json, "markup", "pango");

    // 󰈀: 4 byte
    // \u2004: 3 byte
    char speed[] = "󰈀\u20040.00K 0.00K";
    read_network_bytes ();
    format_storage_units (speed + 7, status.rx - status.prev_rx);
    // format_storage_units 会调用 snprintf，将 speed[12] 置为\0
    speed[12] = ' ';
    format_storage_units (speed + 13, status.tx - status.prev_tx);
    cJSON_AddStringToObject (json, "full_text", speed);

    // 将JSON对象转换为字符串
    modules[module_id].output = cJSON_PrintUnformatted (json);

    // 删除JSON对象并释放内存
    cJSON_Delete (json);

    status.prev_rx = status.rx;
    status.prev_tx = status.tx;
}

// 仅仅使用每秒更新，不实时监听其他文件
void network_init (int epoll_fd) {
    (void) epoll_fd;
    status.prev_rx = 0;
    status.prev_tx = 0;
    status.rx = 0;
    status.tx = 0;
    module_id = modules_cnt++;
    modules[module_id].output = NULL;
    modules[module_id].update = network_update;
    modules[module_id].sec = true;
    modules[module_id].fds = NULL;
}

// #define NETWORK
#ifdef NETWORK
#include <unistd.h>
int main () {
    network_init ();
    network_update ();
    while (1) {
        network_update ();
        puts (network_output);
        sleep (1);
    }
}
#endif
