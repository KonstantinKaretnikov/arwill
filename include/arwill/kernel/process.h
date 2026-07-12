#ifndef ARWILL_KERNEL_PROCESS_H
#define ARWILL_KERNEL_PROCESS_H

#include <stddef.h>
#include <stdint.h>

enum {
    arwill_process_table_capacity = 8
};

enum arwill_process_state {
    arwill_process_state_empty,
    arwill_process_state_ready,
    arwill_process_state_running,
    arwill_process_state_finished
};

struct arwill_process_runtime {
    uint32_t pid;
    const char *name;
    uint64_t run_count;
    void *context;
};

enum arwill_process_result_state {
    arwill_process_result_finished,
    arwill_process_result_yielded
};

struct arwill_process_result {
    enum arwill_process_result_state state;
    uint32_t exit_code;
};

typedef struct arwill_process_result (*arwill_process_entry)(
    const struct arwill_process_runtime *runtime
);

struct arwill_process {
    uint32_t pid;
    const char *name;
    enum arwill_process_state state;
    uint32_t exit_code;
    uint64_t run_count;
    arwill_process_entry entry;
    void *context;
};

struct arwill_process_manager {
    struct arwill_process table[arwill_process_table_capacity];
    uint32_t next_pid;
};

void arwill_process_manager_init(struct arwill_process_manager *manager);

int arwill_process_spawn(
    struct arwill_process_manager *manager,
    const char *name,
    arwill_process_entry entry,
    void *context,
    uint32_t *pid
);

size_t arwill_process_run_ready(struct arwill_process_manager *manager);

struct arwill_process_result arwill_process_finish(uint32_t exit_code);

struct arwill_process_result arwill_process_yield(void);

const struct arwill_process *arwill_process_table(
    const struct arwill_process_manager *manager
);

const char *arwill_process_state_name(enum arwill_process_state state);

#endif
