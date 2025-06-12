#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define MOD_SIZE 16
#define BUF_SIZE 4096

typedef struct {
    char *output;      // 输出
    uint64_t interval; // 确定模块更新的时间间隔，0表示不随时间更新
    uint64_t state;    // input 专用
    void (*alter) (size_t, uint64_t); // input 专用
    void (*update) (size_t);          // 子 update
    void (*del) (size_t);             // 析构函数
    union {                           // 模块内部数据
        void *ptr;
        uint64_t num;
    } data;
} module_t;

extern module_t modules[];
extern size_t modules_cnt;
