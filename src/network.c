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

static void get_rate (uint64_t *rx, uint64_t *tx, char *master) {
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

    bool found = false, found_ether = false;

    while (fgets (buffer, sizeof (buffer), fp)) {
        char *line = buffer;
        while (*line == ' ')
            line++; // 跳过前导空格

        // 只接受 wlan 和 ether
        if (*line != 'w' && *line != 'e') {
            continue;
        }

        if (*line == 'e') {
            found_ether = true;
        }
        if (!found_ether || *line == 'e') {
            // 保存接口名称
            size_t cnt = 0;
            while (isalnum (*line) && cnt < IFNAMSIZ - 1) {
                master[cnt++] = *line++;
            }
            master[cnt] = '\0';
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

    cJSON_AddStringToObject (json, "name", name);
    cJSON_AddStringToObject (json, "color", IDLE);
    cJSON_AddFalseToObject (json, "separator");
    cJSON_AddNumberToObject (json, "separator_block_width", 0);
    cJSON_AddStringToObject (json, "markup", "pango");

    char *icons[] = {"󰈀", "󰖩"};
    // 󰈀: 4 byte
    // \u2004: 3 byte
    char output_str[] = "🖧\u20040.00K\u20040.00K  ";

    uint64_t rx = 0, tx = 0;
    char master[IFNAMSIZ];
    get_rate (&rx, &tx, master);

    char *icon = icons[master[0] == 'w'];
    for (size_t i = 0; i < 4; i++)
        output_str[i] = icon[i];

    if (modules[module_id].state) {
        snprintf (output_str + 7, sizeof (output_str) - 7, "%s", master);
    } else {
        format_storage_units (output_str + 7, tx);
        // format_storage_units 会调用 snprintf，将 speed[12] 置为\0
        // \u2004 = e28084
        output_str[12] = '\xe2';
        format_storage_units (output_str + 15, rx);
    }
    cJSON_AddStringToObject (json, "full_text", output_str);

    modules[module_id].output = cJSON_PrintUnformatted (json);

    cJSON_Delete (json);
}

static void alter (uint64_t btn) {
    switch (btn) {
    case 2: // middle button
        system ("iwgtk &");
        break;
    case 3: // right button
        modules[module_id].state ^= 1;
        modules[module_id].update ();
        break;
    }
}

void init_network (int epoll_fd) {
    (void) epoll_fd;
    INIT_BASE ();

    modules[module_id].update = update;
    modules[module_id].alter = alter;
    modules[module_id].interval = 1;

    UPDATE_Q ();
}
