#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/process.h>

static void clear_process(struct arwill_process *process) {
    process->pid = 0;
    process->name = "";
    process->state = arwill_process_state_empty;
    process->exit_code = 0;
    process->run_count = 0;
    process->entry = 0;
    process->context = 0;
}

void arwill_process_manager_init(struct arwill_process_manager *manager) {
    if (manager == 0) {
        return;
    }

    for (size_t index = 0; index < arwill_process_table_capacity; index++) {
        clear_process(&manager->table[index]);
    }

    manager->next_pid = 1;
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

int arwill_process_spawn(
    struct arwill_process_manager *manager,
    const char *name,
    arwill_process_entry entry,
    void *context,
    uint32_t *pid
) {
    if (manager == 0 || name == 0 || entry == 0 || pid == 0) {
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
    manager->table[slot].state = arwill_process_state_ready;
    manager->table[slot].exit_code = 0;
    manager->table[slot].run_count = 0;
    manager->table[slot].entry = entry;
    manager->table[slot].context = context;

    *pid = new_pid;
    return 1;
}

struct arwill_process_result arwill_process_finish(uint32_t exit_code) {
    struct arwill_process_result result;

    result.state = arwill_process_result_finished;
    result.exit_code = exit_code;
    return result;
}

struct arwill_process_result arwill_process_yield(void) {
    struct arwill_process_result result;

    result.state = arwill_process_result_yielded;
    result.exit_code = 0;
    return result;
}

size_t arwill_process_run_ready(struct arwill_process_manager *manager) {
    size_t run_count = 0;

    if (manager == 0) {
        return 0;
    }

    for (size_t index = 0; index < arwill_process_table_capacity; index++) {
        struct arwill_process *process = &manager->table[index];

        if (process->state != arwill_process_state_ready) {
            continue;
        }

        struct arwill_process_runtime runtime;

        runtime.pid = process->pid;
        runtime.name = process->name;
        runtime.run_count = process->run_count;
        runtime.context = process->context;

        process->state = arwill_process_state_running;
        process->run_count++;

        const struct arwill_process_result result = process->entry(&runtime);

        process->exit_code = result.exit_code;
        if (result.state == arwill_process_result_yielded) {
            process->state = arwill_process_state_ready;
        } else {
            process->state = arwill_process_state_finished;
        }
        run_count++;
    }

    return run_count;
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
