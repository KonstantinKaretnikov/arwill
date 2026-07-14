#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/process.h>

static void clear_process(struct arwill_process *process) {
    process->pid = 0;
    process->name = "";
    process->kind = arwill_process_kind_kernel;
    process->state = arwill_process_state_empty;
    process->exit_code = 0;
    process->run_count = 0;
    process->entry = 0;
    process->context = 0;
    process->saved_context.stack_pointer = 0;
    process->saved_context.entry = 0;
    process->saved_context.argument = 0;
    process->manager = 0;
}

void arwill_process_manager_init(
    struct arwill_process_manager *manager,
    const struct arwill_process_context_backend *context_backend
) {
    if (manager == 0) {
        return;
    }

    for (size_t index = 0; index < arwill_process_table_capacity; index++) {
        clear_process(&manager->table[index]);
    }

    manager->scheduler_context = 0;
    manager->context_backend = context_backend;
    manager->current = 0;
    manager->next_pid = 1;
}

static void process_trampoline(void *argument) {
    struct arwill_process *process = (struct arwill_process *)argument;

    if (process == 0 || process->manager == 0) {
        for (;;) {
        }
    }

    struct arwill_process_manager *manager = process->manager;
    struct arwill_process_runtime runtime;

    runtime.pid = process->pid;
    runtime.name = process->name;
    runtime.run_count = process->run_count - 1U;
    runtime.context = process->context;
    runtime.manager = manager;

    const struct arwill_process_result result = process->entry(&runtime);

    process->exit_code = result.exit_code;
    process->state = arwill_process_state_finished;
    if (manager->scheduler_context == 0) {
        for (;;) {
        }
    }
    manager->current = 0;
    manager->context_backend->switch_context(
        &process->saved_context,
        manager->scheduler_context
    );

    for (;;) {
    }
}

static int find_spawn_slot(struct arwill_process_manager *manager, size_t *slot) {
    for (size_t index = 0; index < arwill_process_table_capacity; index++) {
        if (manager->table[index].state == arwill_process_state_empty) {
            *slot = index;
            return 1;
        }
    }

    for (size_t index = 0; index < arwill_process_table_capacity; index++) {
        if (manager->table[index].state == arwill_process_state_finished) {
            *slot = index;
            return 1;
        }
    }

    return 0;
}

static int spawn_process(
    struct arwill_process_manager *manager,
    const char *name,
    enum arwill_process_kind kind,
    arwill_process_entry entry,
    void *context,
    uint32_t *pid
) {
    if (
        manager == 0 || manager->context_backend == 0 ||
        manager->context_backend->initialize == 0 ||
        manager->context_backend->switch_context == 0 || name == 0 ||
        entry == 0 || pid == 0
    ) {
        return 0;
    }

    size_t slot = 0;

    if (!find_spawn_slot(manager, &slot)) {
        return 0;
    }

    const uint32_t new_pid = manager->next_pid;
    manager->next_pid++;

    if (manager->next_pid == 0U) {
        manager->next_pid = 1;
    }

    manager->table[slot].pid = new_pid;
    manager->table[slot].name = name;
    manager->table[slot].kind = kind;
    manager->table[slot].state = arwill_process_state_ready;
    manager->table[slot].exit_code = 0;
    manager->table[slot].run_count = 0;
    manager->table[slot].entry = entry;
    manager->table[slot].context = context;
    manager->table[slot].manager = manager;

    if (!manager->context_backend->initialize(
        &manager->table[slot].saved_context,
        manager->stacks[slot],
        arwill_process_stack_capacity,
        process_trampoline,
        &manager->table[slot]
    )) {
        clear_process(&manager->table[slot]);
        return 0;
    }

    *pid = new_pid;
    return 1;
}

int arwill_process_spawn(
    struct arwill_process_manager *manager,
    const char *name,
    arwill_process_entry entry,
    void *context,
    uint32_t *pid
) {
    return spawn_process(
        manager, name, arwill_process_kind_kernel, entry, context, pid
    );
}

int arwill_process_spawn_system(
    struct arwill_process_manager *manager,
    const char *name,
    arwill_process_entry entry,
    void *context,
    uint32_t *pid
) {
    return spawn_process(
        manager, name, arwill_process_kind_system, entry, context, pid
    );
}

struct arwill_process_result arwill_process_finish(uint32_t exit_code) {
    struct arwill_process_result result;

    result.exit_code = exit_code;
    return result;
}

void arwill_process_yield(const struct arwill_process_runtime *runtime) {
    if (runtime == 0 || runtime->manager == 0) {
        return;
    }

    struct arwill_process_manager *manager = runtime->manager;
    struct arwill_process *process = manager->current;

    if (
        process == 0 || process->pid != runtime->pid ||
        manager->scheduler_context == 0 || manager->context_backend == 0 ||
        manager->context_backend->switch_context == 0
    ) {
        return;
    }

    process->state = arwill_process_state_ready;
    manager->current = 0;
    manager->context_backend->switch_context(
        &process->saved_context,
        manager->scheduler_context
    );

    manager->current = process;
    process->state = arwill_process_state_running;
}

static size_t run_ready_kind(
    struct arwill_process_manager *manager,
    enum arwill_process_kind kind
) {
    size_t run_count = 0;

    if (
        manager == 0 || manager->context_backend == 0 ||
        manager->context_backend->switch_context == 0
    ) {
        return 0;
    }

    struct arwill_process *previous_process = manager->current;
    struct arwill_process_context *previous_scheduler =
        manager->scheduler_context;

    for (size_t index = 0; index < arwill_process_table_capacity; index++) {
        struct arwill_process *process = &manager->table[index];

        if (
            process->state != arwill_process_state_ready ||
            process->kind != kind
        ) {
            continue;
        }

        struct arwill_process_context scheduler_context;
        scheduler_context.stack_pointer = 0;
        scheduler_context.entry = 0;
        scheduler_context.argument = 0;

        process->state = arwill_process_state_running;
        process->run_count++;
        manager->current = process;
        manager->scheduler_context = &scheduler_context;
        manager->context_backend->switch_context(
            &scheduler_context,
            &process->saved_context
        );
        manager->current = previous_process;
        manager->scheduler_context = previous_scheduler;
        run_count++;
    }

    return run_count;
}

size_t arwill_process_run_ready(struct arwill_process_manager *manager) {
    return run_ready_kind(manager, arwill_process_kind_kernel);
}

size_t arwill_process_run_system(struct arwill_process_manager *manager) {
    return run_ready_kind(manager, arwill_process_kind_system);
}

const struct arwill_process *arwill_process_table(
    const struct arwill_process_manager *manager
) {
    if (manager == 0) {
        return 0;
    }

    return manager->table;
}

const char *arwill_process_state_name(enum arwill_process_state state) {
    switch (state) {
        case arwill_process_state_empty:
            return "empty";
        case arwill_process_state_ready:
            return "ready";
        case arwill_process_state_running:
            return "running";
        case arwill_process_state_finished:
            return "finished";
        default:
            return "unknown";
    }
}

const char *arwill_process_kind_name(enum arwill_process_kind kind) {
    switch (kind) {
        case arwill_process_kind_kernel:
            return "kernel";
        case arwill_process_kind_system:
            return "system";
        default:
            return "unknown";
    }
}
