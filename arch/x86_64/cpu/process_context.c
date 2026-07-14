#include <stddef.h>
#include <stdint.h>

#include <arwill/arch/x86_64/process_context.h>
#include <arwill/kernel/process.h>

extern const uint8_t arwill_x86_64_process_context_bootstrap[];

void arwill_x86_64_process_context_switch_assembly(
    struct arwill_process_context *from,
    struct arwill_process_context *to
);

void arwill_x86_64_process_context_start(
    struct arwill_process_context *context
);

__asm__(
    ".text\n"
    ".global arwill_x86_64_process_context_switch_assembly\n"
    ".type arwill_x86_64_process_context_switch_assembly,@function\n"
    "arwill_x86_64_process_context_switch_assembly:\n"
    "pushq %rbp\n"
    "pushq %rbx\n"
    "pushq %r12\n"
    "pushq %r13\n"
    "pushq %r14\n"
    "pushq %r15\n"
    "movq %rsp, 0(%rdi)\n"
    "movq 0(%rsi), %rsp\n"
    "popq %r15\n"
    "popq %r14\n"
    "popq %r13\n"
    "popq %r12\n"
    "popq %rbx\n"
    "popq %rbp\n"
    "retq\n"
    ".size arwill_x86_64_process_context_switch_assembly, .-arwill_x86_64_process_context_switch_assembly\n"
    ".global arwill_x86_64_process_context_bootstrap\n"
    ".type arwill_x86_64_process_context_bootstrap,@function\n"
    "arwill_x86_64_process_context_bootstrap:\n"
    "movq %r13, %rdi\n"
    "call arwill_x86_64_process_context_start\n"
    "ud2\n"
    ".size arwill_x86_64_process_context_bootstrap, .-arwill_x86_64_process_context_bootstrap\n"
);

void arwill_x86_64_process_context_start(
    struct arwill_process_context *context
) {
    context->entry(context->argument);
}

static int initialize_context(
    struct arwill_process_context *context,
    uint8_t *stack,
    size_t stack_size,
    arwill_process_context_entry entry,
    void *argument
) {
    if (
        context == 0 || stack == 0 || entry == 0 ||
        stack_size < (7U * sizeof(uintptr_t))
    ) {
        return 0;
    }

    uintptr_t stack_top = (uintptr_t)(stack + stack_size);
    stack_top &= ~(uintptr_t)0x0fU;
    uintptr_t *frame = (uintptr_t *)(stack_top - (7U * sizeof(uintptr_t)));

    frame[0] = 0;
    frame[1] = 0;
    frame[2] = (uintptr_t)context;
    frame[3] = 0;
    frame[4] = 0;
    frame[5] = 0;
    frame[6] = (uintptr_t)&arwill_x86_64_process_context_bootstrap[0];

    context->stack_pointer = (uintptr_t)frame;
    context->entry = entry;
    context->argument = argument;
    return 1;
}

static void switch_context(
    struct arwill_process_context *from,
    struct arwill_process_context *to
) {
    arwill_x86_64_process_context_switch_assembly(from, to);
}

const struct arwill_process_context_backend *
arwill_x86_64_process_context_backend(void) {
    static const struct arwill_process_context_backend backend = {
        .initialize = initialize_context,
        .switch_context = switch_context
    };

    return &backend;
}
