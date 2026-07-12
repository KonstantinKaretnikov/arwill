#include <stddef.h>
#include <stdint.h>

#include <arwill/arch/x86_64/user_mode.h>
#include <arwill/kernel/console.h>
#include <arwill/kernel/input.h>
#include <arwill/kernel/memory.h>
#include <arwill/kernel/user.h>

enum {
    gdt_kernel_code_selector = 0x08,
    gdt_kernel_data_selector = 0x10,
    gdt_user_data_selector = 0x1b,
    gdt_user_code_selector = 0x23,
    gdt_tss_selector = 0x28,
    gdt_access_kernel_code = 0x9a,
    gdt_access_kernel_data = 0x92,
    gdt_access_user_code = 0xfa,
    gdt_access_user_data = 0xf2,
    gdt_flags_code = 0x0a,
    gdt_flags_data = 0x0c,
    page_present = 0x001,
    page_writable = 0x002,
    page_user = 0x004,
    page_large = 0x080,
    syscall_write = 1,
    syscall_exit = 2,
    syscall_read = 3,
    user_code_message_offset = 0x100,
    user_write_limit = 256,
    bad_syscall_exit_code = 127
};

enum {
    awp_header_size = 16,
    awp_magic_0 = 'A',
    awp_magic_1 = 'W',
    awp_magic_2 = 'P',
    awp_magic_3 = '1',
};

struct gdt_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss_descriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t limit_high_flags;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct gdt_table {
    uint64_t null_descriptor;
    uint64_t kernel_code;
    uint64_t kernel_data;
    uint64_t user_data;
    uint64_t user_code;
    struct tss_descriptor tss;
} __attribute__((packed));

struct task_state_segment {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} __attribute__((packed));

struct syscall_registers {
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

struct syscall_frame {
    uint64_t instruction_pointer;
    uint64_t code_segment;
    uint64_t cpu_flags;
    uint64_t stack_pointer;
    uint64_t stack_segment;
};

struct x86_64_user_context {
    struct arwill_memory *memory;
    uint64_t hhdm_offset;
    int available;
    int hhdm_available;
    int gdt_loaded;
    int tss_loaded;
    int syscall_gate_loaded;
    uint64_t runs;
    uint64_t syscall_count;
    uint64_t bytes_written;
    uint64_t bad_syscalls;
    const struct arwill_console *active_console;
    const struct arwill_input *input;
    struct arwill_x86_64_user_run_state run_state;
    uint64_t active_code_base;
    uint64_t active_code_size;
    uint64_t active_stack_base;
    uint64_t active_stack_size;
    int active_exited;
    uint32_t active_exit_code;
    uint64_t active_syscalls;
    uint64_t active_bytes_written;
    const char *active_status;
};

static struct gdt_table gdt;
static struct task_state_segment tss;
static uint8_t tss_stack[8192] __attribute__((aligned(16)));
static struct x86_64_user_context user_context;

static const uint64_t user_code_virtual = 0x0000008000000000ULL;
static const uint64_t user_stack_virtual = 0x0000008000200000ULL;
static const char hello_message[] = "user hello: hello from ring 3\n";

static uint64_t make_descriptor(uint8_t access, uint8_t flags) {
    uint64_t descriptor = 0;

    descriptor |= 0xffffULL;
    descriptor |= (uint64_t)access << 40U;
    descriptor |= 0x0fULL << 48U;
    descriptor |= (uint64_t)flags << 52U;

    return descriptor;
}

static void configure_tss_descriptor(struct tss_descriptor *descriptor, uint64_t base) {
    const uint32_t limit = (uint32_t)(sizeof(tss) - 1U);

    descriptor->limit_low = (uint16_t)(limit & 0xffffU);
    descriptor->base_low = (uint16_t)(base & 0xffffU);
    descriptor->base_mid = (uint8_t)((base >> 16U) & 0xffU);
    descriptor->access = 0x89;
    descriptor->limit_high_flags = (uint8_t)((limit >> 16U) & 0x0fU);
    descriptor->base_high = (uint8_t)((base >> 24U) & 0xffU);
    descriptor->base_upper = (uint32_t)(base >> 32U);
    descriptor->reserved = 0;
}

static void load_gdt_and_tss(const struct gdt_pointer *pointer) {
    __asm__ volatile(
        "lgdt (%0)\n"
        "movw %1, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        "pushq %2\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "movw %3, %%ax\n"
        "ltr %%ax\n"
        :
        : "r"(pointer),
          "i"(gdt_kernel_data_selector),
          "i"(gdt_kernel_code_selector),
          "i"(gdt_tss_selector)
        : "rax", "memory"
    );
}

static void initialize_gdt(void) {
    struct gdt_pointer pointer;

    gdt.null_descriptor = 0;
    gdt.kernel_code = make_descriptor(gdt_access_kernel_code, gdt_flags_code);
    gdt.kernel_data = make_descriptor(gdt_access_kernel_data, gdt_flags_data);
    gdt.user_data = make_descriptor(gdt_access_user_data, gdt_flags_data);
    gdt.user_code = make_descriptor(gdt_access_user_code, gdt_flags_code);

    tss.reserved0 = 0;
    tss.rsp0 = (uint64_t)(uintptr_t)&tss_stack[sizeof(tss_stack)];
    tss.rsp1 = 0;
    tss.rsp2 = 0;
    tss.reserved1 = 0;
    tss.ist1 = 0;
    tss.ist2 = 0;
    tss.ist3 = 0;
    tss.ist4 = 0;
    tss.ist5 = 0;
    tss.ist6 = 0;
    tss.ist7 = 0;
    tss.reserved2 = 0;
    tss.reserved3 = 0;
    tss.io_map_base = (uint16_t)sizeof(tss);

    configure_tss_descriptor(&gdt.tss, (uint64_t)(uintptr_t)&tss);

    pointer.limit = (uint16_t)(sizeof(gdt) - 1U);
    pointer.base = (uint64_t)(uintptr_t)&gdt;
    load_gdt_and_tss(&pointer);

    user_context.gdt_loaded = 1;
    user_context.tss_loaded = 1;
}

static uint64_t read_cr3(void) {
    uint64_t value = 0;

    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

static void invalidate_page(uint64_t virtual_address) {
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_address) : "memory");
}

static void clear_page(uint8_t *page) {
    for (size_t index = 0; index < ARWILL_MEMORY_PAGE_SIZE; index++) {
        page[index] = 0;
    }
}

static uint8_t *physical_to_virtual(const struct x86_64_user_context *context, uint64_t physical) {
    return (uint8_t *)(uintptr_t)(context->hhdm_offset + physical);
}

static uint64_t table_entry_physical(uint64_t entry) {
    return entry & 0x000ffffffffff000ULL;
}

static int allocate_zero_page(struct x86_64_user_context *context, uint64_t *physical) {
    if (!arwill_physical_allocate_page(context->memory, physical)) {
        return 0;
    }

    clear_page(physical_to_virtual(context, *physical));
    return 1;
}

static size_t page_table_index(uint64_t virtual_address, unsigned shift) {
    return (size_t)((virtual_address >> shift) & 0x1ffULL);
}

static int ensure_child_table(
    struct x86_64_user_context *context,
    uint64_t *table,
    size_t index,
    uint64_t **child
) {
    uint64_t entry = table[index];

    if ((entry & page_present) == 0U) {
        uint64_t child_physical = 0;

        if (!allocate_zero_page(context, &child_physical)) {
            return 0;
        }

        entry = child_physical | page_present | page_writable | page_user;
        table[index] = entry;
    } else if ((entry & page_large) != 0U) {
        return 0;
    } else if ((entry & page_user) == 0U) {
        entry |= page_user;
        table[index] = entry;
    }

    *child = (uint64_t *)physical_to_virtual(context, table_entry_physical(entry));
    return 1;
}

static int map_user_page(
    struct x86_64_user_context *context,
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
) {
    const uint64_t cr3 = read_cr3() & 0x000ffffffffff000ULL;
    uint64_t *pml4 = (uint64_t *)physical_to_virtual(context, cr3);
    uint64_t *pdpt = 0;
    uint64_t *pd = 0;
    uint64_t *pt = 0;
    const size_t pml4_index = page_table_index(virtual_address, 39U);
    const size_t pdpt_index = page_table_index(virtual_address, 30U);
    const size_t pd_index = page_table_index(virtual_address, 21U);
    const size_t pt_index = page_table_index(virtual_address, 12U);

    if (!ensure_child_table(context, pml4, pml4_index, &pdpt)) {
        return 0;
    }

    if (!ensure_child_table(context, pdpt, pdpt_index, &pd)) {
        return 0;
    }

    if (!ensure_child_table(context, pd, pd_index, &pt)) {
        return 0;
    }

    pt[pt_index] = (physical_address & 0x000ffffffffff000ULL) | flags | page_present | page_user;
    invalidate_page(virtual_address);
    return 1;
}

static int emit8(uint8_t *code, size_t *offset, uint8_t value) {
    if (*offset >= ARWILL_MEMORY_PAGE_SIZE) {
        return 0;
    }

    code[*offset] = value;
    *offset = *offset + 1U;
    return 1;
}

static int emit32(uint8_t *code, size_t *offset, uint32_t value) {
    for (unsigned shift = 0; shift < 32U; shift += 8U) {
        if (!emit8(code, offset, (uint8_t)((value >> shift) & 0xffU))) {
            return 0;
        }
    }

    return 1;
}

static int emit64(uint8_t *code, size_t *offset, uint64_t value) {
    for (unsigned shift = 0; shift < 64U; shift += 8U) {
        if (!emit8(code, offset, (uint8_t)((value >> shift) & 0xffU))) {
            return 0;
        }
    }

    return 1;
}

static int emit_mov_rax_imm32(uint8_t *code, size_t *offset, uint32_t value) {
    return emit8(code, offset, 0x48) &&
        emit8(code, offset, 0xc7) &&
        emit8(code, offset, 0xc0) &&
        emit32(code, offset, value);
}

static int emit_mov_rdi_imm32(uint8_t *code, size_t *offset, uint32_t value) {
    return emit8(code, offset, 0x48) &&
        emit8(code, offset, 0xc7) &&
        emit8(code, offset, 0xc7) &&
        emit32(code, offset, value);
}

static int emit_mov_rdi_imm64(uint8_t *code, size_t *offset, uint64_t value) {
    return emit8(code, offset, 0x48) &&
        emit8(code, offset, 0xbf) &&
        emit64(code, offset, value);
}

static int emit_mov_rsi_imm32(uint8_t *code, size_t *offset, uint32_t value) {
    return emit8(code, offset, 0x48) &&
        emit8(code, offset, 0xc7) &&
        emit8(code, offset, 0xc6) &&
        emit32(code, offset, value);
}

static int emit_int80(uint8_t *code, size_t *offset) {
    return emit8(code, offset, 0xcd) && emit8(code, offset, 0x80);
}

static int emit_ud2(uint8_t *code, size_t *offset) {
    return emit8(code, offset, 0x0f) && emit8(code, offset, 0x0b);
}

static int copy_message_to_user_page(uint8_t *code) {
    const size_t length = sizeof(hello_message) - 1U;

    if (user_code_message_offset + length >= ARWILL_MEMORY_PAGE_SIZE) {
        return 0;
    }

    for (size_t index = 0; index < length; index++) {
        code[user_code_message_offset + index] = (uint8_t)hello_message[index];
    }

    return 1;
}

static int build_hello_program(uint8_t *code) {
    size_t offset = 0;
    const uint64_t message_virtual = user_code_virtual + user_code_message_offset;

    if (!copy_message_to_user_page(code)) {
        return 0;
    }

    return emit_mov_rax_imm32(code, &offset, syscall_write) &&
        emit_mov_rdi_imm64(code, &offset, message_virtual) &&
        emit_mov_rsi_imm32(code, &offset, (uint32_t)(sizeof(hello_message) - 1U)) &&
        emit_int80(code, &offset) &&
        emit_mov_rax_imm32(code, &offset, syscall_exit) &&
        emit_mov_rdi_imm32(code, &offset, 7U) &&
        emit_int80(code, &offset) &&
        emit_ud2(code, &offset);
}

static int build_bad_syscall_program(uint8_t *code) {
    size_t offset = 0;

    return emit_mov_rax_imm32(code, &offset, 99U) &&
        emit_int80(code, &offset) &&
        emit_ud2(code, &offset);
}

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8U) |
        ((uint32_t)bytes[2] << 16U) |
        ((uint32_t)bytes[3] << 24U);
}

static int copy_user_image_to_page(
    const uint8_t *image,
    uint64_t image_size,
    uint8_t *code,
    uint64_t *entry_point
) {
    if (image == 0 || code == 0 || entry_point == 0) {
        return 0;
    }

    if (image_size < awp_header_size ||
        image[0] != awp_magic_0 ||
        image[1] != awp_magic_1 ||
        image[2] != awp_magic_2 ||
        image[3] != awp_magic_3) {
        return 0;
    }

    const uint16_t header_size = read_le16(&image[4]);
    const uint16_t entry_offset = read_le16(&image[6]);
    const uint32_t code_size = read_le32(&image[8]);

    if (header_size < awp_header_size ||
        (uint64_t)header_size > image_size ||
        code_size == 0U ||
        (uint64_t)code_size > ARWILL_MEMORY_PAGE_SIZE ||
        (uint64_t)entry_offset >= (uint64_t)code_size ||
        image_size - (uint64_t)header_size < (uint64_t)code_size) {
        return 0;
    }

    for (uint32_t index = 0; index < code_size; index++) {
        code[index] = image[(uint64_t)header_size + (uint64_t)index];
    }

    *entry_point = user_code_virtual + (uint64_t)entry_offset;
    return 1;
}

static int prepare_user_program(
    struct x86_64_user_context *context,
    enum arwill_user_program program,
    uint64_t *entry_point,
    uint64_t *stack_pointer
) {
    uint64_t code_physical = 0;
    uint64_t stack_physical = 0;
    uint8_t *code = 0;

    if (!allocate_zero_page(context, &code_physical)) {
        return 0;
    }

    if (!allocate_zero_page(context, &stack_physical)) {
        return 0;
    }

    code = physical_to_virtual(context, code_physical);

    if (program == arwill_user_program_hello) {
        if (!build_hello_program(code)) {
            return 0;
        }
    } else if (program == arwill_user_program_bad_syscall) {
        if (!build_bad_syscall_program(code)) {
            return 0;
        }
    } else {
        return 0;
    }

    if (!map_user_page(context, user_code_virtual, code_physical, 0U)) {
        return 0;
    }

    if (!map_user_page(
            context,
            user_stack_virtual,
            stack_physical,
            page_writable
        )) {
        return 0;
    }

    context->active_code_base = user_code_virtual;
    context->active_code_size = ARWILL_MEMORY_PAGE_SIZE;
    context->active_stack_base = user_stack_virtual;
    context->active_stack_size = ARWILL_MEMORY_PAGE_SIZE;
    *entry_point = user_code_virtual;
    *stack_pointer = user_stack_virtual + ARWILL_MEMORY_PAGE_SIZE - 16U;
    return 1;
}

static int prepare_user_image(
    struct x86_64_user_context *context,
    const uint8_t *image,
    uint64_t image_size,
    uint64_t *entry_point,
    uint64_t *stack_pointer
) {
    uint64_t code_physical = 0;
    uint64_t stack_physical = 0;
    uint8_t *code = 0;

    if (!allocate_zero_page(context, &code_physical)) {
        return 0;
    }

    if (!allocate_zero_page(context, &stack_physical)) {
        return 0;
    }

    code = physical_to_virtual(context, code_physical);

    if (!copy_user_image_to_page(image, image_size, code, entry_point)) {
        return 0;
    }

    if (!map_user_page(context, user_code_virtual, code_physical, 0U)) {
        return 0;
    }

    if (!map_user_page(
            context,
            user_stack_virtual,
            stack_physical,
            page_writable
        )) {
        return 0;
    }

    context->active_code_base = user_code_virtual;
    context->active_code_size = ARWILL_MEMORY_PAGE_SIZE;
    context->active_stack_base = user_stack_virtual;
    context->active_stack_size = ARWILL_MEMORY_PAGE_SIZE;
    *stack_pointer = user_stack_virtual + ARWILL_MEMORY_PAGE_SIZE - 16U;
    return 1;
}

static int range_in_user_region(
    uint64_t start,
    uint64_t length,
    uint64_t region_base,
    uint64_t region_size
) {
    if (length == 0U) {
        return 1;
    }

    if (start < region_base) {
        return 0;
    }

    if (length > region_size) {
        return 0;
    }

    return (start - region_base) <= (region_size - length);
}

static int user_range_is_readable(const struct x86_64_user_context *context, uint64_t start, uint64_t length) {
    return range_in_user_region(
            start,
            length,
            context->active_code_base,
            context->active_code_size
        ) ||
        range_in_user_region(
            start,
            length,
            context->active_stack_base,
            context->active_stack_size
        );
}

static int user_range_is_writable(const struct x86_64_user_context *context, uint64_t start, uint64_t length) {
    return range_in_user_region(
        start,
        length,
        context->active_stack_base,
        context->active_stack_size
    );
}

static void write_user_bytes(
    const struct arwill_console *console,
    const uint8_t *bytes,
    uint64_t length
) {
    char text[2];

    text[1] = '\0';

    for (uint64_t index = 0; index < length; index++) {
        text[0] = (char)bytes[index];
        arwill_console_write(console, text);
    }
}

__attribute__((used))
static uint64_t arwill_x86_64_user_kernel_rsp(void) {
    if (user_context.run_state.kernel_rsp == 0U) {
        for (;;) {
            __asm__ volatile("hlt");
        }
    }

    return user_context.run_state.kernel_rsp;
}

__attribute__((used))
static int arwill_x86_64_user_handle_syscall(
    struct syscall_registers *registers,
    const struct syscall_frame *frame
) {
    (void)frame;

    if (registers == 0) {
        user_context.active_exit_code = bad_syscall_exit_code;
        user_context.active_status = "bad syscall frame";
        user_context.active_exited = 1;
        return 1;
    }

    user_context.active_syscalls++;
    user_context.syscall_count++;

    if (registers->rax == syscall_write) {
        const uint64_t user_pointer = registers->rdi;
        uint64_t length = registers->rsi;

        if (length > user_write_limit) {
            length = user_write_limit;
        }

        if (
            user_context.active_console == 0 ||
            !user_range_is_readable(&user_context, user_pointer, length)
        ) {
            user_context.active_exit_code = bad_syscall_exit_code;
            user_context.active_status = "bad user pointer";
            user_context.active_exited = 1;
            return 1;
        }

        write_user_bytes(
            user_context.active_console,
            (const uint8_t *)(uintptr_t)user_pointer,
            length
        );
        user_context.active_bytes_written += length;
        user_context.bytes_written += length;
        registers->rax = length;
        return 0;
    }

    if (registers->rax == syscall_exit) {
        user_context.active_exit_code = (uint32_t)(registers->rdi & 0xffffffffU);
        user_context.active_status = "exited";
        user_context.active_exited = 1;
        return 1;
    }

    if (registers->rax == syscall_read) {
        const uint64_t user_pointer = registers->rdi;
        uint64_t length = registers->rsi;

        if (length > user_write_limit) {
            length = user_write_limit;
        }

        if (user_context.input == 0 ||
            !user_range_is_writable(&user_context, user_pointer, length)) {
            user_context.active_exit_code = bad_syscall_exit_code;
            user_context.active_status = "bad user pointer";
            user_context.active_exited = 1;
            return 1;
        }

        uint8_t *destination = (uint8_t *)(uintptr_t)user_pointer;
        for (uint64_t index = 0; index < length; index++) {
            destination[index] = arwill_input_read_byte(user_context.input);
        }

        registers->rax = length;
        return 0;
    }

    user_context.bad_syscalls++;
    user_context.active_exit_code = bad_syscall_exit_code;
    user_context.active_status = "bad syscall";
    user_context.active_exited = 1;
    return 1;
}

__asm__(
    ".global arwill_x86_64_user_enter\n"
    "arwill_x86_64_user_enter:\n"
    "    movq %rsp, 0(%rdx)\n"
    "    pushq $0x1b\n"
    "    pushq %rsi\n"
    "    pushfq\n"
    "    popq %rax\n"
    "    andq $~0x200, %rax\n"
    "    orq $0x2, %rax\n"
    "    pushq %rax\n"
    "    pushq $0x23\n"
    "    pushq %rdi\n"
    "    iretq\n"
);

__asm__(
    ".global arwill_x86_64_user_syscall_entry\n"
    "arwill_x86_64_user_syscall_entry:\n"
    "    pushq %r15\n"
    "    pushq %r14\n"
    "    pushq %r13\n"
    "    pushq %r12\n"
    "    pushq %r11\n"
    "    pushq %r10\n"
    "    pushq %r9\n"
    "    pushq %r8\n"
    "    pushq %rbp\n"
    "    pushq %rdi\n"
    "    pushq %rsi\n"
    "    pushq %rdx\n"
    "    pushq %rcx\n"
    "    pushq %rbx\n"
    "    pushq %rax\n"
    "    movq %rsp, %rdi\n"
    "    leaq 120(%rsp), %rsi\n"
    "    call arwill_x86_64_user_handle_syscall\n"
    "    testl %eax, %eax\n"
    "    jne 1f\n"
    "    popq %rax\n"
    "    popq %rbx\n"
    "    popq %rcx\n"
    "    popq %rdx\n"
    "    popq %rsi\n"
    "    popq %rdi\n"
    "    popq %rbp\n"
    "    popq %r8\n"
    "    popq %r9\n"
    "    popq %r10\n"
    "    popq %r11\n"
    "    popq %r12\n"
    "    popq %r13\n"
    "    popq %r14\n"
    "    popq %r15\n"
    "    iretq\n"
    "1:\n"
    "    call arwill_x86_64_user_kernel_rsp\n"
    "    movq %rax, %rsp\n"
    "    retq\n"
);

static void clear_run_state(struct x86_64_user_context *context) {
    context->active_console = 0;
    context->run_state.kernel_rsp = 0;
    context->active_code_base = 0;
    context->active_code_size = 0;
    context->active_stack_base = 0;
    context->active_stack_size = 0;
    context->active_exited = 0;
    context->active_exit_code = 0;
    context->active_syscalls = 0;
    context->active_bytes_written = 0;
    context->active_status = "not started";
}

static int run_prepared_user_code(
    struct x86_64_user_context *user,
    uint64_t entry_point,
    uint64_t stack_pointer,
    const struct arwill_console *console,
    struct arwill_user_program_result *result
) {
    if (user == 0 || !user->available || console == 0) {
        return 0;
    }

    user->active_console = console;

    user->runs++;
    if (result != 0) {
        result->started = 1;
    }

    arwill_x86_64_user_enter(entry_point, stack_pointer, &user->run_state);

    if (result != 0) {
        result->exited = user->active_exited;
        result->exit_code = user->active_exit_code;
        result->syscall_count = user->active_syscalls;
        result->bytes_written = user->active_bytes_written;
        result->status = user->active_status;
    }

    clear_run_state(user);
    return 1;
}

static int x86_64_user_run(
    void *context,
    enum arwill_user_program program,
    const struct arwill_console *console,
    struct arwill_user_program_result *result
) {
    struct x86_64_user_context *user = (struct x86_64_user_context *)context;
    uint64_t entry_point = 0;
    uint64_t stack_pointer = 0;

    if (result != 0) {
        result->status = "unavailable";
    }

    if (user == 0 || !user->available || console == 0) {
        return 0;
    }

    clear_run_state(user);

    if (!prepare_user_program(user, program, &entry_point, &stack_pointer)) {
        if (result != 0) {
            result->status = "load failed";
        }
        clear_run_state(user);
        return 0;
    }

    return run_prepared_user_code(user, entry_point, stack_pointer, console, result);
}

static int x86_64_user_run_image(
    void *context,
    const uint8_t *image,
    uint64_t image_size,
    const struct arwill_console *console,
    struct arwill_user_program_result *result
) {
    struct x86_64_user_context *user = (struct x86_64_user_context *)context;
    uint64_t entry_point = 0;
    uint64_t stack_pointer = 0;

    if (result != 0) {
        result->status = "unavailable";
    }

    if (user == 0 || !user->available || console == 0 || image == 0) {
        return 0;
    }

    clear_run_state(user);

    if (!prepare_user_image(user, image, image_size, &entry_point, &stack_pointer)) {
        if (result != 0) {
            result->status = "load failed";
        }
        clear_run_state(user);
        return 0;
    }

    return run_prepared_user_code(user, entry_point, stack_pointer, console, result);
}

static void x86_64_user_stats(void *context, struct arwill_user_stats *stats) {
    const struct x86_64_user_context *user = (const struct x86_64_user_context *)context;

    if (user == 0 || stats == 0) {
        return;
    }

    stats->available = user->available;
    stats->hhdm_available = user->hhdm_available;
    stats->gdt_loaded = user->gdt_loaded;
    stats->tss_loaded = user->tss_loaded;
    stats->syscall_gate_loaded = user->syscall_gate_loaded;
    stats->runs = user->runs;
    stats->syscall_count = user->syscall_count;
    stats->bytes_written = user->bytes_written;
    stats->bad_syscalls = user->bad_syscalls;
}

static const struct arwill_user_runtime user_runtime = {
    .context = &user_context,
    .name = "x86_64 ring3 int80",
    .run = x86_64_user_run,
    .run_image = x86_64_user_run_image,
    .stats = x86_64_user_stats,
};

const struct arwill_user_runtime *arwill_x86_64_user_mode_init(
    struct arwill_memory *memory,
    uint64_t hhdm_offset,
    const struct arwill_input *input
) {
    user_context.memory = memory;
    user_context.hhdm_offset = hhdm_offset;
    user_context.available = 0;
    user_context.hhdm_available = hhdm_offset != 0U;
    user_context.gdt_loaded = 0;
    user_context.tss_loaded = 0;
    user_context.syscall_gate_loaded = 0;
    user_context.runs = 0;
    user_context.syscall_count = 0;
    user_context.bytes_written = 0;
    user_context.bad_syscalls = 0;
    user_context.input = input;
    clear_run_state(&user_context);

    initialize_gdt();

    if (memory != 0 && user_context.hhdm_available) {
        user_context.available = 1;
    }

    return &user_runtime;
}

void arwill_x86_64_user_mode_mark_syscall_gate_loaded(void) {
    user_context.syscall_gate_loaded = 1;
}
