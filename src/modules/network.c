#include <module_base.h>
#include <net/if.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tools.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/epoll.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <netlink/msg.h>
#include <netlink/attr.h>
#include <netlink/netlink.h>
#include <netlink/route/route.h>
#include <netlink/route/link.h>
#include <linux/nl80211.h>
#define NETLINK_ETHTOOL 31
#include <linux/ethtool_netlink.h>

struct station_info {
    int signal_dbm;
    uint32_t tx_bitrate;
};

struct network_data {
    uint64_t prev_rx;
    uint64_t prev_tx;
    struct nl_sock *nlsock;    // nl80211 for wireless signal
    struct nl_sock *eth_sock;  // ethtool netlink for link speed
    struct nl_sock *rtnl_sock; // NETLINK_ROUTE monitor (events only)
    struct nl_sock *stat_sock; // NETLINK_ROUTE for traffic + route discovery
    int driver_id;             // nl80211 family id
    int ifindex;               // cached default route ifindex, 0 = none
    char ifname[IFNAMSIZ];     // cached interface name
    // 回调暂存区
    struct station_info buf_station;
    uint32_t buf_link_speed;
};

static int station_info_cb(struct nl_msg *msg, void *arg) {
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
    struct network_data *data = arg;

    nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL);

    if (tb[NL80211_ATTR_STA_INFO]) {
        struct nlattr *sinfo[NL80211_STA_INFO_MAX + 1];
        nla_parse_nested(sinfo, NL80211_STA_INFO_MAX, tb[NL80211_ATTR_STA_INFO], NULL);

        if (sinfo[NL80211_STA_INFO_SIGNAL])
            data->buf_station.signal_dbm = (int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL]);

        if (sinfo[NL80211_STA_INFO_TX_BITRATE]) {
            struct nlattr *rinfo[NL80211_RATE_INFO_MAX + 1];
            nla_parse_nested(rinfo, NL80211_RATE_INFO_MAX, sinfo[NL80211_STA_INFO_TX_BITRATE], NULL);
            if (rinfo[NL80211_RATE_INFO_BITRATE32])
                data->buf_station.tx_bitrate = nla_get_u32(rinfo[NL80211_RATE_INFO_BITRATE32]);
            else if (rinfo[NL80211_RATE_INFO_BITRATE])
                data->buf_station.tx_bitrate = nla_get_u16(rinfo[NL80211_RATE_INFO_BITRATE]);
        }
    }

    return NL_SKIP;
}

static int refresh_wireless_status(struct network_data *data) {
    if (data->ifindex == 0)
        return -1;

    if (!data->nlsock || data->driver_id < 0) {
        if (data->nlsock) {
            nl_socket_free(data->nlsock);
            data->nlsock = NULL;
        }

        data->nlsock = nl_socket_alloc();
        if (!data->nlsock)
            return -1;

        if (genl_connect(data->nlsock) < 0)
            goto fail;

        data->driver_id = genl_ctrl_resolve(data->nlsock, "nl80211");
        if (data->driver_id < 0)
            goto fail;

        nl_socket_modify_cb(data->nlsock, NL_CB_VALID, NL_CB_CUSTOM, station_info_cb, data);
    }

    memset(&data->buf_station, 0, sizeof(data->buf_station));

    struct nl_msg *msg = nlmsg_alloc();
    if (!msg)
        return -1;

    genlmsg_put(msg, 0, 0, data->driver_id, 0, NLM_F_DUMP, NL80211_CMD_GET_STATION, 0);
    nla_put_u32(msg, NL80211_ATTR_IFINDEX, data->ifindex);

    int err = nl_send_auto_complete(data->nlsock, msg);
    nlmsg_free(msg);
    if (err < 0)
        return -1;

    err = nl_recvmsgs_default(data->nlsock);
    if (err < 0)
        return -1;

    return 0;

fail:
    nl_socket_free(data->nlsock);
    data->nlsock = NULL;
    data->driver_id = -1;
    return -1;
}

// 扫描默认路由，写入 data->ifindex / data->ifname。仅在路由变更时调用。
static int discover_default_route(struct network_data *data) {
    struct nl_cache *route_cache = NULL;
    if (rtnl_route_alloc_cache(data->stat_sock, AF_UNSPEC, 0, &route_cache) < 0)
        return -1;

    int ret = -1;
    struct nl_object *obj;
    for (obj = nl_cache_get_first(route_cache); obj; obj = nl_cache_get_next(obj)) {
        struct rtnl_route *route = (struct rtnl_route *)obj;

        if (rtnl_route_get_table(route) != RT_TABLE_MAIN)
            continue;

        struct nl_addr *dst = rtnl_route_get_dst(route);
        if (dst && nl_addr_get_prefixlen(dst) > 0)
            continue;

        if (rtnl_route_get_nnexthops(route) == 0)
            continue;

        struct rtnl_nexthop *nh = rtnl_route_nexthop_n(route, 0);
        int idx = rtnl_route_nh_get_ifindex(nh);
        if (idx <= 0)
            continue;

        if (!if_indextoname(idx, data->ifname))
            continue;

        data->ifindex = idx;
        ret = 0;
        break;
    }

    if (ret < 0)
        data->ifindex = 0;

    nl_cache_free(route_cache);
    return ret;
}

// 非阻塞读取路由/链路事件，有相关变更时置 changed=true
static void drain_route_events(struct nl_sock *sock, bool *changed) {
    *changed = false;
    if (!sock)
        return;
    while (true) {
        struct sockaddr_nl nla;
        unsigned char *buf = NULL;
        int len = nl_recv(sock, &nla, &buf, NULL);

        if (len == -NLE_AGAIN)
            break;
        if (len < 0 || !buf) {
            free(buf);
            break;
        }

        struct nlmsghdr *hdr = (struct nlmsghdr *)buf;
        while (NLMSG_OK(hdr, len)) {
            unsigned int t = hdr->nlmsg_type;
            if ((t >= RTM_NEWLINK && t <= RTM_DELLINK) || (t >= RTM_NEWROUTE && t <= RTM_DELROUTE))
                *changed = true;
            hdr = NLMSG_NEXT(hdr, len);
        }
        free(buf);
    }
}

static int link_speed_cb(struct nl_msg *msg, void *arg) {
    struct network_data *data = arg;
    struct nlmsghdr *hdr = nlmsg_hdr(msg);
    struct nlattr *tb[ETHTOOL_A_LINKMODES_MAX + 1];

    if (nla_parse(tb, ETHTOOL_A_LINKMODES_MAX, nlmsg_attrdata(hdr, 0), nlmsg_attrlen(hdr, 0), NULL) == 0 &&
        tb[ETHTOOL_A_LINKMODES_SPEED])
        data->buf_link_speed = nla_get_u32(tb[ETHTOOL_A_LINKMODES_SPEED]);

    return NL_SKIP;
}

// 通过 ethtool netlink 查询链接速率（Mbps），失败返回 0
static uint64_t get_link_speed(struct network_data *data) {
    if (!data->eth_sock) {
        data->eth_sock = nl_socket_alloc();
        if (!data->eth_sock)
            return 0;
        if (nl_connect(data->eth_sock, NETLINK_ETHTOOL) < 0) {
            nl_socket_free(data->eth_sock);
            data->eth_sock = NULL;
            return 0;
        }
        nl_socket_modify_cb(data->eth_sock, NL_CB_VALID, NL_CB_CUSTOM, link_speed_cb, data);
    }

    data->buf_link_speed = 0;

    struct nl_msg *msg = nlmsg_alloc();
    if (!msg)
        return 0;

    struct nlmsghdr *hdr = nlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, ETHTOOL_MSG_LINKMODES_GET, 0, NLM_F_REQUEST);
    if (!hdr) {
        nlmsg_free(msg);
        return 0;
    }

    struct nlattr *nest = nla_nest_start(msg, ETHTOOL_A_LINKMODES_HEADER);
    nla_put_string(msg, ETHTOOL_A_HEADER_DEV_NAME, data->ifname);
    nla_nest_end(msg, nest);

    int err = nl_send_auto(data->eth_sock, msg);
    nlmsg_free(msg);
    if (err < 0)
        return 0;

    err = nl_recvmsgs_default(data->eth_sock);
    if (err < 0)
        return 0;

    return data->buf_link_speed;
}

static void format_ether_output(size_t module_id, char *buffer, size_t buffer_size, uint64_t rx, uint64_t tx) {
    if (modules[module_id].state) {
        struct network_data *data = modules[module_id].data;
        uint64_t speed = get_link_speed(data);
        snprintf(buffer, buffer_size, "󰈀 %ldM", speed);
    } else {
        char rxs[6], txs[6];
        format_storage_units(&rxs, rx);
        format_storage_units(&txs, tx);
        snprintf(buffer, buffer_size, "󰈀 %s %s", rxs, txs);
    }
}

static void format_wireless_output(size_t module_id, char *buffer, size_t buffer_size, uint64_t rx, uint64_t tx) {
    struct network_data *data = modules[module_id].data;
    int64_t level = -100;
    uint32_t tx_bitrate = 0;

    if (data->ifindex > 0 && refresh_wireless_status(data) == 0) {
        level = data->buf_station.signal_dbm ? data->buf_station.signal_dbm : -100;
        tx_bitrate = data->buf_station.tx_bitrate;
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
            snprintf(buffer, buffer_size, "%s %s %ldDB", icons[icon_idx], rate_str, level);
        } else {
            snprintf(buffer, buffer_size, "%s %ldDB", icons[icon_idx], level);
        }
    } else {
        char rxs[6], txs[6];
        format_storage_units(&rxs, rx);
        format_storage_units(&txs, tx);
        snprintf(buffer, buffer_size, "%s %s %s", icons[icon_idx], rxs, txs);
    }
}

static void update(size_t module_id) {
    char output_str[] = "🖧 0.00K 0.00K    ";
    struct network_data *data = modules[module_id].data;

    // 1) 非阻塞读取路由/链路事件
    bool changed = false;
    drain_route_events(data->rtnl_sock, &changed);

    // 2) 路由变更或尚未初始化时重新发现默认路由
    if (changed || data->ifindex <= 0) {
        if (!data->stat_sock) {
            data->ifindex = 0;
            data->ifname[0] = '\0';
            snprintf(output_str, sizeof(output_str), "󱞐");
            update_json(module_id, output_str);
            return;
        }
        int old_idx = data->ifindex;
        if (discover_default_route(data) < 0) {
            data->ifindex = 0;
            data->ifname[0] = '\0';
        } else if (data->nlsock && data->ifindex != old_idx && old_idx != 0) {
            // 接口变了，销毁 nl80211 socket 下次自动重建
            nl_socket_free(data->nlsock);
            data->nlsock = NULL;
            data->driver_id = -1;
        }
    }

    // 3) 无默认路由则显示离线
    if (data->ifindex <= 0) {
        snprintf(output_str, sizeof(output_str), "󱞐");
        update_json(module_id, output_str);
        return;
    }

    // 4) 查询流量统计
    struct rtnl_link *link = NULL;
    if (!data->stat_sock || rtnl_link_get_kernel(data->stat_sock, data->ifindex, NULL, &link) < 0 || !link) {
        snprintf(output_str, sizeof(output_str), "󱞐");
        update_json(module_id, output_str);
        return;
    }
    uint64_t rx = rtnl_link_get_stat(link, RTNL_LINK_RX_BYTES);
    uint64_t tx = rtnl_link_get_stat(link, RTNL_LINK_TX_BYTES);
    rtnl_link_put(link);

    // 5) 计算增量
    if (!data->prev_rx)
        data->prev_rx = rx;
    if (!data->prev_tx)
        data->prev_tx = tx;

    uint64_t drx = rx - data->prev_rx;
    uint64_t dtx = tx - data->prev_tx;
    data->prev_rx = rx;
    data->prev_tx = tx;
    rx = drx;
    tx = dtx;

    // 6) 格式化输出
    if (strncmp(data->ifname, "wl", 2) == 0)
        format_wireless_output(module_id, output_str, sizeof(output_str), rx, tx);
    else
        format_ether_output(module_id, output_str, sizeof(output_str), rx, tx);

    update_json(module_id, output_str);
}

static void alter(size_t module_id, uint64_t btn) {
    switch (btn) {
    case 2:
        system("iwgtk &");
        break;
    case 3:
        modules[module_id].state ^= 1;
        modules[module_id].update(module_id);
        break;
    }
}

static void del(size_t module_id) {
    struct network_data *data = modules[module_id].data;
    if (data->nlsock)
        nl_socket_free(data->nlsock);
    if (data->eth_sock)
        nl_socket_free(data->eth_sock);
    if (data->rtnl_sock)
        nl_socket_free(data->rtnl_sock);
    if (data->stat_sock)
        nl_socket_free(data->stat_sock);
    free(data);
}

void init_network(int epoll_fd) {
    INIT_BASE;

    struct network_data *data = calloc(1, sizeof(struct network_data));
    if (!data) {
        perror("calloc");
        modules_cnt--;
        return;
    }

    // 路由/链路事件监控 socket（只收不发），注册到 epoll 实现立即响应
    data->rtnl_sock = nl_socket_alloc();
    if (data->rtnl_sock) {
        if (nl_connect(data->rtnl_sock, NETLINK_ROUTE) == 0) {
            nl_socket_add_membership(data->rtnl_sock, RTNLGRP_IPV4_ROUTE);
            nl_socket_add_membership(data->rtnl_sock, RTNLGRP_IPV6_ROUTE);
            nl_socket_add_membership(data->rtnl_sock, RTNLGRP_LINK);
            nl_socket_set_nonblocking(data->rtnl_sock);
            struct epoll_event ev = {.events = EPOLLIN, .data.u64 = module_id};
            epoll_ctl(epoll_fd, EPOLL_CTL_ADD, nl_socket_get_fd(data->rtnl_sock), &ev);
        } else {
            nl_socket_free(data->rtnl_sock);
            data->rtnl_sock = NULL;
        }
    }

    // 流量查询 + 路由发现 socket
    data->stat_sock = nl_socket_alloc();
    if (data->stat_sock && nl_connect(data->stat_sock, NETLINK_ROUTE) < 0) {
        nl_socket_free(data->stat_sock);
        data->stat_sock = NULL;
    }

    // 初始发现默认路由
    if (data->stat_sock)
        discover_default_route(data);

    modules[module_id].data = data;
    modules[module_id].update = update;
    modules[module_id].alter = alter;
    modules[module_id].del = del;
    modules[module_id].interval = 1;

    UPDATE_Q(module_id);
}
