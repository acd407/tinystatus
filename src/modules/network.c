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

// 模块私有数据
struct network_data {
    uint64_t prev_rx;
    uint64_t prev_tx;
    struct nl_sock *sock;
    int driver_id; // nl80211 family id，-1 表示无效
    int ifindex;   // 当前监听的接口索引，0 表示未初始化
    struct station_info info;
};

// 回调函数：处理 NL80211_CMD_NEW_STATION 响应
static int parse_station_info(struct nl_msg *msg, void *arg) {
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
    struct station_info *info = arg;

    nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL);

    if (tb[NL80211_ATTR_STA_INFO]) {
        struct nlattr *sinfo[NL80211_STA_INFO_MAX + 1];
        nla_parse_nested(sinfo, NL80211_STA_INFO_MAX, tb[NL80211_ATTR_STA_INFO], NULL);

        if (sinfo[NL80211_STA_INFO_SIGNAL])
            info->signal_dbm = (int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL]);

        if (sinfo[NL80211_STA_INFO_TX_BITRATE]) {
            struct nlattr *rinfo[NL80211_RATE_INFO_MAX + 1];
            nla_parse_nested(rinfo, NL80211_RATE_INFO_MAX, sinfo[NL80211_STA_INFO_TX_BITRATE], NULL);
            if (rinfo[NL80211_RATE_INFO_BITRATE32])
                info->tx_bitrate = nla_get_u32(rinfo[NL80211_RATE_INFO_BITRATE32]);
            else if (rinfo[NL80211_RATE_INFO_BITRATE])
                info->tx_bitrate = nla_get_u16(rinfo[NL80211_RATE_INFO_BITRATE]);
        }
        return NL_SKIP;
    }

    return NL_SKIP;
}

// 复用 socket，失败时返回 -1 并标记需重建
static int refresh_wireless_status(struct network_data *data, int ifindex) {
    if (ifindex == 0)
        return -1;

    if (data->sock == NULL || data->driver_id < 0 || data->ifindex != ifindex) {
        // 需要重建 socket
        if (data->sock)
            nl_socket_free(data->sock);

        data->sock = nl_socket_alloc();
        if (!data->sock)
            return -1;

        if (genl_connect(data->sock) < 0)
            goto fail;

        data->driver_id = genl_ctrl_resolve(data->sock, "nl80211");
        if (data->driver_id < 0)
            goto fail;

        data->ifindex = ifindex;
    }

    // 重置 info，准备接收新数据
    memset(&data->info, 0, sizeof(data->info));
    nl_socket_modify_cb(data->sock, NL_CB_VALID, NL_CB_CUSTOM, parse_station_info, &data->info);

    struct nl_msg *msg = nlmsg_alloc();
    if (!msg)
        return -1;

    genlmsg_put(msg, 0, 0, data->driver_id, 0, NLM_F_DUMP, NL80211_CMD_GET_STATION, 0);
    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex);

    int err = nl_send_auto_complete(data->sock, msg);
    nlmsg_free(msg);
    if (err < 0)
        return -1;

    err = nl_recvmsgs_default(data->sock);
    if (err < 0)
        return -1;

    return 0;

fail:
    nl_socket_free(data->sock);
    data->sock = NULL;
    data->driver_id = -1;
    return -1;
}

static void get_network_speed_and_master_dev(size_t module_id, uint64_t *rx, uint64_t *tx, char *master) {
    struct network_data *data = modules[module_id].data;

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
            line++;

        if (*line != 'w' && *line != 'e')
            continue;

        // 检查网络接口是否 UP
        char carrier_path[64];
        snprintf(carrier_path, sizeof(carrier_path), "/sys/class/net/%.*s/carrier", (int)(name_end - line), line);
        if (!read_uint64_file(carrier_path))
            continue;

        // 保存接口名称
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
    }

    if (!data->prev_rx)
        data->prev_rx = *rx;
    if (!data->prev_tx)
        data->prev_tx = *tx;

    *rx -= data->prev_rx;
    *tx -= data->prev_tx;
    data->prev_rx += *rx;
    data->prev_tx += *tx;
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
    struct network_data *data = modules[module_id].data;
    int ifindex = if_nametoindex(ifname);
    int64_t level = -100;
    uint32_t tx_bitrate = 0;

    if (ifindex > 0 && refresh_wireless_status(data, ifindex) == 0) {
        level = data->info.signal_dbm ? data->info.signal_dbm : -100;
        tx_bitrate = data->info.tx_bitrate;
    }

    const char *icons[] = {"󰤮", "󰤯", "󰤟", "󰤢", "󰤥", "󰤨"};
    size_t icon_idx = 0;
    icon_idx += level > -100;
    icon_idx += level > -90;
    icon_idx += level > -80;
    icon_idx += level > -65;
    icon_idx += level > -55;

    if (modules[module_id].state) {
        if (tx_bitrate > 0) {
            uint64_t bitrate_bps = (uint64_t)tx_bitrate * 100 * 1000;
            char rate_str[6];
            format_storage_units(&rate_str, bitrate_bps);
            snprintf(buffer, buffer_size, "%s\u2004%s\u2004%ldDB", icons[icon_idx], rate_str, level);
        } else {
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

    update_json(module_id, output_str);
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
    struct network_data *data = modules[module_id].data;
    if (data->sock)
        nl_socket_free(data->sock);
    free(data);
}

void init_network(int epoll_fd) {
    (void)epoll_fd;
    INIT_BASE;

    struct network_data *data = calloc(1, sizeof(struct network_data));
    if (!data) {
        perror("malloc");
        modules_cnt--;
        return;
    }

    modules[module_id].data = data;
    modules[module_id].update = update;
    modules[module_id].alter = alter;
    modules[module_id].del = del;
    modules[module_id].interval = 1;

    UPDATE_Q(module_id);
}
