#include <fcntl.h>
#include <math.h>
#include <module_base.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tools.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <errno.h>

#define GPU_USAGE_TOOL TOOLS_DIR "/i915_gpu_usage"
#define VRAM_TOOL TOOLS_DIR "/i915_vmem"

// 存储GPU状态和进程信息
struct intel_gpu_storage {
    pid_t gpu_usage_pid;   // GPU使用率工具进程ID
    pid_t vram_pid;        // 显存工具进程ID
    int gpu_usage_fd;      // GPU使用率管道读端
    int vram_fd;           // 显存使用管道读端
    double gpu_usage;      // 当前GPU使用率
    uint64_t vram_usage;   // 当前显存使用量
    int gpu_usage_ready;   // GPU使用率是否已准备好
    int vram_ready;        // 显存使用量是否已准备好
    int epoll_fd;          // 保存 epoll_fd，供 del() 移除 fd
};

// 启动工具进程并创建管道：pipe() → fork() → child dup2 + exec → parent close(write)
// 返回 0 成功，-1 失败。成功后 *pid / *read_fd 有效。
static int spawn_tool(const char *tool_path, pid_t *pid, int *read_fd) {
    int pipe_fd[2];

    if (pipe(pipe_fd) == -1) {
        perror("pipe");
        return -1;
    }

    pid_t child = fork();
    if (child == -1) {
        perror("fork");
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return -1;
    }

    if (child == 0) {
        // ── 子进程 ──
        close(pipe_fd[0]);                     // 关闭读端
        dup2(pipe_fd[1], STDOUT_FILENO);       // stdout → pipe 写端
        close(pipe_fd[1]);                     // 关闭原始写端（dup2 后的副本已够用）

        // 子进程不需要继承 epoll fd 等多余的 fd，但保持简单 — exec 后全部消失
        execl(tool_path, tool_path, NULL);
        // exec 失败才走到这里
        _exit(127);
    }

    // ── 父进程 ──
    close(pipe_fd[1]);        // 关闭写端
    *pid = child;
    *read_fd = pipe_fd[0];
    return 0;
}

// 从管道读取GPU使用率
static void read_gpu_usage(int fd, double *usage, int *ready) {
    char buf[256];
    ssize_t bytes_read;

    // 非阻塞读取所有可用数据
    while ((bytes_read = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[bytes_read] = '\0';

        // 解析GPU使用率
        if (sscanf(buf, "%lf", usage) == 1) {
            *ready = 1;
        }
    }

    if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("read gpu_usage");
    }
}

// 从管道读取显存使用量
static void read_vram_usage(int fd, uint64_t *vram, int *ready) {
    char buf[256];
    ssize_t bytes_read;

    // 非阻塞读取所有可用数据
    while ((bytes_read = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[bytes_read] = '\0';

        // 解析显存使用量
        if (sscanf(buf, "%lu", vram) == 1) {
            *ready = 1;
        }
    }

    if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("read vram");
    }
}

// 更新函数：epoll 事件触发时读取管道数据
static void update(size_t module_id) {
    struct intel_gpu_storage *storage = (struct intel_gpu_storage *)modules[module_id].data;

    // 读取GPU使用率
    read_gpu_usage(storage->gpu_usage_fd, &storage->gpu_usage, &storage->gpu_usage_ready);

    // 读取显存使用量
    read_vram_usage(storage->vram_fd, &storage->vram_usage, &storage->vram_ready);

    // 如果数据还没准备好，不更新显示
    if (!storage->gpu_usage_ready) {
        return;
    }

    char output_str[32] = "󰍹\u2004";
    double usage = storage->gpu_usage;

    if (modules[module_id].state && storage->vram_ready) {
        char vram_str[6];
        format_storage_units(&vram_str, storage->vram_usage);
        strcat(output_str, vram_str);
    } else {
        const char *format_inactive = "%4.*f%%";
        int precision = usage < 10 ? 2 : 1;
        snprintf(
            output_str + strlen(output_str), sizeof(output_str) - strlen(output_str), format_inactive, precision, usage
        );
    }

    char *colors[] = {IDLE, WARNING, CRITICAL};
    size_t idx;
    if (usage < 30)
        idx = 0;
    else if (usage < 60)
        idx = 1;
    else
        idx = 2;

    update_json(module_id, output_str, colors[idx]);
}

// 状态切换函数
static void alter(size_t module_id, uint64_t btn) {
    switch (btn) {
    case 3: // right button
        modules[module_id].state ^= 1;
        break;
    default:
        return;
    }
    modules[module_id].update(module_id);
}

// 删除函数：清理资源
static void del(size_t module_id) {
    struct intel_gpu_storage *storage = (struct intel_gpu_storage *)modules[module_id].data;
    if (!storage)
        return;

    // 关闭管道读端 → pipe 断裂 → 子进程下次 write 收到 SIGPIPE 退出
    if (storage->gpu_usage_fd >= 0) {
        epoll_ctl(storage->epoll_fd, EPOLL_CTL_DEL, storage->gpu_usage_fd, NULL);
        close(storage->gpu_usage_fd);
    }
    if (storage->vram_fd >= 0) {
        epoll_ctl(storage->epoll_fd, EPOLL_CTL_DEL, storage->vram_fd, NULL);
        close(storage->vram_fd);
    }

    // 如果子进程还在跑，发 SIGTERM 让它退出
    // 不阻塞等待 — init 会收养僵尸，或随 tinystatus 退出统一清理
    if (storage->gpu_usage_pid > 0) {
        int wstatus;
        if (waitpid(storage->gpu_usage_pid, &wstatus, WNOHANG) == 0)
            kill(storage->gpu_usage_pid, SIGTERM);
    }
    if (storage->vram_pid > 0) {
        int wstatus;
        if (waitpid(storage->vram_pid, &wstatus, WNOHANG) == 0)
            kill(storage->vram_pid, SIGTERM);
    }

    free(storage);
}

// 向 epoll 注册一个管道读端（边缘触发）
static int register_epoll_fd(int epoll_fd, int fd, uint64_t module_id) {
    struct epoll_event ev = { .events = EPOLLIN | EPOLLET, .data.u64 = module_id };
    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

// 工具启动失败时的统一清理
static void cleanup_pipe_fd(int fd) {
    if (fd >= 0)
        close(fd);
}

static void cleanup_child(pid_t pid) {
    if (pid > 0) {
        kill(pid, SIGTERM);
    }
}

// 初始化Intel GPU模块
void init_intel_gpu(int epoll_fd) {
    INIT_BASE;

    // 创建存储结构体
    struct intel_gpu_storage *storage = calloc(1, sizeof(struct intel_gpu_storage));
    if (!storage) {
        perror("calloc");
        modules_cnt--;
        return;
    }

    storage->gpu_usage_fd = -1;
    storage->vram_fd = -1;
    storage->gpu_usage_pid = 0;
    storage->vram_pid = 0;
    storage->gpu_usage_ready = 0;
    storage->vram_ready = 0;
    storage->epoll_fd = epoll_fd;

    // 启动GPU使用率工具
    if (spawn_tool(GPU_USAGE_TOOL, &storage->gpu_usage_pid, &storage->gpu_usage_fd) == -1) {
        fprintf(stderr, "Failed to spawn %s\n", GPU_USAGE_TOOL);
        free(storage);
        modules_cnt--;
        return;
    }
    set_nonblocking(storage->gpu_usage_fd);

    // 启动显存使用工具
    if (spawn_tool(VRAM_TOOL, &storage->vram_pid, &storage->vram_fd) == -1) {
        fprintf(stderr, "Failed to spawn %s\n", VRAM_TOOL);
        cleanup_child(storage->gpu_usage_pid);
        cleanup_pipe_fd(storage->gpu_usage_fd);
        free(storage);
        modules_cnt--;
        return;
    }
    set_nonblocking(storage->vram_fd);

    // 注册 epoll — 两个管道共用一个 module_id，任一有数据都触发 update()
    if (register_epoll_fd(epoll_fd, storage->gpu_usage_fd, module_id) == -1) {
        perror("epoll_ctl gpu_usage_fd");
        cleanup_child(storage->vram_pid);
        cleanup_child(storage->gpu_usage_pid);
        cleanup_pipe_fd(storage->vram_fd);
        cleanup_pipe_fd(storage->gpu_usage_fd);
        free(storage);
        modules_cnt--;
        return;
    }

    if (register_epoll_fd(epoll_fd, storage->vram_fd, module_id) == -1) {
        perror("epoll_ctl vram_fd");
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, storage->gpu_usage_fd, NULL);
        cleanup_child(storage->vram_pid);
        cleanup_child(storage->gpu_usage_pid);
        cleanup_pipe_fd(storage->vram_fd);
        cleanup_pipe_fd(storage->gpu_usage_fd);
        free(storage);
        modules_cnt--;
        return;
    }

    // 设置模块回调
    modules[module_id].data = storage;
    modules[module_id].update = update;
    modules[module_id].alter = alter;
    modules[module_id].del = del;
    modules[module_id].interval = 0; // 完全事件驱动，不依赖定时器

    // 立即更新一次
    UPDATE_Q(module_id);
}
