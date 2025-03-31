#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define MOD_SIZE 16
#define BUF_SIZE 4096

typedef struct {
    void (*update) (void);
    void (*alter) (uint64_t);
    char *output;
    int *fds;             // 监听的fd,给派发器用,数组末尾为-1
    unsigned int sec : 1; // 一个timer的附加项，确定模块是否每秒更新
    uint64_t state;
} module_t;

extern module_t modules[];
extern size_t modules_cnt;
