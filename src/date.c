#include <cJSON.h>
#include <module_base.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tools.h>

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

    time_t raw_time;
    time (&raw_time);                             // 获取当前时间戳
    struct tm *time_info = localtime (&raw_time); // 转换为本地时间
    char output_str[80];
    strftime (
        output_str, sizeof (output_str), "%a\u2004%d\u2004%H:%M:%S", time_info
    );
    cJSON_AddStringToObject (json, "full_text", output_str);

    // 将JSON对象转换为字符串
    modules[module_id].output = cJSON_PrintUnformatted (json);

    // 删除JSON对象并释放内存
    cJSON_Delete (json);
}

void init_date (int epoll_fd) {
    (void) epoll_fd;
    init_base();

    modules[module_id].update = update;
    modules[module_id].sec = 1;
}
