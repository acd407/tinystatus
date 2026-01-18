#include <stddef.h>
#include <stdint.h>
void format_storage_units(char (*buf)[6], double bytes);
uint64_t read_uint64_file(char *file);
void update_json(size_t module_id, const char *output_str, const char *color);

#define ARRAY_SIZE(arr)                                                                                                \
    ((void)(sizeof(struct {                                                                                            \
         static_assert(                                                                                                \
             !__builtin_types_compatible_p(typeof(arr), typeof(&(arr)[0])), "ARRAY_SIZE cannot be used on pointers"    \
         );                                                                                                            \
         int _dummy;                                                                                                   \
     })),                                                                                                              \
     sizeof(arr) / sizeof((arr)[0]))

// 查找匹配的文件路径
// path_pattern: 路径模式，可以包含通配符，如"/sys/class/powercap/intel-rapl*/name"
// target_content: 要匹配的文件内容，如"package-0"
char *match_content_path(const char *path_pattern, const char *target_content);

// 使用正则表达式替换路径中的部分
// input_path: 输入路径
// match_pattern: 匹配模式，如"temp(.*)_label"
// replace_pattern: 替换模式，如"temp\\1_input"
char *regex(const char *input_path, const char *match_pattern, const char *replace_pattern);
