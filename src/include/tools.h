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
