#ifndef ARWILL_KERNEL_USER_H
#define ARWILL_KERNEL_USER_H

#include <stdint.h>

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

const char *arwill_user_program_name(enum arwill_user_program program);

#endif
