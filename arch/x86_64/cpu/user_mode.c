#include <stddef.h>
#include <stdint.h>

#include <arwill/arch/x86_64/user_mode.h>
#include <arwill/kernel/clock.h>
#include <arwill/kernel/console.h>
#include <arwill/kernel/filesystem.h>
#include <arwill/kernel/input.h>
#include <arwill/kernel/log.h>
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
    syscall_write = 1,
    syscall_exit = 2,
    syscall_read = 3,
    syscall_clock = 4,
    syscall_read_file = 5,
    syscall_write_file = 6,
    user_code_message_offset = 0x100,
    user_write_limit = 256,
    user_input_capacity = 128,
    user_code_page_count = 2,
    user_stack_page_count = 2,
    user_quantum_ticks = 2,
    user_file_limit = 2048,
    user_path_limit = 63,
    bad_syscall_exit_code = 127,
    canceled_exit_code = 130,
    awp_header_size = 16,
    awp_magic_0 = 'A',
    awp_magic_1 = 'W',
    awp_magic_2 = 'P',
    awp_magic_3 = '1',
    efer_nxe = 1 << 11
};

static const uint32_t msr_efer = 0xc0000080U;
static const uint64_t page_address_mask = 0x000ffffffffff000ULL;
static const uint64_t page_no_execute = 1ULL << 63U;
static const uint64_t user_code_virtual = 0x0000008000000000ULL;
static const uint64_t user_stack_virtual = 0x0000008000200000ULL;
static const char hello_message[] = "user hello: hello from ring 3\n";

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

struct x86_64_user_address_space {
    uint64_t cr3;
    uint64_t code_physical[user_code_page_count];
    uint64_t stack_physical[user_stack_page_count];
};

struct x86_64_user_task {
    struct x86_64_user_address_space address_space;
    struct arwill_x86_64_user_saved_context saved;
    enum arwill_user_task_state state;
    uint32_t pid;
    char name[arwill_user_task_name_capacity];
    const struct arwill_console *console;
    uint32_t exit_code;
    uint8_t fault_vector;
    uint64_t run_count;
    uint64_t syscall_count;
    uint64_t bytes_written;
    const char *status;
    uint8_t input[user_input_capacity];
    size_t input_head;
    size_t input_count;
    uint64_t pending_read_pointer;
    uint64_t pending_read_length;
};

struct x86_64_user_context {
    struct arwill_memory *memory;
    uint64_t hhdm_offset;
    uint64_t kernel_cr3;
    uint64_t kernel_rsp;
    int available;
    int hhdm_available;
    int gdt_loaded;
    int tss_loaded;
    int syscall_gate_loaded;
    uint64_t runs;
    uint64_t syscall_count;
    uint64_t bytes_written;
    uint64_t bad_syscalls;
    uint64_t preemptions;
    uint64_t faults;
    const struct arwill_input *legacy_input;
    const struct arwill_clock *clock;
    const struct arwill_filesystem *filesystem;
    struct arwill_event_log *log;
    uint8_t filesystem_buffer[user_file_limit];
    char path_buffer[user_path_limit + 1];
    struct x86_64_user_task tasks[arwill_user_task_capacity];
    struct x86_64_user_task *active_task;
    uint32_t next_pid;
    size_t next_slot;
    unsigned active_slice_ticks;
};

static struct gdt_table gdt;
static struct task_state_segment tss;
static uint8_t tss_stack[16384] __attribute__((aligned(16)));
static struct x86_64_user_context user_context;

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
    return value & page_address_mask;
}

static uint64_t read_msr(uint32_t msr) {
    uint32_t low = 0;
    uint32_t high = 0;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32U) | low;
}

static void write_msr(uint32_t msr, uint64_t value) {
    __asm__ volatile(
        "wrmsr"
        :
        : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32U))
        : "memory"
    );
}

static void enable_no_execute(void) {
    write_msr(msr_efer, read_msr(msr_efer) | (uint64_t)efer_nxe);
}

static void clear_page(uint8_t *page) {
    for (size_t index = 0; index < ARWILL_MEMORY_PAGE_SIZE; index++) {
        page[index] = 0;
    }
}

static uint8_t *physical_to_virtual(
    const struct x86_64_user_context *context,
    uint64_t physical
) {
    return (uint8_t *)(uintptr_t)(context->hhdm_offset + physical);
}

static int allocate_zero_page(
    struct x86_64_user_context *context,
    uint64_t *physical
) {
    if (!arwill_physical_allocate_page(context->memory, physical)) {
        return 0;
    }
    clear_page(physical_to_virtual(context, *physical));
    return 1;
}

static size_t page_table_index(uint64_t virtual_address, unsigned shift) {
    return (size_t)((virtual_address >> shift) & 0x1ffULL);
}

static int initialize_address_space(
    struct x86_64_user_context *context,
    struct x86_64_user_address_space *space
) {
    uint64_t pdpt_physical = 0;
    uint64_t pd_physical = 0;
    uint64_t code_pt_physical = 0;
    uint64_t stack_pt_physical = 0;

    if (!allocate_zero_page(context, &space->cr3) ||
        !allocate_zero_page(context, &pdpt_physical) ||
        !allocate_zero_page(context, &pd_physical) ||
        !allocate_zero_page(context, &code_pt_physical) ||
        !allocate_zero_page(context, &stack_pt_physical)) {
        return 0;
    }

    for (size_t index = 0; index < user_code_page_count; index++) {
        if (!allocate_zero_page(context, &space->code_physical[index])) {
            return 0;
        }
    }
    for (size_t index = 0; index < user_stack_page_count; index++) {
        if (!allocate_zero_page(context, &space->stack_physical[index])) {
            return 0;
        }
    }

    uint64_t *kernel_pml4 = (uint64_t *)physical_to_virtual(context, context->kernel_cr3);
    uint64_t *pml4 = (uint64_t *)physical_to_virtual(context, space->cr3);
    uint64_t *pdpt = (uint64_t *)physical_to_virtual(context, pdpt_physical);
    uint64_t *pd = (uint64_t *)physical_to_virtual(context, pd_physical);
    uint64_t *code_pt = (uint64_t *)physical_to_virtual(context, code_pt_physical);
    uint64_t *stack_pt = (uint64_t *)physical_to_virtual(context, stack_pt_physical);

    for (size_t index = 0; index < 512U; index++) {
        pml4[index] = kernel_pml4[index];
    }

    const size_t pml4_index = page_table_index(user_code_virtual, 39U);
    const size_t pdpt_index = page_table_index(user_code_virtual, 30U);
    const size_t code_pd_index = page_table_index(user_code_virtual, 21U);
    const size_t stack_pd_index = page_table_index(user_stack_virtual, 21U);
    const size_t code_pt_index = page_table_index(user_code_virtual, 12U);
    const size_t stack_pt_index = page_table_index(user_stack_virtual, 12U);
    const uint64_t table_flags = page_present | page_writable | page_user;

    pml4[pml4_index] = pdpt_physical | table_flags;
    pdpt[pdpt_index] = pd_physical | table_flags;
    pd[code_pd_index] = code_pt_physical | table_flags;
    pd[stack_pd_index] = stack_pt_physical | table_flags;

    for (size_t index = 0; index < user_code_page_count; index++) {
        code_pt[code_pt_index + index] =
            space->code_physical[index] | page_present | page_user;
    }
    for (size_t index = 0; index < user_stack_page_count; index++) {
        stack_pt[stack_pt_index + index] = space->stack_physical[index] |
            page_present | page_writable | page_user | page_no_execute;
    }
    return 1;
}

static void clear_saved_context(struct arwill_x86_64_user_saved_context *saved) {
    saved->registers.rax = 0;
    saved->registers.rbx = 0;
    saved->registers.rcx = 0;
    saved->registers.rdx = 0;
    saved->registers.rsi = 0;
    saved->registers.rdi = 0;
    saved->registers.rbp = 0;
    saved->registers.r8 = 0;
    saved->registers.r9 = 0;
    saved->registers.r10 = 0;
    saved->registers.r11 = 0;
    saved->registers.r12 = 0;
    saved->registers.r13 = 0;
    saved->registers.r14 = 0;
    saved->registers.r15 = 0;
    saved->frame.instruction_pointer = user_code_virtual;
    saved->frame.code_segment = gdt_user_code_selector;
    saved->frame.cpu_flags = 0x202U;
    saved->frame.stack_pointer = user_stack_virtual +
        (uint64_t)user_stack_page_count * ARWILL_MEMORY_PAGE_SIZE - 16U;
    saved->frame.stack_segment = gdt_user_data_selector;
}

static void reset_task_runtime(
    struct x86_64_user_context *context,
    struct x86_64_user_task *task
) {
    for (size_t index = 0; index < user_code_page_count; index++) {
        clear_page(physical_to_virtual(context, task->address_space.code_physical[index]));
    }
    for (size_t index = 0; index < user_stack_page_count; index++) {
        clear_page(physical_to_virtual(context, task->address_space.stack_physical[index]));
    }

    clear_saved_context(&task->saved);
    task->state = arwill_user_task_empty;
    task->pid = 0;
    task->name[0] = '\0';
    task->console = 0;
    task->exit_code = 0;
    task->fault_vector = 0;
    task->run_count = 0;
    task->syscall_count = 0;
    task->bytes_written = 0;
    task->status = "empty";
    task->input_head = 0;
    task->input_count = 0;
    task->pending_read_pointer = 0;
    task->pending_read_length = 0;
}

static int copy_name(char *destination, size_t capacity, const char *source) {
    size_t index = 0;
    if (destination == 0 || capacity == 0U || source == 0) {
        return 0;
    }
    while (source[index] != '\0') {
        if (index + 1U >= capacity) {
            return 0;
        }
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
    return 1;
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

static int copy_image_to_task(
    struct x86_64_user_context *context,
    struct x86_64_user_task *task,
    const uint8_t *image,
    uint64_t image_size
) {
    if (image_size < awp_header_size ||
        image[0] != awp_magic_0 || image[1] != awp_magic_1 ||
        image[2] != awp_magic_2 || image[3] != awp_magic_3) {
        return 0;
    }

    const uint16_t header_size = read_le16(&image[4]);
    const uint16_t entry_offset = read_le16(&image[6]);
    const uint32_t code_size = read_le32(&image[8]);
    const uint64_t code_capacity =
        (uint64_t)user_code_page_count * ARWILL_MEMORY_PAGE_SIZE;

    if (header_size < awp_header_size || (uint64_t)header_size > image_size ||
        code_size == 0U || (uint64_t)code_size > code_capacity ||
        (uint64_t)entry_offset >= (uint64_t)code_size ||
        image_size - (uint64_t)header_size < (uint64_t)code_size) {
        return 0;
    }

    for (uint32_t index = 0; index < code_size; index++) {
        const size_t page = (size_t)((uint64_t)index / ARWILL_MEMORY_PAGE_SIZE);
        const size_t offset = (size_t)((uint64_t)index % ARWILL_MEMORY_PAGE_SIZE);
        uint8_t *destination = physical_to_virtual(
            context, task->address_space.code_physical[page]
        );
        destination[offset] = image[(uint64_t)header_size + index];
    }
    task->saved.frame.instruction_pointer = user_code_virtual + entry_offset;
    return 1;
}

static int emit8(uint8_t *code, size_t capacity, size_t *offset, uint8_t value) {
    if (*offset >= capacity) {
        return 0;
    }
    code[*offset] = value;
    *offset = *offset + 1U;
    return 1;
}

static int emit32(uint8_t *code, size_t capacity, size_t *offset, uint32_t value) {
    for (unsigned shift = 0; shift < 32U; shift += 8U) {
        if (!emit8(code, capacity, offset, (uint8_t)((value >> shift) & 0xffU))) {
            return 0;
        }
    }
    return 1;
}

static int emit64(uint8_t *code, size_t capacity, size_t *offset, uint64_t value) {
    for (unsigned shift = 0; shift < 64U; shift += 8U) {
        if (!emit8(code, capacity, offset, (uint8_t)((value >> shift) & 0xffU))) {
            return 0;
        }
    }
    return 1;
}

static int emit_mov_rax_imm32(uint8_t *code, size_t capacity, size_t *offset, uint32_t value) {
    return emit8(code, capacity, offset, 0x48) &&
        emit8(code, capacity, offset, 0xc7) &&
        emit8(code, capacity, offset, 0xc0) &&
        emit32(code, capacity, offset, value);
}

static int emit_mov_rdi_imm32(uint8_t *code, size_t capacity, size_t *offset, uint32_t value) {
    return emit8(code, capacity, offset, 0x48) &&
        emit8(code, capacity, offset, 0xc7) &&
        emit8(code, capacity, offset, 0xc7) &&
        emit32(code, capacity, offset, value);
}

static int emit_mov_rdi_imm64(uint8_t *code, size_t capacity, size_t *offset, uint64_t value) {
    return emit8(code, capacity, offset, 0x48) &&
        emit8(code, capacity, offset, 0xbf) &&
        emit64(code, capacity, offset, value);
}

static int emit_mov_rsi_imm32(uint8_t *code, size_t capacity, size_t *offset, uint32_t value) {
    return emit8(code, capacity, offset, 0x48) &&
        emit8(code, capacity, offset, 0xc7) &&
        emit8(code, capacity, offset, 0xc6) &&
        emit32(code, capacity, offset, value);
}

static int emit_int80(uint8_t *code, size_t capacity, size_t *offset) {
    return emit8(code, capacity, offset, 0xcd) &&
        emit8(code, capacity, offset, 0x80);
}

static int emit_ud2(uint8_t *code, size_t capacity, size_t *offset) {
    return emit8(code, capacity, offset, 0x0f) &&
        emit8(code, capacity, offset, 0x0b);
}

static int build_builtin_program(
    struct x86_64_user_context *context,
    struct x86_64_user_task *task,
    enum arwill_user_program program
) {
    uint8_t *code = physical_to_virtual(context, task->address_space.code_physical[0]);
    size_t offset = 0;
    const size_t capacity = ARWILL_MEMORY_PAGE_SIZE;

    if (program == arwill_user_program_hello) {
        const size_t message_length = sizeof(hello_message) - 1U;
        for (size_t index = 0; index < message_length; index++) {
            code[user_code_message_offset + index] = (uint8_t)hello_message[index];
        }
        return emit_mov_rax_imm32(code, capacity, &offset, syscall_write) &&
            emit_mov_rdi_imm64(code, capacity, &offset,
                user_code_virtual + user_code_message_offset) &&
            emit_mov_rsi_imm32(code, capacity, &offset, (uint32_t)message_length) &&
            emit_int80(code, capacity, &offset) &&
            emit_mov_rax_imm32(code, capacity, &offset, syscall_exit) &&
            emit_mov_rdi_imm32(code, capacity, &offset, 7U) &&
            emit_int80(code, capacity, &offset) &&
            emit_ud2(code, capacity, &offset);
    }

    if (program == arwill_user_program_bad_syscall) {
        return emit_mov_rax_imm32(code, capacity, &offset, 99U) &&
            emit_int80(code, capacity, &offset) &&
            emit_ud2(code, capacity, &offset);
    }
    return 0;
}

static struct x86_64_user_task *find_reusable_task(struct x86_64_user_context *context) {
    for (size_t pass = 0; pass < 2U; pass++) {
        for (size_t offset = 0; offset < arwill_user_task_capacity; offset++) {
            const size_t index = (context->next_slot + offset) % arwill_user_task_capacity;
            struct x86_64_user_task *task = &context->tasks[index];
            const int reusable = pass == 0U
                ? task->state == arwill_user_task_empty
                : task->state == arwill_user_task_finished ||
                    task->state == arwill_user_task_faulted;
            if (reusable) {
                context->next_slot = (index + 1U) % arwill_user_task_capacity;
                return task;
            }
        }
    }
    return 0;
}

static uint32_t allocate_pid(struct x86_64_user_context *context) {
    const uint32_t pid = context->next_pid;
    context->next_pid++;
    if (context->next_pid < 1000U) {
        context->next_pid = 1000U;
    }
    return pid;
}

static int prepare_task(
    struct x86_64_user_context *context,
    struct x86_64_user_task *task,
    const char *name,
    const struct arwill_console *console
) {
    reset_task_runtime(context, task);
    if (!copy_name(task->name, sizeof(task->name), name)) {
        return 0;
    }
    task->pid = allocate_pid(context);
    task->console = console;
    task->state = arwill_user_task_ready;
    task->status = "ready";
    context->runs++;
    return 1;
}

static int range_in_region(uint64_t start, uint64_t length, uint64_t base, uint64_t size) {
    if (length == 0U) {
        return 1;
    }
    return start >= base && length <= size && start - base <= size - length;
}

static int user_range_readable(uint64_t start, uint64_t length) {
    return range_in_region(start, length, user_code_virtual,
            (uint64_t)user_code_page_count * ARWILL_MEMORY_PAGE_SIZE) ||
        range_in_region(start, length, user_stack_virtual,
            (uint64_t)user_stack_page_count * ARWILL_MEMORY_PAGE_SIZE);
}

static int user_range_writable(uint64_t start, uint64_t length) {
    return range_in_region(start, length, user_stack_virtual,
        (uint64_t)user_stack_page_count * ARWILL_MEMORY_PAGE_SIZE);
}

static uint8_t *task_writable_pointer(
    struct x86_64_user_context *context,
    struct x86_64_user_task *task,
    uint64_t address
) {
    if (!user_range_writable(address, 1U)) {
        return 0;
    }
    const uint64_t relative = address - user_stack_virtual;
    const size_t page = (size_t)(relative / ARWILL_MEMORY_PAGE_SIZE);
    const size_t offset = (size_t)(relative % ARWILL_MEMORY_PAGE_SIZE);
    return physical_to_virtual(context, task->address_space.stack_physical[page]) + offset;
}

static int copy_user_path(
    struct x86_64_user_context *context,
    uint64_t user_pointer,
    uint64_t length
) {
    if (length == 0U || length > user_path_limit ||
        !user_range_readable(user_pointer, length)) {
        return 0;
    }
    const uint8_t *source = (const uint8_t *)(uintptr_t)user_pointer;
    for (uint64_t index = 0; index < length; index++) {
        if (source[index] == 0U || source[index] == '\n' || source[index] == '\r') {
            return 0;
        }
        context->path_buffer[index] = (char)source[index];
    }
    context->path_buffer[length] = '\0';
    return 1;
}

static int copy_to_task(
    struct x86_64_user_context *context,
    struct x86_64_user_task *task,
    uint64_t user_pointer,
    const uint8_t *source,
    size_t length
) {
    if (!user_range_writable(user_pointer, length)) {
        return 0;
    }
    for (size_t index = 0; index < length; index++) {
        uint8_t *destination = task_writable_pointer(context, task, user_pointer + index);
        if (destination == 0) {
            return 0;
        }
        *destination = source[index];
    }
    return 1;
}

static void copy_active_context(
    struct x86_64_user_task *task,
    const struct arwill_x86_64_user_registers *registers,
    const struct arwill_x86_64_user_frame *frame
) {
    task->saved.registers = *registers;
    task->saved.frame = *frame;
}

static void write_user_bytes(
    const struct arwill_console *console,
    const uint8_t *bytes,
    uint64_t length
) {
    char text[129];
    uint64_t offset = 0;
    while (offset < length) {
        size_t chunk = (size_t)(length - offset);
        if (chunk > sizeof(text) - 1U) {
            chunk = sizeof(text) - 1U;
        }
        for (size_t index = 0; index < chunk; index++) {
            text[index] = (char)bytes[offset + index];
        }
        text[chunk] = '\0';
        arwill_console_write(console, text);
        offset += chunk;
    }
}

static size_t complete_pending_read(
    struct x86_64_user_context *context,
    struct x86_64_user_task *task
) {
    size_t count = task->input_count;
    if ((uint64_t)count > task->pending_read_length) {
        count = (size_t)task->pending_read_length;
    }
    for (size_t index = 0; index < count; index++) {
        uint8_t *destination = task_writable_pointer(
            context, task, task->pending_read_pointer + index
        );
        if (destination == 0) {
            task->state = arwill_user_task_faulted;
            task->exit_code = bad_syscall_exit_code;
            task->fault_vector = 0;
            task->status = "bad user pointer";
            return 0;
        }
        *destination = task->input[task->input_head];
        task->input_head = (task->input_head + 1U) % user_input_capacity;
        task->input_count--;
    }
    task->saved.registers.rax = count;
    task->pending_read_pointer = 0;
    task->pending_read_length = 0;
    if (task->state == arwill_user_task_blocked_input) {
        task->state = arwill_user_task_ready;
        task->status = "ready";
    }
    return count;
}

__attribute__((used))
static int arwill_x86_64_user_handle_syscall(
    struct arwill_x86_64_user_registers *registers,
    const struct arwill_x86_64_user_frame *frame
) {
    struct x86_64_user_task *task = user_context.active_task;
    if (task == 0 || registers == 0 || frame == 0) {
        return 0;
    }

    task->syscall_count++;
    user_context.syscall_count++;

    if (registers->rax == syscall_write) {
        uint64_t length = registers->rsi;
        if (length > user_write_limit) {
            length = user_write_limit;
        }
        if (task->console == 0 || !user_range_readable(registers->rdi, length)) {
            copy_active_context(task, registers, frame);
            task->state = arwill_user_task_faulted;
            task->exit_code = bad_syscall_exit_code;
            task->status = "bad user pointer";
            user_context.faults++;
            user_context.active_task = 0;
            return 1;
        }
        write_user_bytes(task->console, (const uint8_t *)(uintptr_t)registers->rdi, length);
        task->bytes_written += length;
        user_context.bytes_written += length;
        registers->rax = length;
        return 0;
    }

    if (registers->rax == syscall_exit) {
        copy_active_context(task, registers, frame);
        task->exit_code = (uint32_t)(registers->rdi & 0xffffffffU);
        task->state = arwill_user_task_finished;
        task->status = "exited";
        user_context.active_task = 0;
        return 1;
    }

    if (registers->rax == syscall_read) {
        uint64_t length = registers->rsi;
        if (length > user_write_limit) {
            length = user_write_limit;
        }
        if (!user_range_writable(registers->rdi, length)) {
            copy_active_context(task, registers, frame);
            task->state = arwill_user_task_faulted;
            task->exit_code = bad_syscall_exit_code;
            task->status = "bad user pointer";
            user_context.faults++;
            user_context.active_task = 0;
            return 1;
        }
        if (length == 0U) {
            registers->rax = 0;
            return 0;
        }
        if (task->input_count != 0U) {
            task->pending_read_pointer = registers->rdi;
            task->pending_read_length = length;
            registers->rax = complete_pending_read(&user_context, task);
            return 0;
        }
        copy_active_context(task, registers, frame);
        task->pending_read_pointer = registers->rdi;
        task->pending_read_length = length;
        task->state = arwill_user_task_blocked_input;
        task->status = "waiting for input";
        user_context.active_task = 0;
        return 1;
    }

    if (registers->rax == syscall_clock) {
        registers->rax = arwill_clock_monotonic_milliseconds(user_context.clock);
        return 0;
    }

    if (registers->rax == syscall_read_file) {
        const uint64_t capacity = registers->rcx > user_file_limit
            ? user_file_limit : registers->rcx;
        struct arwill_fs_file file;
        if (user_context.filesystem == 0 ||
            !copy_user_path(&user_context, registers->rdi, registers->rsi) ||
            !user_range_writable(registers->rdx, capacity) ||
            !arwill_filesystem_read_file(
                user_context.filesystem, user_context.path_buffer, &file
            ) || file.type != arwill_fs_file_text || file.contents == 0 ||
            file.size_bytes > capacity || file.size_bytes > user_file_limit ||
            !copy_to_task(&user_context, task, registers->rdx,
                (const uint8_t *)file.contents, (size_t)file.size_bytes)) {
            registers->rax = UINT64_MAX;
            return 0;
        }
        registers->rax = file.size_bytes;
        return 0;
    }

    if (registers->rax == syscall_write_file) {
        const uint64_t length = registers->rcx;
        if (user_context.filesystem == 0 || length > user_file_limit ||
            !copy_user_path(&user_context, registers->rdi, registers->rsi) ||
            !user_range_readable(registers->rdx, length)) {
            registers->rax = UINT64_MAX;
            return 0;
        }
        const uint8_t *source = (const uint8_t *)(uintptr_t)registers->rdx;
        for (uint64_t index = 0; index < length; index++) {
            user_context.filesystem_buffer[index] = source[index];
        }
        if (!arwill_filesystem_write_bytes(
                user_context.filesystem,
                user_context.path_buffer,
                arwill_fs_file_text,
                user_context.filesystem_buffer,
                (size_t)length
            )) {
            registers->rax = UINT64_MAX;
            return 0;
        }
        arwill_event_log_record(
            user_context.log,
            arwill_log_info,
            arwill_log_filesystem,
            arwill_log_file_written,
            task->pid,
            length
        );
        registers->rax = length;
        return 0;
    }

    user_context.bad_syscalls++;
    copy_active_context(task, registers, frame);
    task->exit_code = bad_syscall_exit_code;
    task->state = arwill_user_task_finished;
    task->status = "bad syscall";
    user_context.active_task = 0;
    return 1;
}

int arwill_x86_64_user_handle_timer(
    const struct arwill_x86_64_user_registers *registers,
    const struct arwill_x86_64_user_frame *frame
) {
    struct x86_64_user_task *task = user_context.active_task;
    if (task == 0 || registers == 0 || frame == 0 ||
        (frame->code_segment & 3U) != 3U) {
        return 0;
    }
    user_context.active_slice_ticks++;
    if (user_context.active_slice_ticks < user_quantum_ticks) {
        return 0;
    }
    copy_active_context(task, registers, frame);
    task->state = arwill_user_task_ready;
    task->status = "ready";
    user_context.preemptions++;
    user_context.active_task = 0;
    return 1;
}

int arwill_x86_64_user_handle_fault(
    const struct arwill_x86_64_user_registers *registers,
    const struct arwill_x86_64_user_frame *frame,
    uint8_t vector,
    uint64_t error_code
) {
    (void)error_code;
    struct x86_64_user_task *task = user_context.active_task;
    if (task == 0 || registers == 0 || frame == 0 ||
        (frame->code_segment & 3U) != 3U) {
        return 0;
    }
    copy_active_context(task, registers, frame);
    task->state = arwill_user_task_faulted;
    task->fault_vector = vector;
    task->exit_code = 128U + vector;
    task->status = "user fault";
    user_context.faults++;
    user_context.active_task = 0;
    return 1;
}

uint64_t arwill_x86_64_user_kernel_rsp(void) {
    if (user_context.kernel_rsp == 0U) {
        for (;;) {
            __asm__ volatile("cli; hlt");
        }
    }
    return user_context.kernel_rsp;
}

uint64_t arwill_x86_64_user_kernel_cr3(void) {
    return user_context.kernel_cr3;
}

__asm__(
    ".global arwill_x86_64_user_resume\n"
    "arwill_x86_64_user_resume:\n"
    "    pushq %rbp\n"
    "    pushq %rbx\n"
    "    pushq %r12\n"
    "    pushq %r13\n"
    "    pushq %r14\n"
    "    pushq %r15\n"
    "    leaq 1f(%rip), %rax\n"
    "    pushq %rax\n"
    "    movq %rsp, (%rdx)\n"
    "    movq %rsi, %cr3\n"
    "    movq %rdi, %r15\n"
    "    pushq 152(%r15)\n"
    "    pushq 144(%r15)\n"
    "    pushq 136(%r15)\n"
    "    pushq 128(%r15)\n"
    "    pushq 120(%r15)\n"
    "    movq 8(%r15), %rbx\n"
    "    movq 16(%r15), %rcx\n"
    "    movq 24(%r15), %rdx\n"
    "    movq 32(%r15), %rsi\n"
    "    movq 40(%r15), %rdi\n"
    "    movq 48(%r15), %rbp\n"
    "    movq 56(%r15), %r8\n"
    "    movq 64(%r15), %r9\n"
    "    movq 72(%r15), %r10\n"
    "    movq 80(%r15), %r11\n"
    "    movq 88(%r15), %r12\n"
    "    movq 96(%r15), %r13\n"
    "    movq 104(%r15), %r14\n"
    "    movq 0(%r15), %rax\n"
    "    movq 112(%r15), %r15\n"
    "    iretq\n"
    "1:\n"
    "    popq %r15\n"
    "    popq %r14\n"
    "    popq %r13\n"
    "    popq %r12\n"
    "    popq %rbx\n"
    "    popq %rbp\n"
    "    retq\n"
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
    "    jne arwill_x86_64_user_return_to_kernel\n"
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
);

__asm__(
    ".global arwill_x86_64_user_return_to_kernel\n"
    "arwill_x86_64_user_return_to_kernel:\n"
    "    call arwill_x86_64_user_kernel_cr3\n"
    "    movq %rax, %cr3\n"
    "    call arwill_x86_64_user_kernel_rsp\n"
    "    movq %rax, %rsp\n"
    "    retq\n"
);

static struct x86_64_user_task *find_task(
    struct x86_64_user_context *context,
    uint32_t pid
) {
    for (size_t index = 0; index < arwill_user_task_capacity; index++) {
        if (context->tasks[index].state != arwill_user_task_empty &&
            context->tasks[index].pid == pid) {
            return &context->tasks[index];
        }
    }
    return 0;
}

static void x86_64_user_poll(void *opaque) {
    struct x86_64_user_context *context = (struct x86_64_user_context *)opaque;
    if (context == 0 || !context->available || context->active_task != 0) {
        return;
    }
    for (size_t offset = 0; offset < arwill_user_task_capacity; offset++) {
        const size_t index = (context->next_slot + offset) % arwill_user_task_capacity;
        struct x86_64_user_task *task = &context->tasks[index];
        if (task->state != arwill_user_task_ready) {
            continue;
        }
        context->next_slot = (index + 1U) % arwill_user_task_capacity;
        context->active_task = task;
        context->active_slice_ticks = 0;
        task->state = arwill_user_task_running;
        task->status = "running";
        task->run_count++;
        arwill_x86_64_user_resume(
            &task->saved, task->address_space.cr3, &context->kernel_rsp
        );
        context->active_task = 0;
        return;
    }
}

static void fill_program_result(
    const struct x86_64_user_task *task,
    struct arwill_user_program_result *result
) {
    if (result == 0 || task == 0) {
        return;
    }
    result->started = 1;
    result->exited = task->state == arwill_user_task_finished ||
        task->state == arwill_user_task_faulted;
    result->exit_code = task->exit_code;
    result->syscall_count = task->syscall_count;
    result->bytes_written = task->bytes_written;
    result->status = task->status;
}

static void run_until_stable(
    struct x86_64_user_context *context,
    struct x86_64_user_task *task
) {
    while (task->state == arwill_user_task_ready ||
           task->state == arwill_user_task_running) {
        x86_64_user_poll(context);
    }
}

static int x86_64_user_run(
    void *opaque,
    enum arwill_user_program program,
    const struct arwill_console *console,
    struct arwill_user_program_result *result
) {
    struct x86_64_user_context *context = (struct x86_64_user_context *)opaque;
    if (context == 0 || !context->available || console == 0) {
        return 0;
    }
    struct x86_64_user_task *task = find_reusable_task(context);
    if (task == 0 || !prepare_task(context, task, arwill_user_program_name(program), console) ||
        !build_builtin_program(context, task, program)) {
        return 0;
    }
    run_until_stable(context, task);
    fill_program_result(task, result);
    task->state = arwill_user_task_empty;
    return 1;
}

static int x86_64_user_spawn_image(
    void *opaque,
    const uint8_t *image,
    uint64_t image_size,
    const char *name,
    const struct arwill_console *console,
    uint32_t *pid
) {
    struct x86_64_user_context *context = (struct x86_64_user_context *)opaque;
    if (context == 0 || !context->available || image == 0 || name == 0 ||
        console == 0 || pid == 0) {
        return 0;
    }
    struct x86_64_user_task *task = find_reusable_task(context);
    if (task == 0 || !prepare_task(context, task, name, console) ||
        !copy_image_to_task(context, task, image, image_size)) {
        if (task != 0) {
            task->state = arwill_user_task_empty;
        }
        return 0;
    }
    *pid = task->pid;
    return 1;
}

static int x86_64_user_run_image(
    void *opaque,
    const uint8_t *image,
    uint64_t image_size,
    const struct arwill_console *console,
    struct arwill_user_program_result *result
) {
    struct x86_64_user_context *context = (struct x86_64_user_context *)opaque;
    uint32_t pid = 0;
    if (!x86_64_user_spawn_image(
            opaque, image, image_size, "awp", console, &pid
        )) {
        return 0;
    }
    struct x86_64_user_task *task = find_task(context, pid);
    run_until_stable(context, task);
    fill_program_result(task, result);
    task->state = arwill_user_task_empty;
    return 1;
}

static int x86_64_user_deliver_input(void *opaque, uint32_t pid, uint8_t byte) {
    struct x86_64_user_context *context = (struct x86_64_user_context *)opaque;
    struct x86_64_user_task *task = context == 0 ? 0 : find_task(context, pid);
    if (task == 0 || (task->state != arwill_user_task_ready &&
        task->state != arwill_user_task_blocked_input)) {
        return 0;
    }
    if (task->input_count >= user_input_capacity) {
        return 0;
    }
    const size_t tail = (task->input_head + task->input_count) % user_input_capacity;
    task->input[tail] = byte;
    task->input_count++;
    if (task->state == arwill_user_task_blocked_input) {
        (void)complete_pending_read(context, task);
    }
    return 1;
}

static int x86_64_user_cancel(void *opaque, uint32_t pid, uint32_t exit_code) {
    struct x86_64_user_context *context = (struct x86_64_user_context *)opaque;
    struct x86_64_user_task *task = context == 0 ? 0 : find_task(context, pid);
    if (task == 0 || task == context->active_task ||
        task->state == arwill_user_task_finished || task->state == arwill_user_task_faulted) {
        return 0;
    }
    task->state = arwill_user_task_finished;
    task->exit_code = exit_code == 0U ? canceled_exit_code : exit_code;
    task->status = "canceled";
    return 1;
}

static void copy_task_info(
    const struct x86_64_user_task *task,
    struct arwill_user_task_info *info
) {
    info->pid = task->pid;
    info->name = task->name;
    info->state = task->state;
    info->exit_code = task->exit_code;
    info->fault_vector = task->fault_vector;
    info->run_count = task->run_count;
}

static int x86_64_user_task_info(
    void *opaque,
    uint32_t pid,
    struct arwill_user_task_info *info
) {
    struct x86_64_user_context *context = (struct x86_64_user_context *)opaque;
    struct x86_64_user_task *task = context == 0 ? 0 : find_task(context, pid);
    if (task == 0 || info == 0) {
        return 0;
    }
    copy_task_info(task, info);
    return 1;
}

static size_t x86_64_user_tasks(
    void *opaque,
    struct arwill_user_task_info *tasks,
    size_t capacity
) {
    struct x86_64_user_context *context = (struct x86_64_user_context *)opaque;
    size_t count = 0;
    if (context == 0 || tasks == 0) {
        return 0;
    }
    for (size_t index = 0; index < arwill_user_task_capacity && count < capacity; index++) {
        if (context->tasks[index].state == arwill_user_task_empty) {
            continue;
        }
        copy_task_info(&context->tasks[index], &tasks[count]);
        count++;
    }
    return count;
}

static void x86_64_user_stats(void *opaque, struct arwill_user_stats *stats) {
    const struct x86_64_user_context *context =
        (const struct x86_64_user_context *)opaque;
    if (context == 0 || stats == 0) {
        return;
    }
    stats->available = context->available;
    stats->hhdm_available = context->hhdm_available;
    stats->gdt_loaded = context->gdt_loaded;
    stats->tss_loaded = context->tss_loaded;
    stats->syscall_gate_loaded = context->syscall_gate_loaded;
    stats->runs = context->runs;
    stats->syscall_count = context->syscall_count;
    stats->bytes_written = context->bytes_written;
    stats->bad_syscalls = context->bad_syscalls;
    stats->preemptions = context->preemptions;
    stats->faults = context->faults;
}

static const struct arwill_user_runtime user_runtime = {
    .context = &user_context,
    .name = "x86_64 ring3 awp scheduler",
    .run = x86_64_user_run,
    .run_image = x86_64_user_run_image,
    .spawn_image = x86_64_user_spawn_image,
    .poll = x86_64_user_poll,
    .deliver_input = x86_64_user_deliver_input,
    .cancel = x86_64_user_cancel,
    .task_info = x86_64_user_task_info,
    .tasks = x86_64_user_tasks,
    .stats = x86_64_user_stats,
};

const struct arwill_user_runtime *arwill_x86_64_user_mode_init(
    struct arwill_memory *memory,
    uint64_t hhdm_offset,
    const struct arwill_input *input,
    const struct arwill_clock *clock,
    const struct arwill_filesystem *filesystem,
    struct arwill_event_log *log
) {
    user_context.memory = memory;
    user_context.hhdm_offset = hhdm_offset;
    user_context.kernel_cr3 = read_cr3();
    user_context.kernel_rsp = 0;
    user_context.available = 0;
    user_context.hhdm_available = hhdm_offset != 0U;
    user_context.gdt_loaded = 0;
    user_context.tss_loaded = 0;
    user_context.syscall_gate_loaded = 0;
    user_context.runs = 0;
    user_context.syscall_count = 0;
    user_context.bytes_written = 0;
    user_context.bad_syscalls = 0;
    user_context.preemptions = 0;
    user_context.faults = 0;
    user_context.legacy_input = input;
    user_context.clock = clock;
    user_context.filesystem = filesystem;
    user_context.log = log;
    user_context.active_task = 0;
    user_context.next_pid = 1000U;
    user_context.next_slot = 0;
    user_context.active_slice_ticks = 0;

    initialize_gdt();
    enable_no_execute();

    if (memory == 0 || !user_context.hhdm_available) {
        return &user_runtime;
    }
    for (size_t index = 0; index < arwill_user_task_capacity; index++) {
        if (!initialize_address_space(&user_context, &user_context.tasks[index].address_space)) {
            return &user_runtime;
        }
        reset_task_runtime(&user_context, &user_context.tasks[index]);
    }
    user_context.available = 1;
    return &user_runtime;
}

void arwill_x86_64_user_mode_mark_syscall_gate_loaded(void) {
    user_context.syscall_gate_loaded = 1;
}
