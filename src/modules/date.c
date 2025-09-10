#include <cJSON.h>
#include <module_base.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tools.h>

static void update (size_t module_id) {
    time_t raw_time;
    time (&raw_time);                             // 获取当前时间戳
    struct tm *time_info = localtime (&raw_time); // 转换为本地时间
    char output_str[80];
    strftime (
        output_str, sizeof (output_str), "%a\u2004%m/%d\u2004%H:%M:%S",
        time_info
    );

    update_json (module_id, output_str, IDLE);
}

static void alter (size_t module_id, uint64_t btn) {
    (void) module_id;
    switch (btn) {
    case 2: // middle btn
        system ("qjournalctl &");
        break;
    }
}

void init_date (int epoll_fd) {
    (void) epoll_fd;
    INIT_BASE;

    modules[module_id].update = update;
    modules[module_id].alter = alter;
    modules[module_id].interval = 1;

    UPDATE_Q (module_id);
}
