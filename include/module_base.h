#include <main.h>

#define GOOD "#98BC37"
#define IDLE "#FCE8C3"
#define WARNING "#FED06E"
#define CRITICAL "#F75341"

static size_t module_id;

static void init_base () {
    module_id = modules_cnt++;

    modules[module_id].update = NULL;
    modules[module_id].alter = NULL;
    modules[module_id].output = NULL;
    modules[module_id].fds = NULL;
    modules[module_id].sec = false;
    modules[module_id].state = 0;
}
