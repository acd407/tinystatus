#include <assert.h>
#include <main.h>

#define GOOD "#98BC37"
#define IDLE "#FCE8C3"
#define WARNING "#FED06E"
#define CRITICAL "#F75341"

static size_t module_id;

#define INIT_BASE()                                                            \
    do {                                                                       \
        module_id = modules_cnt++;                                             \
        assert (module_id < MOD_SIZE);                                         \
        modules[module_id].update = NULL;                                      \
        modules[module_id].alter = NULL;                                       \
        modules[module_id].output = NULL;                                      \
        modules[module_id].del = NULL;                                         \
        modules[module_id].fds = NULL;                                         \
        modules[module_id].interval = 0;                                       \
        modules[module_id].state = 0;                                          \
        modules[module_id].data.ptr = NULL;                                    \
    } while (0)

// 刷新那些不随时间刷新的 modules
#define UPDATE_Q()                                                             \
    do {                                                                       \
        if (modules[module_id].interval == 0)                                  \
            update ();                                                         \
    } while (0)
