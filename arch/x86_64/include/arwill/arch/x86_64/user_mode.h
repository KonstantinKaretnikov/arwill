#ifndef ARWILL_ARCH_X86_64_USER_MODE_H
#define ARWILL_ARCH_X86_64_USER_MODE_H

#include <stdint.h>

#include <arwill/kernel/memory.h>
#include <arwill/kernel/input.h>
#include <arwill/kernel/clock.h>
#include <arwill/kernel/filesystem.h>
#include <arwill/kernel/user.h>

struct arwill_x86_64_user_registers {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
};

struct arwill_x86_64_user_frame {
    uint64_t instruction_pointer;
    uint64_t code_segment;
    uint64_t cpu_flags;
    uint64_t stack_pointer;
    uint64_t stack_segment;
};

struct arwill_x86_64_user_saved_context {
    struct arwill_x86_64_user_registers registers;
    struct arwill_x86_64_user_frame frame;
};

const struct arwill_user_runtime *arwill_x86_64_user_mode_init(
    struct arwill_memory *memory,
    uint64_t hhdm_offset,
    const struct arwill_input *input,
    const struct arwill_clock *clock,
    const struct arwill_filesystem *filesystem
);

void arwill_x86_64_user_resume(
    const struct arwill_x86_64_user_saved_context *context,
    uint64_t cr3,
    uint64_t *kernel_rsp
);

void arwill_x86_64_user_syscall_entry(void);
void arwill_x86_64_user_timer_entry(void);
void arwill_x86_64_user_invalid_opcode_entry(void);
void arwill_x86_64_user_general_protection_entry(void);
void arwill_x86_64_user_page_fault_entry(void);
void arwill_x86_64_user_return_to_kernel(void);

int arwill_x86_64_user_handle_timer(
    const struct arwill_x86_64_user_registers *registers,
    const struct arwill_x86_64_user_frame *frame
);

int arwill_x86_64_user_handle_fault(
    const struct arwill_x86_64_user_registers *registers,
    const struct arwill_x86_64_user_frame *frame,
    uint8_t vector,
    uint64_t error_code
);

uint64_t arwill_x86_64_user_kernel_rsp(void);
uint64_t arwill_x86_64_user_kernel_cr3(void);

void arwill_x86_64_user_mode_mark_syscall_gate_loaded(void);

#endif
