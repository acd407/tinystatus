#include <stddef.h>
#include <stdint.h>
void format_storage_units (char (*buf)[6], double bytes);
uint64_t read_uint64_file (char *file);
void update_json (size_t module_id, char *output_str, char *color);
