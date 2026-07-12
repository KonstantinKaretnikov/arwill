#ifndef ARWILL_ARCH_X86_64_USER_MODE_H
#define ARWILL_ARCH_X86_64_USER_MODE_H

#include <stdint.h>

#include <arwill/kernel/memory.h>
#include <arwill/kernel/user.h>

struct arwill_x86_64_user_run_state {
    uint64_t kernel_rsp;
};

const struct arwill_user_runtime *arwill_x86_64_user_mode_init(
    struct arwill_memory *memory,
    uint64_t hhdm_offset
);

void arwill_x86_64_user_enter(
    uint64_t entry_point,
    uint64_t stack_pointer,
    struct arwill_x86_64_user_run_state *state
);

void arwill_x86_64_user_syscall_entry(void);

void arwill_x86_64_user_mode_mark_syscall_gate_loaded(void);

#endif
