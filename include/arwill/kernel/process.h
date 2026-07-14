#ifndef ARWILL_KERNEL_PROCESS_H
#define ARWILL_KERNEL_PROCESS_H

#include <stddef.h>
#include <stdint.h>

enum {
    arwill_process_table_capacity = 8,
    arwill_process_stack_capacity = 8192
};

typedef void (*arwill_process_context_entry)(void *argument);

struct arwill_process_context {
    uintptr_t stack_pointer;
    arwill_process_context_entry entry;
    void *argument;
};

struct arwill_process_context_backend {
    int (*initialize)(
        struct arwill_process_context *context,
        uint8_t *stack,
        size_t stack_size,
        arwill_process_context_entry entry,
        void *argument
    );
    void (*switch_context)(
        struct arwill_process_context *from,
        struct arwill_process_context *to
    );
};

struct arwill_process_manager;

enum arwill_process_state {
    arwill_process_state_empty,
    arwill_process_state_ready,
    arwill_process_state_running,
    arwill_process_state_finished
};

enum arwill_process_kind {
    arwill_process_kind_kernel,
    arwill_process_kind_system
};

struct arwill_process_runtime {
    uint32_t pid;
    const char *name;
    uint64_t run_count;
    void *context;
    struct arwill_process_manager *manager;
};

struct arwill_process_result {
    uint32_t exit_code;
};

typedef struct arwill_process_result (*arwill_process_entry)(
    const struct arwill_process_runtime *runtime
);

struct arwill_process {
    uint32_t pid;
    const char *name;
    enum arwill_process_kind kind;
    enum arwill_process_state state;
    uint32_t exit_code;
    uint64_t run_count;
    arwill_process_entry entry;
    void *context;
    struct arwill_process_context saved_context;
    struct arwill_process_manager *manager;
};

struct arwill_process_manager {
    struct arwill_process table[arwill_process_table_capacity];
    _Alignas(16) uint8_t stacks
        [arwill_process_table_capacity][arwill_process_stack_capacity];
    struct arwill_process_context *scheduler_context;
    const struct arwill_process_context_backend *context_backend;
    struct arwill_process *current;
    uint32_t next_pid;
};

void arwill_process_manager_init(
    struct arwill_process_manager *manager,
    const struct arwill_process_context_backend *context_backend
);

int arwill_process_spawn(
    struct arwill_process_manager *manager,
    const char *name,
    arwill_process_entry entry,
    void *context,
    uint32_t *pid
);

int arwill_process_spawn_system(
    struct arwill_process_manager *manager,
    const char *name,
    arwill_process_entry entry,
    void *context,
    uint32_t *pid
);

size_t arwill_process_run_ready(struct arwill_process_manager *manager);

size_t arwill_process_run_system(struct arwill_process_manager *manager);

struct arwill_process_result arwill_process_finish(uint32_t exit_code);

void arwill_process_yield(const struct arwill_process_runtime *runtime);

const struct arwill_process *arwill_process_table(
    const struct arwill_process_manager *manager
);

const char *arwill_process_state_name(enum arwill_process_state state);

const char *arwill_process_kind_name(enum arwill_process_kind kind);

#endif
