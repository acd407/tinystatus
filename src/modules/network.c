#include <cJSON.h>
#include <ctype.h>
#include <module_base.h>
#include <net/if.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tools.h>

static void get_wireless_status(char *ifname, int64_t *link, int64_t *level) {
    FILE *fp;
    char buffer[BUF_SIZE];

    fp = fopen("/proc/net/wireless", "r");
    if (!fp) {
        perror("Failed to open /proc/net/wireless");
        exit(EXIT_FAILURE);
    }

    // 跳过前两行
    fgets(buffer, sizeof(buffer), fp);
    fgets(buffer, sizeof(buffer), fp);

    while (fgets(buffer, sizeof(buffer), fp)) {
        char *line = buffer;
        while (*line == ' ')
            line++; // 跳过前导空格

        if (strncmp(ifname, line, strlen(ifname)) != 0)
            continue;

        line = strchr(line, ':');
        if (!line)
            continue;
        line++;

        if (sscanf(line, "%*d %ld. %ld. %*d", link, level) == 2)
            break;
    }

    *link = *link * 10 / 7; // rtw88 驱动程序链路质量最大值是 70，不是 100
    fclose(fp);
}

static void get_network_speed_and_master_dev(size_t module_id, uint64_t *rx, uint64_t *tx, char *master) {
    uint64_t *prev_rx = &((uint64_t *)modules[module_id].data)[0];
    uint64_t *prev_tx = &((uint64_t *)modules[module_id].data)[1];

    FILE *fp;
    char buffer[BUF_SIZE];

    fp = fopen("/proc/net/dev", "r");
    if (!fp) {
        perror("Failed to open /proc/net/dev");
        exit(EXIT_FAILURE);
    }

    // 跳过前两行
    fgets(buffer, sizeof(buffer), fp);
    fgets(buffer, sizeof(buffer), fp);

    bool found = false;

    while (fgets(buffer, sizeof(buffer), fp)) {
        char *line = buffer, *name_end = strchr(line, ':');

        if (!name_end)
            continue;

        while (*line == ' ')
            line++; // 跳过前导空格

        // 只接受 wlan 和 ether
        if (*line != 'w' && *line != 'e') {
            continue;
        }

        // 检查一下网络接口是否 UP
        // name 由冒号分隔，无法直接提取，故使用 strcat 拼接 而不是 snprintf
        const char carrier_path_template_1[] = "/sys/class/net/";
        const char carrier_path_template_2[] = "/carrier";
        char carrier_path[sizeof(carrier_path_template_1) + sizeof(carrier_path_template_2) - 2 + IFNAMSIZ] = {[0] = 0};
        assert(strcat(carrier_path, carrier_path_template_1));
        assert(strncat(carrier_path, line, name_end - line));
        assert(strcat(carrier_path, carrier_path_template_2));
        if (!read_uint64_file(carrier_path))
            continue;

        // 保存接口名称
        // 如果之前没找到过：!found，那就一定要初始化一下
        // 如果已经初始化过，那么保存以太网的名称
        if (!found || *line == 'e') {
            size_t cnt = 0;
            while (isalnum(*line)) {
                master[cnt++] = *line++;
            }
            master[cnt] = '\0';
        }

        uint64_t prx, ptx;
        if (sscanf(name_end + 1, "%lu %*u %*u %*u %*u %*u %*u %*u %lu", &prx, &ptx) == 2) {
            *rx += prx;
            *tx += ptx;
            found = true;
        }
    }
    fclose(fp);

    if (!found) {
        perror("can't find any wlan or ether");
        // exit (EXIT_FAILURE);
    }

    if (!*prev_rx)
        *prev_rx = *rx;
    if (!*prev_tx)
        *prev_tx = *tx;

    *rx -= *prev_rx;
    *tx -= *prev_tx;
    *prev_rx += *rx;
    *prev_tx += *tx;
}

static void
format_ether_output(size_t module_id, char *buffer, size_t buffer_size, char *ifname, uint64_t rx, uint64_t tx) {
    if (modules[module_id].state) {
        const char speed_path_template[] = "/sys/class/net/%s/speed";
        char speed_path[sizeof(speed_path_template) - 2 - 1 + IFNAMSIZ];
        snprintf(speed_path, sizeof(speed_path), speed_path_template, ifname);
        uint64_t speed = read_uint64_file(speed_path);
        snprintf(buffer, buffer_size, "󰈀\u2004%ldM", speed);
    } else {
        char rxs[6], txs[6];
        format_storage_units(&rxs, rx);
        format_storage_units(&txs, tx);
        snprintf(buffer, buffer_size, "󰈀\u2004%s\u2004%s", rxs, txs);
    }
}

static void
format_wireless_output(size_t module_id, char *buffer, size_t buffer_size, char *ifname, uint64_t rx, uint64_t tx) {
    int64_t link = 0, level = 0;
    get_wireless_status(ifname, &link, &level);

    const char *icons[] = {"󰤮", "󰤯", "󰤟", "󰤢", "󰤥", "󰤨"};
    size_t icon_idx = 0;
    icon_idx += level > -100;
    icon_idx += level > -90;
    icon_idx += level > -80;
    icon_idx += level > -65;
    icon_idx += level > -55;

    // 采用 level 方法判断出问题的时候，就用 link 方法
    if (icon_idx == 0 || icon_idx >= ARRAY_SIZE(icons)) {
        icon_idx = ARRAY_SIZE(icons) * link / 101;
    }

    if (modules[module_id].state) {
        snprintf(buffer, buffer_size, "%s\u2004%ld%%\u2004%ldDB", icons[icon_idx], link, level);
    } else {
        char rxs[6], txs[6];
        format_storage_units(&rxs, rx);
        format_storage_units(&txs, tx);
        snprintf(buffer, buffer_size, "%s\u2004%s\u2004%s", icons[icon_idx], rxs, txs);
    }
}

static void update(size_t module_id) {
    char output_str[] = "🖧\u20040.00K\u20040.00K  ";

    uint64_t rx = 0, tx = 0;
    char master_ifname[IFNAMSIZ] = {[0] = 0};
    get_network_speed_and_master_dev(module_id, &rx, &tx, master_ifname);

    if (master_ifname[0] == 'e')
        format_ether_output(module_id, output_str, sizeof(output_str), master_ifname, rx, tx);
    else if (master_ifname[0] == 'w')
        format_wireless_output(module_id, output_str, sizeof(output_str), master_ifname, rx, tx);
    else
        snprintf(output_str, sizeof(output_str), "\xf3\xb1\x9e\x90"); // 󱞐

    update_json(module_id, output_str, IDLE);
}

static void alter(size_t module_id, uint64_t btn) {
    switch (btn) {
    case 2: // middle button
        system("iwgtk &");
        break;
    case 3: // right button
        modules[module_id].state ^= 1;
        modules[module_id].update(module_id);
        break;
    }
}

static void del(size_t module_id) {
    free(modules[module_id].data);
}

void init_network(int epoll_fd) {
    (void)epoll_fd;
    INIT_BASE;

    modules[module_id].update = update;
    modules[module_id].alter = alter;
    modules[module_id].interval = 1;
    modules[module_id].data = malloc(sizeof(uint64_t) * 2);
    if (!modules[module_id].data) {
        perror("malloc");
        modules_cnt--;
        return;
    }
    ((uint64_t *)modules[module_id].data)[0] = 0; // prev_rx
    ((uint64_t *)modules[module_id].data)[1] = 0; // prev_tx
    modules[module_id].del = del;

    UPDATE_Q(module_id);
}