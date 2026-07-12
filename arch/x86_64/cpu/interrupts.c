#include <stddef.h>
#include <stdint.h>

#include <arwill/arch/x86_64/interrupts.h>
#include <arwill/arch/x86_64/io.h>
#include <arwill/arch/x86_64/user_mode.h>
#include <arwill/kernel/interrupts.h>
#include <arwill/kernel/scheduler.h>

enum {
    idt_entry_count = 256,
    idt_gate_interrupt = 0x8e,
    idt_gate_user_interrupt = 0xee,
    vector_breakpoint = 3,
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

__attribute__((interrupt))
static void timer_handler(struct interrupt_frame *frame) {
    (void)frame;

    interrupt_context.timer_ticks++;
    arwill_scheduler_tick();
    arwill_x86_64_out8(pic1_command, pic_eoi);
}

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
    idt_set_gate(vector_irq0_timer, idt_gate_interrupt, (void (*)(void))timer_handler);
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
