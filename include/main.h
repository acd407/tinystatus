#include <stddef.h>
#define MOD_SIZE 16
#define BUF_SIZE 4096

typedef struct {
    void (*update) (void);
    char *output;
    int *fds; // 监听的fd,给派发器用,数组末尾为-1
    unsigned int sec : 1;
} module_t;

extern module_t modules[];
extern size_t modules_cnt;

