#ifndef ARWILL_KERNEL_USER_H
#define ARWILL_KERNEL_USER_H

#include <stdint.h>
#include <stddef.h>

#include <arwill/kernel/console.h>

enum arwill_user_program {
    arwill_user_program_hello,
    arwill_user_program_bad_syscall
};

struct arwill_user_program_result {
    int started;
    int exited;
    uint32_t exit_code;
    uint64_t syscall_count;
    uint64_t bytes_written;
    const char *status;
};

struct arwill_user_stats {
    int available;
    int hhdm_available;
    int gdt_loaded;
    int tss_loaded;
    int syscall_gate_loaded;
    uint64_t runs;
    uint64_t syscall_count;
    uint64_t bytes_written;
    uint64_t bad_syscalls;
    uint64_t preemptions;
    uint64_t faults;
};

enum {
    arwill_user_task_capacity = 4,
    arwill_user_task_name_capacity = 48,
    arwill_user_argument_capacity = 64
};

enum arwill_user_task_state {
    arwill_user_task_empty,
    arwill_user_task_ready,
    arwill_user_task_running,
    arwill_user_task_blocked_input,
    arwill_user_task_finished,
    arwill_user_task_faulted
};

struct arwill_user_task_info {
    uint32_t pid;
    const char *name;
    enum arwill_user_task_state state;
    uint32_t exit_code;
    uint8_t fault_vector;
    uint64_t run_count;
};

struct arwill_user_runtime {
    void *context;
    const char *name;
    int (*run)(
        void *context,
        enum arwill_user_program program,
        const struct arwill_console *console,
        struct arwill_user_program_result *result
    );
    int (*run_image)(
        void *context,
        const uint8_t *image,
        uint64_t image_size,
        const struct arwill_console *console,
        struct arwill_user_program_result *result
    );
    int (*spawn_image)(
        void *context,
        const uint8_t *image,
        uint64_t image_size,
        const char *name,
        const char *argument,
        const struct arwill_console *console,
        uint32_t *pid
    );
    void (*poll)(void *context);
    int (*deliver_input)(void *context, uint32_t pid, uint8_t byte);
    int (*cancel)(void *context, uint32_t pid, uint32_t exit_code);
    int (*task_info)(void *context, uint32_t pid, struct arwill_user_task_info *info);
    size_t (*tasks)(void *context, struct arwill_user_task_info *tasks, size_t capacity);
    void (*stats)(void *context, struct arwill_user_stats *stats);
};

void arwill_user_runtime_stats(
    const struct arwill_user_runtime *runtime,
    struct arwill_user_stats *stats
);

int arwill_user_run_program(
    const struct arwill_user_runtime *runtime,
    enum arwill_user_program program,
    const struct arwill_console *console,
    struct arwill_user_program_result *result
);

int arwill_user_run_image(
    const struct arwill_user_runtime *runtime,
    const uint8_t *image,
    uint64_t image_size,
    const struct arwill_console *console,
    struct arwill_user_program_result *result
);

int arwill_user_spawn_image(
    const struct arwill_user_runtime *runtime,
    const uint8_t *image,
    uint64_t image_size,
    const char *name,
    const char *argument,
    const struct arwill_console *console,
    uint32_t *pid
);

void arwill_user_poll(const struct arwill_user_runtime *runtime);

int arwill_user_deliver_input(
    const struct arwill_user_runtime *runtime,
    uint32_t pid,
    uint8_t byte
);

int arwill_user_cancel(
    const struct arwill_user_runtime *runtime,
    uint32_t pid,
    uint32_t exit_code
);

int arwill_user_task_info(
    const struct arwill_user_runtime *runtime,
    uint32_t pid,
    struct arwill_user_task_info *info
);

size_t arwill_user_tasks(
    const struct arwill_user_runtime *runtime,
    struct arwill_user_task_info *tasks,
    size_t capacity
);

const char *arwill_user_task_state_name(enum arwill_user_task_state state);

const char *arwill_user_program_name(enum arwill_user_program program);

#endif
