#include <ctype.h>
#include <module_base.h>
#include <net/if.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tools.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <netlink/msg.h>
#include <netlink/attr.h>
#include <linux/nl80211.h>
#include <netlink/socket.h>

// 定义用于传递信息的结构体
struct station_info {
    int signal_dbm;
    uint32_t tx_bitrate;
};

// 回调函数：处理 NL80211_CMD_NEW_STATION 响应
static int parse_station_info(struct nl_msg *msg, void *arg) {
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
    struct station_info *info = (struct station_info *)arg;

    // 解析顶层属性
    nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL);

    if (tb[NL80211_ATTR_STA_INFO]) {
        struct nlattr *sinfo[NL80211_STA_INFO_MAX + 1];
        nla_parse_nested(sinfo, NL80211_STA_INFO_MAX, tb[NL80211_ATTR_STA_INFO], NULL);

        if (sinfo[NL80211_STA_INFO_SIGNAL]) {
            info->signal_dbm = (int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL]);
        }

        if (sinfo[NL80211_STA_INFO_TX_BITRATE]) {
            struct nlattr *rinfo[NL80211_RATE_INFO_MAX + 1];
            nla_parse_nested(rinfo, NL80211_RATE_INFO_MAX, sinfo[NL80211_STA_INFO_TX_BITRATE], NULL);

            if (rinfo[NL80211_RATE_INFO_BITRATE32]) {
                info->tx_bitrate = nla_get_u32(rinfo[NL80211_RATE_INFO_BITRATE32]);
            } else if (rinfo[NL80211_RATE_INFO_BITRATE]) {
                info->tx_bitrate = nla_get_u16(rinfo[NL80211_RATE_INFO_BITRATE]);
            }
        }
        return NL_SKIP; // 已获取，不再继续
    }

    return NL_SKIP;
}

static void get_wireless_status(char *ifname, int64_t *level, uint32_t *tx_bitrate) {
    struct nl_sock *sock;
    int driver_id;
    int ifindex;
    struct station_info info = {0, 0};
    int err;

    // 创建 netlink socket
    sock = nl_socket_alloc();
    if (!sock) {
        perror("nl_socket_alloc failed");
        exit(EXIT_FAILURE);
    }

    // 连接到 generic netlink
    if (genl_connect(sock)) {
        fprintf(stderr, "genl_connect failed\n");
        nl_socket_free(sock);
        exit(EXIT_FAILURE);
    }

    // 获取 nl80211 的 family ID
    driver_id = genl_ctrl_resolve(sock, "nl80211");
    if (driver_id < 0) {
        fprintf(stderr, "nl80211 not found\n");
        nl_socket_free(sock);
        exit(EXIT_FAILURE);
    }

    // 获取接口索引
    ifindex = if_nametoindex(ifname);
    if (!ifindex) {
        perror("if_nametoindex");
        nl_socket_free(sock);
        exit(EXIT_FAILURE);
    }

    // 设置回调函数以解析响应
    nl_socket_modify_cb(sock, NL_CB_VALID, NL_CB_CUSTOM, parse_station_info, &info);

    // 构造请求消息：NL80211_CMD_GET_STATION
    struct nl_msg *msg = nlmsg_alloc();
    if (!msg) {
        fprintf(stderr, "nlmsg_alloc failed\n");
        nl_socket_free(sock);
        exit(EXIT_FAILURE);
    }

    genlmsg_put(msg, 0, 0, driver_id, 0, NLM_F_DUMP, NL80211_CMD_GET_STATION, 0);

    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex);

    // 发送请求并接收响应
    err = nl_send_auto_complete(sock, msg);
    if (err < 0) {
        fprintf(stderr, "nl_send_auto_complete failed: %s\n", nl_geterror(err));
        nlmsg_free(msg);
        nl_socket_free(sock);
        exit(EXIT_FAILURE);
    }

    err = nl_recvmsgs_default(sock);
    if (err < 0) {
        fprintf(stderr, "nl_recvmsgs_default failed: %s\n", nl_geterror(err));
        nlmsg_free(msg);
        nl_socket_free(sock);
        exit(EXIT_FAILURE);
    }

    nlmsg_free(msg);
    nl_socket_free(sock);

    // 如果成功获取信号强度，计算链路质量
    if (info.signal_dbm != 0) {
        *level = info.signal_dbm;
    } else {
        *level = -100;
    }

    // 设置传输速率（单位：100 kbit/s）
    *tx_bitrate = info.tx_bitrate;
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
    int64_t level = 0;
    uint32_t tx_bitrate = 0;
    get_wireless_status(ifname, &level, &tx_bitrate);

    const char *icons[] = {"󰤮", "󰤯", "󰤟", "󰤢", "󰤥", "󰤨"};
    size_t icon_idx = 0;
    icon_idx += level > -100;
    icon_idx += level > -90;
    icon_idx += level > -80;
    icon_idx += level > -65;
    icon_idx += level > -55;

    if (modules[module_id].state) {
        if (tx_bitrate > 0) {
            // tx_bitrate单位是100 kbit/s，转换为bit/s
            uint64_t bitrate_bps = (uint64_t)tx_bitrate * 100 * 1000;
            char rate_str[6];
            format_storage_units(&rate_str, bitrate_bps);
            snprintf(buffer, buffer_size, "%s\u2004%s\u2004%ldDB", icons[icon_idx], rate_str, level);
        } else {
            // 如果无法获取速率，只显示信号强度
            snprintf(buffer, buffer_size, "%s\u2004%ldDB", icons[icon_idx], level);
        }
    } else {
        char rxs[6], txs[6];
        format_storage_units(&rxs, rx);
        format_storage_units(&txs, tx);
        snprintf(buffer, buffer_size, "%s\u2004%s\u2004%s", icons[icon_idx], rxs, txs);
    }
}

static void update(size_t module_id) {
    char output_str[] = "🖧\u20040.00K\u20040.00K    ";

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
