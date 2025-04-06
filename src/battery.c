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
#include <unistd.h>

#define UPOWER_SERVICE "org.freedesktop.UPower"
#define DEVICE_INTERFACE "org.freedesktop.UPower.Device"
#define BATTERY "/org/freedesktop/UPower/devices/battery_BAT0"

// 获取电池属性值
void get_property (
    DBusConnection *conn, const char *device_path, const char *property_name,
    int expected_type, void *value
) {
    DBusError err;
    dbus_error_init (&err);

    DBusMessage *msg = dbus_message_new_method_call (
        UPOWER_SERVICE, device_path, "org.freedesktop.DBus.Properties", "Get"
    );

    if (msg == NULL) {
        exit (EXIT_FAILURE);
    }

    const char *interface = DEVICE_INTERFACE;
    dbus_message_append_args (
        msg, DBUS_TYPE_STRING, &interface, DBUS_TYPE_STRING, &property_name,
        DBUS_TYPE_INVALID
    );

    DBusMessage *reply =
        dbus_connection_send_with_reply_and_block (conn, msg, -1, &err);
    dbus_message_unref (msg);

    if (dbus_error_is_set (&err)) {
        dbus_error_free (&err);
        exit (EXIT_FAILURE);
    }

    DBusMessageIter iter, variant;
    dbus_message_iter_init (reply, &iter);
    dbus_message_iter_recurse (&iter, &variant);

    int type = dbus_message_iter_get_arg_type (&variant);
    if (type != expected_type) {
        dbus_message_unref (reply);
        exit (EXIT_FAILURE);
    }

    dbus_message_iter_get_basic (&variant, value);
    dbus_message_unref (reply);
}

typedef enum {
    UNKNOWN,
    CHARGING,
    DISCHARGING,
    EMPTY,
    FULLY_CHARGED,
    PENDING_CHARGE,
    PENDING_DISCHARGE,
    LAST
} State;

static void update (size_t module_id) {
    if (modules[module_id].output) {
        free (modules[module_id].output);
    }

    DBusConnection *conn = modules[module_id].data.ptr;
    dbus_connection_read_write (conn, 0);
    DBusMessage *msg;

    State state;
    double energy, percentage_float, energy_rate;
    int64_t time = 0;
    bool has_new_message = false;

    // 使用 do-while 确保至少获取一次状态
    do {
        // 获取当前电池状态
        get_property (conn, BATTERY, "State", DBUS_TYPE_UINT32, &state);
        get_property (conn, BATTERY, "Energy", DBUS_TYPE_DOUBLE, &energy);
        get_property (
            conn, BATTERY, "Percentage", DBUS_TYPE_DOUBLE, &percentage_float
        );
        get_property (
            conn, BATTERY, "EnergyRate", DBUS_TYPE_DOUBLE, &energy_rate
        );

        switch (state) {
        case CHARGING:
            get_property (conn, BATTERY, "TimeToFull", DBUS_TYPE_INT64, &time);
            break;
        case DISCHARGING:
            get_property (conn, BATTERY, "TimeToEmpty", DBUS_TYPE_INT64, &time);
            break;
        default:
            break;
        }

        // 检查是否有新消息需要处理
        has_new_message = false;
        while ((msg = dbus_connection_pop_message (conn)) != NULL) {
            if (dbus_message_is_signal (
                    msg, "org.freedesktop.DBus.Properties", "PropertiesChanged"
                )) {
                has_new_message = true;
            }
            dbus_message_unref (msg);
        }

        // 如果有新消息，则再循环一次以获取最新状态
    } while (has_new_message);

    char *icons_charging[] = {
        "\xf3\xb0\x82\x86", // 󰂆
        "\xf3\xb0\x82\x87", // 󰂇
        "\xf3\xb0\x82\x88", // 󰂈
        "\xf3\xb0\x82\x89", // 󰂉
        "\xf3\xb0\x82\x8a", // 󰂊
        "\xf3\xb0\x82\x8b", // 󰂋
        "\xf3\xb0\x82\x85"  // 󰂅
    };

    char *icons_discharging[] = {
        "\xf3\xb0\x82\x8e", // 󰂎
        "\xf3\xb0\x81\xba", // 󰁺
        "\xf3\xb0\x81\xbb", // 󰁻
        "\xf3\xb0\x81\xbc", // 󰁼
        "\xf3\xb0\x81\xbd", // 󰁽
        "\xf3\xb0\x81\xbe", // 󰁾
        "\xf3\xb0\x81\xbf", // 󰁿
        "\xf3\xb0\x82\x80", // 󰂀
        "\xf3\xb0\x82\x81", // 󰂁
        "\xf3\xb0\x82\x82", // 󰂂
        "\xf3\xb0\x81\xb9", // 󰁹
    };

    uint64_t percentage = percentage_float;
    char output_str[100], *output_p = output_str;

    char *colors[] = {CRITICAL, WARNING, IDLE};
    size_t colors_idx = percentage < 20 ? 0 : (percentage < 40 ? 1 : 2);

    switch (state) { // 输出图标
    case CHARGING:
    case FULLY_CHARGED: {
        size_t idx =
            sizeof (icons_charging) / sizeof (char *) * percentage / 101;
        output_p += snprintf (
            output_p, sizeof (output_str) - (output_p - output_str),
            "<span color='" GOOD "'>%s</span>", icons_charging[idx]
        );
        break;
    }
    case DISCHARGING:
    case EMPTY: {
        size_t idx =
            sizeof (icons_discharging) / sizeof (char *) * percentage / 101;
        output_p += snprintf (
            output_p, sizeof (output_str) - (output_p - output_str),
            "<span color='%s'>%s</span>", colors[colors_idx],
            icons_discharging[idx]
        );
        break;
    }
    default: // syncing
        output_p += snprintf (
            output_p, sizeof (output_str) - (output_p - output_str),
            "<span color='" DEACTIVE "'>\xf3\xb1\xa0\xb5</span>" // 󱠵
        );
        goto end_digits;
    }

    // 输出文字
    if (modules[module_id].state) {
        output_p += snprintf (
            output_p, sizeof (output_str) - (output_p - output_str),
            "\u2004<span color='%s'>%.1fWh</span>", colors[colors_idx], energy
        );
        if (time > 0) {
            output_p += snprintf (
                output_p, sizeof (output_str) - (output_p - output_str),
                "\u2004(%.1fW)", energy_rate
            );
        }
    } else {
        output_p += snprintf (
            output_p, sizeof (output_str) - (output_p - output_str),
            "\u2004<span color='%s'>%ld%%</span>", colors[colors_idx],
            percentage
        );
        if (time > 0) {
            uint64_t hours = time / 3600;
            uint64_t minutes = (time - hours * 3600) / 60;
            output_p += snprintf (
                output_p, sizeof (output_str) - (output_p - output_str),
                "\u2004(%ld:%02ld)", hours, minutes
            );
        }
    }
end_digits:

    cJSON *json = cJSON_CreateObject ();

    char name[] = "A";
    *name += module_id;

    cJSON_AddStringToObject (json, "name", name);
    cJSON_AddStringToObject (json, "color", IDLE);
    cJSON_AddFalseToObject (json, "separator");
    cJSON_AddNumberToObject (json, "separator_block_width", 0);
    cJSON_AddStringToObject (json, "markup", "pango");
    cJSON_AddStringToObject (json, "full_text", output_str);

    modules[module_id].output = cJSON_PrintUnformatted (json);

    cJSON_Delete (json);
}

static void alter (size_t module_id, uint64_t btn) {
    switch (btn) {
    case 3: // right button
        modules[module_id].state ^= 1;
        break;
    default:
        return;
    }
    modules[module_id].update (module_id);
}

void init_battery (int epoll_fd) {
    INIT_BASE ();

    DBusError err;
    DBusConnection *conn;

    dbus_error_init (&err);
    conn = dbus_bus_get (DBUS_BUS_SYSTEM, &err);

    if (dbus_error_is_set (&err)) {
        fprintf (stderr, "DBus error: %s\n", err.message);
        dbus_error_free (&err);
        exit (EXIT_FAILURE);
    }

    // 添加D-Bus信号匹配规则
    char *match_rule =
        "type='signal',interface='org.freedesktop.DBus.Properties',"
        "path='" BATTERY "',arg0='" DEVICE_INTERFACE "'";

    dbus_bus_add_match (conn, match_rule, &err);
    if (dbus_error_is_set (&err)) {
        dbus_error_free (&err);
        return;
    }

    // 添加D-Bus连接到epoll
    int dbus_fd;
    dbus_connection_get_unix_fd (conn, &dbus_fd);
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.u64 = module_id;
    if (epoll_ctl (epoll_fd, EPOLL_CTL_ADD, dbus_fd, &ev) == -1) {
        perror ("epoll_ctl: dbus_fd");
        exit (EXIT_FAILURE);
    }

    modules[module_id].data.ptr = conn;
    modules[module_id].update = update;
    modules[module_id].alter = alter;

    dbus_connection_unref (conn);

    UPDATE_Q ();
}
