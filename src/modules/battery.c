#include <cJSON.h>
#include <dbus/dbus.h>
#include <fcntl.h>
#include <inttypes.h>
#include <module_base.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <time.h>
#include <tools.h>
#include <unistd.h>

#define UPOWER_SERVICE "org.freedesktop.UPower"
#define DEVICE_INTERFACE "org.freedesktop.UPower.Device"
#define BATTERY "/org/freedesktop/UPower/devices/battery_BAT0"

static void dbus_get_property(
    DBusConnection *conn, const char *device_path, const char *property_name, int expected_type, void *value
) {
    DBusError err;
    dbus_error_init(&err);

    DBusMessage *msg =
        dbus_message_new_method_call(UPOWER_SERVICE, device_path, "org.freedesktop.DBus.Properties", "Get");
    if (msg == NULL) {
        exit(EXIT_FAILURE);
    }

    const char *interface = DEVICE_INTERFACE;
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &interface, DBUS_TYPE_STRING, &property_name, DBUS_TYPE_INVALID);

    DBusMessage *reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &err);
    dbus_message_unref(msg);
    if (dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        exit(EXIT_FAILURE);
    }

    DBusMessageIter iter, variant;
    dbus_message_iter_init(reply, &iter);
    dbus_message_iter_recurse(&iter, &variant);

    int type = dbus_message_iter_get_arg_type(&variant);
    if (type != expected_type) {
        dbus_message_unref(reply);
        exit(EXIT_FAILURE);
    }

    dbus_message_iter_get_basic(&variant, value);
    dbus_message_unref(reply);
}

typedef enum { UNKNOWN, CHARGING, DISCHARGING, EMPTY, FULLY_CHARGED, PENDING_CHARGE, PENDING_DISCHARGE, LAST } State;

static void get_battery_property(
    uint64_t module_id, State *state, double *percentage, int64_t *time, double *energy, double *energy_rate
) {
    DBusConnection *conn = modules[module_id].data.ptr;
    dbus_connection_read_write(conn, 0);

    dbus_get_property(conn, BATTERY, "State", DBUS_TYPE_UINT32, state);
    dbus_get_property(conn, BATTERY, "Energy", DBUS_TYPE_DOUBLE, energy);
    dbus_get_property(conn, BATTERY, "Percentage", DBUS_TYPE_DOUBLE, percentage);
    dbus_get_property(conn, BATTERY, "EnergyRate", DBUS_TYPE_DOUBLE, energy_rate);

    switch (*state) {
    case CHARGING:
        dbus_get_property(conn, BATTERY, "TimeToFull", DBUS_TYPE_INT64, time);
        break;
    case DISCHARGING:
        dbus_get_property(conn, BATTERY, "TimeToEmpty", DBUS_TYPE_INT64, time);
        break;
    default:
        break;
    }

    // 清理所有的消息
    DBusMessage *msg;
    while ((msg = dbus_connection_pop_message(conn)) != NULL) {
        dbus_message_unref(msg);
    }
}

static void update(size_t module_id) {
    State state = UNKNOWN;
    double energy = 0, percentage_float = 0, energy_rate = 0;
    int64_t time2ef = -1; // time to Empty/Full

    get_battery_property(module_id, &state, &percentage_float, &time2ef, &energy, &energy_rate);

    uint64_t percentage = percentage_float;
    char output_str[100], *output_p = output_str;

    char *colors[] = {CRITICAL, WARNING, IDLE};
    size_t colors_idx = percentage < 20 ? 0 : (percentage < 40 ? 1 : 2);

    const char *icons_charging[] = {"󰂆", "󰂇", "󰂈", "󰂉", "󰂊", "󰂋", "󰂅"};

    const char *icons_discharging[] = {
        "󰂎", "󰁺", "󰁻", "󰁼", "󰁽", "󰁾", "󰁿", "󰂀", "󰂁", "󰂂", "󰁹",
    };

    // 输出图标
    switch (state) {
    case CHARGING:
    case FULLY_CHARGED: {
        size_t idx = ARRAY_SIZE(icons_charging) * percentage / 101;
        output_p += snprintf(
            output_p, sizeof(output_str) - (output_p - output_str), "<span color='" GOOD "'>%s</span>",
            icons_charging[idx]
        );
        break;
    }
    case DISCHARGING:
    case EMPTY: {
        size_t idx = ARRAY_SIZE(icons_discharging) * percentage / 101;
        output_p += snprintf(
            output_p, sizeof(output_str) - (output_p - output_str), "<span color='%s'>%s</span>", colors[colors_idx],
            icons_discharging[idx]
        );
        break;
    }
    default: // 同步中
        output_p +=
            snprintf(output_p, sizeof(output_str) - (output_p - output_str), "<span color='" DEACTIVE "'>󱠵</span>");
        goto generate_json;
    }

    // 输出文字
    if (modules[module_id].state) {
        output_p += snprintf(
            output_p, sizeof(output_str) - (output_p - output_str), "\u2004<span color='%s'>%.1fWh</span>",
            colors[colors_idx], energy
        );
        if (time2ef > 0) {
            output_p += snprintf(output_p, sizeof(output_str) - (output_p - output_str), "\u2004(%.1fW)", energy_rate);
        }
    } else {
        output_p += snprintf(
            output_p, sizeof(output_str) - (output_p - output_str), "\u2004<span color='%s'>%ld%%</span>",
            colors[colors_idx], percentage
        );
        if (time2ef > 0) {
            uint64_t hours = time2ef / 3600;
            uint64_t minutes = (time2ef - hours * 3600) / 60;
            output_p +=
                snprintf(output_p, sizeof(output_str) - (output_p - output_str), "\u2004(%ld:%02ld)", hours, minutes);
        }
    }

generate_json:

    update_json(module_id, output_str, IDLE);
}

static void alter(size_t module_id, uint64_t btn) {
    switch (btn) {
    case 2:
        system("gnome-power-statistics &");
        break;
    case 3: // right button
        modules[module_id].state ^= 1;
        modules[module_id].update(module_id);
        break;
    }
}

static void del(uint64_t module_id) {
    dbus_connection_unref(modules[module_id].data.ptr);
}

void init_battery(int epoll_fd) {
    INIT_BASE;

    // 初始化 dbus 错误
    DBusError err;
    dbus_error_init(&err);

    // 访问系统总线
    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        modules_cnt--;
        return;
    }

    // 添加D-Bus信号匹配规则
    char *match_rule = "type='signal',interface='org.freedesktop.DBus.Properties',"
                       "path='" BATTERY "',arg0='" DEVICE_INTERFACE "'";
    dbus_bus_add_match(conn, match_rule, &err);
    if (dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        dbus_connection_unref(conn);
        modules_cnt--;
        return;
    }

    // 添加D-Bus连接到epoll
    int dbus_fd;
    dbus_connection_get_unix_fd(conn, &dbus_fd);
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.u64 = module_id;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, dbus_fd, &ev) == -1) {
        perror("epoll_ctl: dbus_fd");
        dbus_connection_unref(conn);
        modules_cnt--;
        return;
    }

    modules[module_id].data.ptr = conn;
    modules[module_id].update = update;
    modules[module_id].alter = alter;
    modules[module_id].del = del;

    UPDATE_Q(module_id);
}
