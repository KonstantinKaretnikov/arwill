#include <arwill/kernel/user.h>

static void clear_result(struct arwill_user_program_result *result) {
    if (result == 0) {
        return;
    }

    result->started = 0;
    result->exited = 0;
    result->exit_code = 0;
    result->syscall_count = 0;
    result->bytes_written = 0;
    result->status = "unavailable";
}

void arwill_user_runtime_stats(
    const struct arwill_user_runtime *runtime,
    struct arwill_user_stats *stats
) {
    if (stats == 0) {
        return;
    }

    stats->available = 0;
    stats->hhdm_available = 0;
    stats->gdt_loaded = 0;
    stats->tss_loaded = 0;
    stats->syscall_gate_loaded = 0;
    stats->runs = 0;
    stats->syscall_count = 0;
    stats->bytes_written = 0;
    stats->bad_syscalls = 0;
    stats->preemptions = 0;
    stats->faults = 0;

    if (runtime == 0 || runtime->stats == 0) {
        return;
    }

    runtime->stats(runtime->context, stats);
}

int arwill_user_run_program(
    const struct arwill_user_runtime *runtime,
    enum arwill_user_program program,
    const struct arwill_console *console,
    struct arwill_user_program_result *result
) {
    clear_result(result);

    if (runtime == 0 || runtime->run == 0) {
        return 0;
    }

    return runtime->run(runtime->context, program, console, result);
}

int arwill_user_run_image(
    const struct arwill_user_runtime *runtime,
    const uint8_t *image,
    uint64_t image_size,
    const struct arwill_console *console,
    struct arwill_user_program_result *result
) {
    clear_result(result);

    if (runtime == 0 || runtime->run_image == 0 || image == 0) {
        return 0;
    }

    return runtime->run_image(runtime->context, image, image_size, console, result);
}

int arwill_user_spawn_image(
    const struct arwill_user_runtime *runtime,
    const uint8_t *image,
    uint64_t image_size,
    const char *name,
    const char *argument,
    const struct arwill_console *console,
    uint32_t *pid
) {
    if (runtime == 0 || runtime->spawn_image == 0 || image == 0 || name == 0 ||
        argument == 0 || console == 0 || pid == 0) {
        return 0;
    }

    return runtime->spawn_image(
        runtime->context, image, image_size, name, argument, console, pid
    );
}

void arwill_user_poll(const struct arwill_user_runtime *runtime) {
    if (runtime != 0 && runtime->poll != 0) {
        runtime->poll(runtime->context);
    }
}

int arwill_user_deliver_input(
    const struct arwill_user_runtime *runtime,
    uint32_t pid,
    uint8_t byte
) {
    return runtime != 0 && runtime->deliver_input != 0 &&
        runtime->deliver_input(runtime->context, pid, byte);
}

int arwill_user_cancel(
    const struct arwill_user_runtime *runtime,
    uint32_t pid,
    uint32_t exit_code
) {
    return runtime != 0 && runtime->cancel != 0 &&
        runtime->cancel(runtime->context, pid, exit_code);
}

int arwill_user_task_info(
    const struct arwill_user_runtime *runtime,
    uint32_t pid,
    struct arwill_user_task_info *info
) {
    if (info != 0) {
        info->pid = 0;
        info->name = "";
        info->state = arwill_user_task_empty;
        info->exit_code = 0;
        info->fault_vector = 0;
        info->run_count = 0;
    }

    return runtime != 0 && runtime->task_info != 0 && info != 0 &&
        runtime->task_info(runtime->context, pid, info);
}

size_t arwill_user_tasks(
    const struct arwill_user_runtime *runtime,
    struct arwill_user_task_info *tasks,
    size_t capacity
) {
    if (runtime == 0 || runtime->tasks == 0 || tasks == 0 || capacity == 0U) {
        return 0;
    }

    return runtime->tasks(runtime->context, tasks, capacity);
}

const char *arwill_user_task_state_name(enum arwill_user_task_state state) {
    switch (state) {
        case arwill_user_task_empty:
            return "empty";
        case arwill_user_task_ready:
            return "ready";
        case arwill_user_task_running:
            return "running";
        case arwill_user_task_blocked_input:
            return "blocked-input";
        case arwill_user_task_finished:
            return "finished";
        case arwill_user_task_faulted:
            return "faulted";
    }

    return "unknown";
}

const char *arwill_user_program_name(enum arwill_user_program program) {
    switch (program) {
        case arwill_user_program_hello:
            return "userhello";
        case arwill_user_program_bad_syscall:
            return "userbad";
    }

    return "unknown";
}
