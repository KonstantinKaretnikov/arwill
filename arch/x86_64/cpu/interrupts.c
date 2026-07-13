#include <stddef.h>
#include <stdint.h>

#include <arwill/arch/x86_64/interrupts.h>
#include <arwill/arch/x86_64/io.h>
#include <arwill/arch/x86_64/user_mode.h>
#include <arwill/kernel/interrupts.h>
#include <arwill/kernel/clock.h>
#include <arwill/kernel/scheduler.h>

enum {
    idt_entry_count = 256,
    idt_gate_interrupt = 0x8e,
    /* Trap gate preserves IF across the user syscall exit-path retq. */
    idt_gate_user_interrupt = 0xef,
    vector_breakpoint = 3,
    vector_invalid_opcode = 6,
    vector_general_protection = 13,
    vector_page_fault = 14,
    vector_irq0_timer = 32,
    vector_syscall = 0x80,
    pic1_command = 0x20,
    pic1_data = 0x21,
    pic2_command = 0xa0,
    pic2_data = 0xa1,
    pic_eoi = 0x20,
    pic_icw1_init = 0x11,
    pic_icw4_8086 = 0x01,
    pic_master_offset = 0x20,
    pic_slave_offset = 0x28,
    pit_channel0 = 0x40,
    pit_command = 0x43,
    pit_square_wave = 0x36,
    pit_frequency = 1193182,
    timer_hz = 100
};

struct interrupt_frame {
    uint64_t instruction_pointer;
    uint64_t code_segment;
    uint64_t cpu_flags;
    uint64_t stack_pointer;
    uint64_t stack_segment;
};

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct x86_64_interrupt_context {
    int idt_loaded;
    int pic_remapped;
    int timer_configured;
    int enabled;
    volatile uint64_t timer_ticks;
    volatile uint64_t exception_count;
    volatile uint8_t last_exception_vector;
};

static struct idt_entry idt[idt_entry_count];
static struct x86_64_interrupt_context interrupt_context;

static uint64_t pit_clock_monotonic_milliseconds(void *context) {
    const struct x86_64_interrupt_context *interrupts =
        (const struct x86_64_interrupt_context *)context;

    if (interrupts == 0) {
        return 0;
    }

    const uint64_t ticks = interrupts->timer_ticks;
    const uint64_t seconds = ticks / timer_hz;

    if (seconds > UINT64_MAX / 1000U) {
        return UINT64_MAX;
    }

    return (seconds * 1000U) +
        (((ticks % timer_hz) * 1000U) / timer_hz);
}

static const struct arwill_clock pit_clock = {
    .name = "x86_64 pit monotonic",
    .context = &interrupt_context,
    .monotonic_milliseconds = pit_clock_monotonic_milliseconds,
};

static uint16_t read_code_segment(void) {
    uint16_t segment = 0;

    __asm__ volatile("mov %%cs, %0" : "=r"(segment));
    return segment;
}

static void load_idt(const struct idt_pointer *pointer) {
    __asm__ volatile("lidt (%0)" : : "r"(pointer) : "memory");
}

static void enable_cpu_interrupts(void) {
    __asm__ volatile("sti" : : : "memory");
}

static void disable_cpu_interrupts(void) {
    __asm__ volatile("cli" : : : "memory");
}

static void io_wait(void) {
    arwill_x86_64_out8(0x80, 0);
}

static void idt_set_gate(uint8_t vector, uint8_t attributes, void (*handler)(void)) {
    const uint64_t address = (uint64_t)handler;

    idt[vector].offset_low = (uint16_t)(address & 0xffffU);
    idt[vector].selector = read_code_segment();
    idt[vector].ist = 0;
    idt[vector].attributes = attributes;
    idt[vector].offset_mid = (uint16_t)((address >> 16U) & 0xffffU);
    idt[vector].offset_high = (uint32_t)(address >> 32U);
    idt[vector].zero = 0;
}

static void pic_remap_timer_only(void) {
    arwill_x86_64_out8(pic1_command, pic_icw1_init);
    io_wait();
    arwill_x86_64_out8(pic2_command, pic_icw1_init);
    io_wait();
    arwill_x86_64_out8(pic1_data, pic_master_offset);
    io_wait();
    arwill_x86_64_out8(pic2_data, pic_slave_offset);
    io_wait();
    arwill_x86_64_out8(pic1_data, 0x04);
    io_wait();
    arwill_x86_64_out8(pic2_data, 0x02);
    io_wait();
    arwill_x86_64_out8(pic1_data, pic_icw4_8086);
    io_wait();
    arwill_x86_64_out8(pic2_data, pic_icw4_8086);
    io_wait();
    arwill_x86_64_out8(pic1_data, 0xfe);
    arwill_x86_64_out8(pic2_data, 0xff);
}

static void pit_configure_timer(void) {
    const uint16_t divisor = (uint16_t)(pit_frequency / timer_hz);

    arwill_x86_64_out8(pit_command, pit_square_wave);
    arwill_x86_64_out8(pit_channel0, (uint8_t)(divisor & 0xffU));
    arwill_x86_64_out8(pit_channel0, (uint8_t)(divisor >> 8U));
}

__attribute__((interrupt))
static void breakpoint_handler(struct interrupt_frame *frame) {
    (void)frame;

    interrupt_context.exception_count++;
    interrupt_context.last_exception_vector = vector_breakpoint;
}

__attribute__((used))
static int arwill_x86_64_timer_dispatch(
    const struct arwill_x86_64_user_registers *registers,
    const struct arwill_x86_64_user_frame *frame
) {
    interrupt_context.timer_ticks++;
    arwill_scheduler_tick(
        frame != 0 && (frame->code_segment & 3U) == 3U
    );
    arwill_x86_64_out8(pic1_command, pic_eoi);
    return arwill_x86_64_user_handle_timer(registers, frame);
}

__attribute__((used))
static int arwill_x86_64_fault_dispatch(
    const struct arwill_x86_64_user_registers *registers,
    const struct arwill_x86_64_user_frame *frame,
    uint8_t vector,
    uint64_t error_code
) {
    interrupt_context.exception_count++;
    interrupt_context.last_exception_vector = vector;
    if (arwill_x86_64_user_handle_fault(registers, frame, vector, error_code)) {
        return 1;
    }
    disable_cpu_interrupts();
    for (;;) {
        __asm__ volatile("hlt");
    }
}

#define ARWILL_PUSH_USER_REGISTERS \
    "    pushq %r15\n" \
    "    pushq %r14\n" \
    "    pushq %r13\n" \
    "    pushq %r12\n" \
    "    pushq %r11\n" \
    "    pushq %r10\n" \
    "    pushq %r9\n" \
    "    pushq %r8\n" \
    "    pushq %rbp\n" \
    "    pushq %rdi\n" \
    "    pushq %rsi\n" \
    "    pushq %rdx\n" \
    "    pushq %rcx\n" \
    "    pushq %rbx\n" \
    "    pushq %rax\n"

#define ARWILL_POP_USER_REGISTERS \
    "    popq %rax\n" \
    "    popq %rbx\n" \
    "    popq %rcx\n" \
    "    popq %rdx\n" \
    "    popq %rsi\n" \
    "    popq %rdi\n" \
    "    popq %rbp\n" \
    "    popq %r8\n" \
    "    popq %r9\n" \
    "    popq %r10\n" \
    "    popq %r11\n" \
    "    popq %r12\n" \
    "    popq %r13\n" \
    "    popq %r14\n" \
    "    popq %r15\n"

__asm__(
    ".global arwill_x86_64_user_timer_entry\n"
    "arwill_x86_64_user_timer_entry:\n"
    ARWILL_PUSH_USER_REGISTERS
    "    movq %rsp, %rdi\n"
    "    leaq 120(%rsp), %rsi\n"
    "    call arwill_x86_64_timer_dispatch\n"
    "    testl %eax, %eax\n"
    "    jne arwill_x86_64_user_return_to_kernel\n"
    ARWILL_POP_USER_REGISTERS
    "    iretq\n"
);

__asm__(
    ".global arwill_x86_64_user_invalid_opcode_entry\n"
    "arwill_x86_64_user_invalid_opcode_entry:\n"
    "    pushq $0\n"
    ARWILL_PUSH_USER_REGISTERS
    "    movq %rsp, %rdi\n"
    "    leaq 128(%rsp), %rsi\n"
    "    movl $6, %edx\n"
    "    xorl %ecx, %ecx\n"
    "    call arwill_x86_64_fault_dispatch\n"
    "    jmp arwill_x86_64_user_return_to_kernel\n"
);

__asm__(
    ".global arwill_x86_64_user_general_protection_entry\n"
    "arwill_x86_64_user_general_protection_entry:\n"
    ARWILL_PUSH_USER_REGISTERS
    "    movq %rsp, %rdi\n"
    "    leaq 128(%rsp), %rsi\n"
    "    movl $13, %edx\n"
    "    movq 120(%rsp), %rcx\n"
    "    call arwill_x86_64_fault_dispatch\n"
    "    jmp arwill_x86_64_user_return_to_kernel\n"
);

__asm__(
    ".global arwill_x86_64_user_page_fault_entry\n"
    "arwill_x86_64_user_page_fault_entry:\n"
    ARWILL_PUSH_USER_REGISTERS
    "    movq %rsp, %rdi\n"
    "    leaq 128(%rsp), %rsi\n"
    "    movl $14, %edx\n"
    "    movq 120(%rsp), %rcx\n"
    "    call arwill_x86_64_fault_dispatch\n"
    "    jmp arwill_x86_64_user_return_to_kernel\n"
);

static void x86_64_interrupts_enable(void *context) {
    struct x86_64_interrupt_context *interrupts =
        (struct x86_64_interrupt_context *)context;

    if (interrupts == 0 || !interrupts->idt_loaded || !interrupts->timer_configured) {
        return;
    }

    interrupts->enabled = 1;
    enable_cpu_interrupts();
}

static void x86_64_interrupts_stats(
    void *context,
    struct arwill_interrupt_stats *stats
) {
    const struct x86_64_interrupt_context *interrupts =
        (const struct x86_64_interrupt_context *)context;

    if (interrupts == 0 || stats == 0) {
        return;
    }

    stats->idt_loaded = interrupts->idt_loaded;
    stats->pic_remapped = interrupts->pic_remapped;
    stats->timer_configured = interrupts->timer_configured;
    stats->enabled = interrupts->enabled;
    stats->timer_ticks = interrupts->timer_ticks;
    stats->exception_count = interrupts->exception_count;
    stats->last_exception_vector = interrupts->last_exception_vector;
}

static void x86_64_interrupts_trigger_breakpoint(void *context) {
    (void)context;

    __asm__ volatile("int3" : : : "memory");
}

static const struct arwill_interrupts interrupts = {
    .context = &interrupt_context,
    .name = "x86_64 idt pic pit",
    .enable = x86_64_interrupts_enable,
    .stats = x86_64_interrupts_stats,
    .trigger_breakpoint = x86_64_interrupts_trigger_breakpoint,
};

const struct arwill_interrupts *arwill_x86_64_interrupts_init(void) {
    struct idt_pointer pointer;

    disable_cpu_interrupts();

    for (size_t index = 0; index < idt_entry_count; index++) {
        idt[index].offset_low = 0;
        idt[index].selector = 0;
        idt[index].ist = 0;
        idt[index].attributes = 0;
        idt[index].offset_mid = 0;
        idt[index].offset_high = 0;
        idt[index].zero = 0;
    }

    interrupt_context.idt_loaded = 0;
    interrupt_context.pic_remapped = 0;
    interrupt_context.timer_configured = 0;
    interrupt_context.enabled = 0;
    interrupt_context.timer_ticks = 0;
    interrupt_context.exception_count = 0;
    interrupt_context.last_exception_vector = 0;

    idt_set_gate(vector_breakpoint, idt_gate_interrupt, (void (*)(void))breakpoint_handler);
    idt_set_gate(vector_invalid_opcode, idt_gate_interrupt,
        arwill_x86_64_user_invalid_opcode_entry);
    idt_set_gate(vector_general_protection, idt_gate_interrupt,
        arwill_x86_64_user_general_protection_entry);
    idt_set_gate(vector_page_fault, idt_gate_interrupt,
        arwill_x86_64_user_page_fault_entry);
    idt_set_gate(vector_irq0_timer, idt_gate_interrupt, arwill_x86_64_user_timer_entry);
    idt_set_gate(vector_syscall, idt_gate_user_interrupt, arwill_x86_64_user_syscall_entry);
    arwill_x86_64_user_mode_mark_syscall_gate_loaded();

    pointer.limit = (uint16_t)(sizeof(idt) - 1U);
    pointer.base = (uint64_t)idt;
    load_idt(&pointer);
    interrupt_context.idt_loaded = 1;

    pic_remap_timer_only();
    interrupt_context.pic_remapped = 1;

    pit_configure_timer();
    interrupt_context.timer_configured = 1;

    return &interrupts;
}

const struct arwill_clock *arwill_x86_64_pit_clock(void) {
    return &pit_clock;
}
