#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define MOD_SIZE 16
#define BUF_SIZE 4096

typedef struct {
    char *output;      // 模块输出，各模块输出由全局的output函数统一收集输出
    uint64_t interval; // 确定模块更新的时间间隔，0表示不随时间更新
    uint64_t state;    // 模块的状态，用于可以改变状态的模块，如支持右键单击
    void (*alter) (size_t, uint64_t); // 改变模块状态时的回调函数
    void (*update) (size_t);          // 更新模块状态时的回调函数
    void (*del) (size_t);             // 析构函数
    union {                           // 模块内部数据，由模块自己决定如何实现
        void *ptr;
        uint64_t num;
    } data;
} module_t;

extern module_t modules[];
extern size_t modules_cnt;
