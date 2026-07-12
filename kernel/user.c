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

const char *arwill_user_program_name(enum arwill_user_program program) {
    switch (program) {
        case arwill_user_program_hello:
            return "userhello";
        case arwill_user_program_bad_syscall:
            return "userbad";
    }

    return "unknown";
}
