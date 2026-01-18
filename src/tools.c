#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <math.h>
#include <limits.h>
#include <cjson/cJSON.h>
#include <glob.h>
#include <regex.h>
#include "tools.h"
#include "main.h"

void format_storage_units (char (*buf)[6], double bytes) {
    const char *units = "KMGTPE"; // 单位从 K 开始
    int unit_idx = -1;            // 0=K, 1=M, 2=G, 3=T, 4=P, 5=E

    // 确保最小单位为 K
    while ((bytes >= 1000.0 && unit_idx < 5) || unit_idx < 0) {
        bytes /= 1024;
        unit_idx++;
    }

    // 动态选择格式
    if (bytes >= 100.0) {
        snprintf (*buf, 6, " %3.0f%c", round (bytes), units[unit_idx]);
    } else if (bytes >= 10.0) {
        snprintf (*buf, 6, "%4.1f%c", bytes, units[unit_idx]);
    } else {
        snprintf (*buf, 6, "%4.2f%c", bytes, units[unit_idx]);
    }
}

uint64_t read_uint64_file (char *file) {
    FILE *file_soc = fopen (file, "r");
    if (!file_soc) {
        fprintf (stderr, "fopen: %s: %s", file, strerror (errno));
        // perror ("fopen");
        exit (EXIT_FAILURE);
    }
    uint64_t ans = 0;
    if (EOF == fscanf (file_soc, "%lu", &ans)) {
        // 检查是否是因为尝试读取目录导致的错误
        struct stat st;
        if (stat(file, &st) == 0 && S_ISDIR(st.st_mode)) {
            fprintf (stderr, "Error: Trying to read a directory as a file: %s\n", file);
        } else {
            perror ("fscanf");
        }
        // exit (EXIT_FAILURE);
    }
    fclose (file_soc);
    return ans;
}

void update_json (size_t module_id, const char *output_str, const char *color) {
    cJSON *json = cJSON_CreateObject ();

    char name[] = "A";
    *name += module_id;

    cJSON_AddStringToObject (json, "name", name);
    cJSON_AddFalseToObject (json, "separator");
    cJSON_AddNumberToObject (json, "separator_block_width", 0);
    cJSON_AddStringToObject (json, "markup", "pango");
    cJSON_AddStringToObject (json, "full_text", output_str);
    cJSON_AddStringToObject (json, "color", color);

    if (modules[module_id].output) {
        free (modules[module_id].output);
    }
    modules[module_id].output = cJSON_PrintUnformatted (json);

    cJSON_Delete (json);
}

// 查找匹配的文件路径
// path_pattern: 路径模式，可以包含通配符，如"/sys/class/powercap/intel-rapl*/name"
// target_content: 要匹配的文件内容，如"package-0"
char *match_content_path(
    const char *path_pattern, const char *target_content
) {
    glob_t glob_result;
    char *result_path = NULL;
    
    // 使用glob匹配路径模式
    int ret = glob(path_pattern, 0, NULL, &glob_result);
    if (ret != 0) {
        globfree(&glob_result);
        return NULL;
    }
    
    // 遍历所有匹配的路径
    for (size_t i = 0; i < glob_result.gl_pathc; i++) {
        const char *file_path = glob_result.gl_pathv[i];
        
        // 读取文件内容
        FILE *fp = fopen(file_path, "r");
        if (!fp) {
            continue;
        }
        
        char content[256];
        if (fgets(content, sizeof(content), fp) == NULL) {
            fclose(fp);
            continue;
        }
        fclose(fp);
        
        // 去除换行符
        content[strcspn(content, "\n")] = '\0';
        
        // 检查是否是目标内容
        if (strcmp(content, target_content) == 0) {
            result_path = strdup(file_path);
            break;
        }
    }
    
    globfree(&glob_result);
    return result_path;
}

// 使用正则表达式替换路径中的部分
// input_path: 输入路径
// match_pattern: 匹配模式，如"temp(.*)_label"
// replace_pattern: 替换模式，如"temp\\1_input"
char *regex(
    const char *input_path, const char *match_pattern, const char *replace_pattern
) {
    if (!input_path || !match_pattern || !replace_pattern) {
        return NULL;
    }
    
    // 提取文件名
    const char *filename = strrchr(input_path, '/');
    if (!filename) {
        filename = input_path;
    } else {
        filename++; // 跳过斜杠
    }
    
    // 编译正则表达式
    regex_t regex;
    if (regcomp(&regex, match_pattern, REG_EXTENDED) != 0) {
        return NULL;
    }
    
    // 执行匹配
    regmatch_t matches[10]; // 最多支持9个捕获组
    if (regexec(&regex, filename, 10, matches, 0) != 0) {
        regfree(&regex);
        return NULL;
    }
    
    // 构建结果路径
    char result_path[PATH_MAX];
    char dir_path[PATH_MAX];
    
    // 保存目录部分
    if (filename != input_path) {
        size_t dir_len = filename - input_path - 1; // 减1去掉斜杠
        strncpy(dir_path, input_path, dir_len);
        dir_path[dir_len] = '\0';
    } else {
        strcpy(dir_path, ".");
    }
    
    // 构建新的文件名
    char new_filename[NAME_MAX];
    const char *src = replace_pattern;
    char *dst = new_filename;
    
    while (*src && dst < new_filename + sizeof(new_filename) - 1) {
        if (*src == '\\' && *(src + 1) >= '1' && *(src + 1) <= '9') {
            // 处理反向引用 \1, \2, ..., \9
            int group_num = *(src + 1) - '0';
            if (group_num < 10 && matches[group_num].rm_so != -1) {
                // 复制匹配的组
                size_t match_len = matches[group_num].rm_eo - matches[group_num].rm_so;
                size_t copy_len = match_len;
                if (dst + copy_len >= new_filename + sizeof(new_filename) - 1) {
                    copy_len = new_filename + sizeof(new_filename) - 1 - dst;
                }
                memcpy(dst, filename + matches[group_num].rm_so, copy_len);
                dst += copy_len;
            }
            src += 2;
        } else {
            // 普通字符
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    
    // 构建完整路径
    snprintf(result_path, sizeof(result_path), "%s/%s", dir_path, new_filename);
    
    regfree(&regex);
    return strdup(result_path);
}
