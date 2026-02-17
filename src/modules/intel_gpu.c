#include <cJSON.h>
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
#include <glib.h>

#define GPU_USAGE_TOOL TOOLS_DIR "/i915_gpu_usage"
#define VRAM_TOOL TOOLS_DIR "/i915_vmem"

// 存储GPU状态和进程信息
struct intel_gpu_storage {
    GPid gpu_usage_pid;  // GPU使用率工具进程ID (GLib)
    GPid vram_pid;       // 显存工具进程ID (GLib)
    int gpu_usage_fd;    // GPU使用率管道的读端
    int vram_fd;         // 显存使用管道的读端
    double gpu_usage;    // 当前GPU使用率
    uint64_t vram_usage; // 当前显存使用量
    int gpu_usage_ready; // GPU使用率是否已准备好
    int vram_ready;      // 显存使用量是否已准备好
    int gpu_exited;      // GPU进程是否已退出
    int vram_exited;     // 显存进程是否已退出
};

// GPU使用率进程退出回调
static void gpu_usage_process_exited(GPid pid, gint status, gpointer data) {
    struct intel_gpu_storage *storage = (struct intel_gpu_storage *)data;

    g_print("GPU usage process exited with status %d\n", status);
    g_spawn_close_pid(pid);
    storage->gpu_exited = 1;
}

// 显存进程退出回调
static void vram_process_exited(GPid pid, gint status, gpointer data) {
    struct intel_gpu_storage *storage = (struct intel_gpu_storage *)data;

    g_print("VRAM process exited with status %d\n", status);
    g_spawn_close_pid(pid);
    storage->vram_exited = 1;
}

// 使用GLib启动工具进程并创建管道
static gboolean
start_tool_with_glib(const char *tool_path, GPid *pid, int *read_fd, GChildWatchFunc watch_func, gpointer user_data) {
    GError *error = NULL;
    gboolean success;

    success = g_spawn_async_with_pipes(
        NULL,                                  // working directory
        (gchar *[]){(gchar *)tool_path, NULL}, // argv
        NULL,                                  // envp
        G_SPAWN_SEARCH_PATH |                  // flags
            G_SPAWN_DO_NOT_REAP_CHILD,         // 需要手动监控进程
        NULL, NULL,                            // child setup function
        pid,                                   // child pid
        NULL,                                  // stdin
        read_fd,                               // stdout
        NULL,                                  // stderr
        &error
    );

    if (success) {
        // 添加子进程监控
        g_child_watch_add(*pid, watch_func, user_data);
    } else {
        g_printerr("Failed to spawn %s: %s\n", tool_path, error->message);
        g_error_free(error);
    }

    return success;
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
        perror("read");
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
        perror("read");
    }
}

// 更新函数：处理管道数据并更新显示
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
        // 使用与CPU模块相同的格式
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
        modules[module_id].interval = modules[module_id].state + 1;
        break;
    default:
        return;
    }
    modules[module_id].update(module_id);
}

// 删除函数：清理资源
static void del(size_t module_id) {
    struct intel_gpu_storage *storage = (struct intel_gpu_storage *)modules[module_id].data;

    // 关闭管道
    if (storage->gpu_usage_fd >= 0)
        close(storage->gpu_usage_fd);
    if (storage->vram_fd >= 0)
        close(storage->vram_fd);

    // 终止子进程
    if (storage->gpu_usage_pid > 0 && !storage->gpu_exited) {
        kill(storage->gpu_usage_pid, SIGTERM);
        g_spawn_close_pid(storage->gpu_usage_pid);
    }
    if (storage->vram_pid > 0 && !storage->vram_exited) {
        kill(storage->vram_pid, SIGTERM);
        g_spawn_close_pid(storage->vram_pid);
    }

    g_free(storage);
}

// 初始化Intel GPU模块
void init_intel_gpu(int epoll_fd) {
    INIT_BASE;

    // 创建存储结构体
    struct intel_gpu_storage *storage = g_new0(struct intel_gpu_storage, 1);
    if (!storage) {
        perror("malloc");
        modules_cnt--;
        return;
    }

    // 初始化存储结构体
    storage->gpu_usage_fd = -1;
    storage->vram_fd = -1;
    storage->gpu_usage_pid = 0;
    storage->vram_pid = 0;
    storage->gpu_usage = 0.0;
    storage->vram_usage = 0;
    storage->gpu_usage_ready = 0;
    storage->vram_ready = 0;
    storage->gpu_exited = 0;
    storage->vram_exited = 0;

    // 启动GPU使用率工具
    if (!start_tool_with_glib(
            GPU_USAGE_TOOL, &storage->gpu_usage_pid, &storage->gpu_usage_fd, gpu_usage_process_exited, storage
        )) {
        fprintf(stderr, "Failed to start %s\n", GPU_USAGE_TOOL);
        g_free(storage);
        modules_cnt--;
        return;
    }

    // 启动显存使用工具
    if (!start_tool_with_glib(VRAM_TOOL, &storage->vram_pid, &storage->vram_fd, vram_process_exited, storage)) {
        fprintf(stderr, "Failed to start %s\n", VRAM_TOOL);
        close(storage->gpu_usage_fd);
        if (storage->gpu_usage_pid > 0) {
            kill(storage->gpu_usage_pid, SIGTERM);
            g_spawn_close_pid(storage->gpu_usage_pid);
        }
        g_free(storage);
        modules_cnt--;
        return;
    }

    // 设置管道为非阻塞模式
    int flags = fcntl(storage->gpu_usage_fd, F_GETFL, 0);
    fcntl(storage->gpu_usage_fd, F_SETFL, flags | O_NONBLOCK);

    flags = fcntl(storage->vram_fd, F_GETFL, 0);
    fcntl(storage->vram_fd, F_SETFL, flags | O_NONBLOCK);

    // 将GPU使用率管道添加到epoll
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET; // 边缘触发模式
    ev.data.u64 = module_id;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, storage->gpu_usage_fd, &ev) == -1) {
        perror("epoll_ctl gpu_usage_fd");
        close(storage->gpu_usage_fd);
        close(storage->vram_fd);
        if (storage->gpu_usage_pid > 0) {
            kill(storage->gpu_usage_pid, SIGTERM);
            g_spawn_close_pid(storage->gpu_usage_pid);
        }
        if (storage->vram_pid > 0) {
            kill(storage->vram_pid, SIGTERM);
            g_spawn_close_pid(storage->vram_pid);
        }
        g_free(storage);
        modules_cnt--;
        return;
    }

    // 将显存管道添加到epoll
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, storage->vram_fd, &ev) == -1) {
        perror("epoll_ctl vram_fd");
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, storage->gpu_usage_fd, NULL);
        close(storage->gpu_usage_fd);
        close(storage->vram_fd);
        if (storage->gpu_usage_pid > 0) {
            kill(storage->gpu_usage_pid, SIGTERM);
            g_spawn_close_pid(storage->gpu_usage_pid);
        }
        if (storage->vram_pid > 0) {
            kill(storage->vram_pid, SIGTERM);
            g_spawn_close_pid(storage->vram_pid);
        }
        g_free(storage);
        modules_cnt--;
        return;
    }

    // 设置模块回调函数
    modules[module_id].data = storage;
    modules[module_id].update = update;
    modules[module_id].alter = alter;
    modules[module_id].del = del;
    modules[module_id].interval = 1; // 每秒更新一次

    // 立即更新一次
    UPDATE_Q(module_id);
}
