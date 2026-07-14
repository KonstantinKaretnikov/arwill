#include <stddef.h>
#include <stdint.h>

#include <limine.h>

#include <arwill/arch/x86_64/framebuffer_console.h>
#include <arwill/kernel/console.h>

enum {
    glyph_width = 5,
    glyph_height = 7,
    cell_width = 6,
    cell_height = 8,
    framebuffer_bpp = 32,
    color_background = 0x00101010U,
    color_foreground = 0x00d8dee9U,
};

struct framebuffer_console_context {
    const struct arwill_console *serial;
    volatile uint8_t *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint64_t column;
    uint64_t row;
    uint64_t columns;
    uint64_t rows;
    int available;
};

static struct framebuffer_console_context framebuffer_console_context;

static uint8_t uppercase(uint8_t value) {
    if (value >= (uint8_t)'a' && value <= (uint8_t)'z') {
        return (uint8_t)(value - (uint8_t)'a' + (uint8_t)'A');
    }

    return value;
}

static const uint8_t *glyph_rows(uint8_t value) {
    static const uint8_t unknown[glyph_height] = { 0x1f, 0x11, 0x04, 0x04, 0x00, 0x04, 0x00 };
    static const uint8_t space[glyph_height] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t digits[10][glyph_height] = {
        { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e },
        { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e },
        { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f },
        { 0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e },
        { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 },
        { 0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e },
        { 0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e },
        { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
        { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e },
        { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e },
    };
    static const uint8_t letters[26][glyph_height] = {
        { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
        { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e },
        { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e },
        { 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e },
        { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f },
        { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 },
        { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f },
        { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
        { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e },
        { 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c },
        { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },
        { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f },
        { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 },
        { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },
        { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
        { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 },
        { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d },
        { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 },
        { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e },
        { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
        { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
        { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 },
        { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11 },
        { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 },
        { 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 },
        { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f },
    };
    static const uint8_t colon[glyph_height] = { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 };
    static const uint8_t slash[glyph_height] = { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 };
    static const uint8_t dash[glyph_height] = { 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00 };
    static const uint8_t dot[glyph_height] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c };
    static const uint8_t comma[glyph_height] = { 0x00, 0x00, 0x00, 0x00, 0x0c, 0x04, 0x08 };
    static const uint8_t greater[glyph_height] = { 0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10 };

    value = uppercase(value);

    if (value == (uint8_t)' ') {
        return space;
    }

    if (value >= (uint8_t)'0' && value <= (uint8_t)'9') {
        return digits[value - (uint8_t)'0'];
    }

    if (value >= (uint8_t)'A' && value <= (uint8_t)'Z') {
        return letters[value - (uint8_t)'A'];
    }

    switch (value) {
        case (uint8_t)':':
            return colon;
        case (uint8_t)'/':
        case (uint8_t)'\\':
            return slash;
        case (uint8_t)'-':
        case (uint8_t)'_':
            return dash;
        case (uint8_t)'.':
            return dot;
        case (uint8_t)',':
            return comma;
        case (uint8_t)'>':
            return greater;
        default:
            return unknown;
    }
}

static void draw_pixel(
    struct framebuffer_console_context *context,
    uint64_t x,
    uint64_t y,
    uint32_t color
) {
    if (x >= context->width || y >= context->height) {
        return;
    }

    volatile uint32_t *pixel =
        (volatile uint32_t *)(context->address + (y * context->pitch) + (x * 4U));

    *pixel = color;
}

static void clear_cell(struct framebuffer_console_context *context, uint64_t column, uint64_t row) {
    const uint64_t x_start = column * cell_width;
    const uint64_t y_start = row * cell_height;

    for (uint64_t y = 0; y < cell_height; y++) {
        for (uint64_t x = 0; x < cell_width; x++) {
            draw_pixel(context, x_start + x, y_start + y, color_background);
        }
    }
}

static void draw_char(struct framebuffer_console_context *context, uint8_t value) {
    const uint8_t *rows = glyph_rows(value);
    const uint64_t x_start = context->column * cell_width;
    const uint64_t y_start = context->row * cell_height;

    clear_cell(context, context->column, context->row);

    for (uint64_t y = 0; y < glyph_height; y++) {
        const uint8_t bits = rows[y];

        for (uint64_t x = 0; x < glyph_width; x++) {
            const uint8_t mask = (uint8_t)(1U << (glyph_width - x - 1U));
            const uint32_t color = (bits & mask) == 0U ? color_background : color_foreground;

            draw_pixel(context, x_start + x, y_start + y, color);
        }
    }
}

static void clear_screen(struct framebuffer_console_context *context) {
    for (uint64_t y = 0; y < context->height; y++) {
        for (uint64_t x = 0; x < context->width; x++) {
            draw_pixel(context, x, y, color_background);
        }
    }

    context->column = 0;
    context->row = 0;
}

static void newline(struct framebuffer_console_context *context) {
    context->column = 0;

    if (context->row + 1U < context->rows) {
        context->row++;
        return;
    }

    clear_screen(context);
}

static void backspace(struct framebuffer_console_context *context) {
    if (context->column > 0U) {
        context->column--;
        clear_cell(context, context->column, context->row);
    }
}

static void framebuffer_write_byte(struct framebuffer_console_context *context, uint8_t value) {
    if (value == (uint8_t)'\r') {
        context->column = 0;
        return;
    }

    if (value == (uint8_t)'\n') {
        newline(context);
        return;
    }

    if (value == (uint8_t)'\b') {
        backspace(context);
        return;
    }

    if (value < 0x20U || value > 0x7eU) {
        return;
    }

    if (context->column >= context->columns) {
        newline(context);
    }

    draw_char(context, value);
    context->column++;

    if (context->column >= context->columns) {
        newline(context);
    }
}

static void framebuffer_console_write(void *raw_context, const char *text) {
    struct framebuffer_console_context *context =
        (struct framebuffer_console_context *)raw_context;

    if (context == 0) {
        return;
    }

    if (context->serial != 0) {
        arwill_console_write(context->serial, text);
    }

    if (!context->available) {
        return;
    }

    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        framebuffer_write_byte(context, (uint8_t)*cursor);
    }
}

static const struct arwill_console framebuffer_console = {
    .context = &framebuffer_console_context,
    .write = framebuffer_console_write,
};

const struct arwill_console *arwill_x86_64_framebuffer_console_init(
    const struct limine_framebuffer_response *response,
    const struct arwill_console *serial_console
) {
    framebuffer_console_context.serial = serial_console;
    framebuffer_console_context.available = 0;
    framebuffer_console_context.address = 0;
    framebuffer_console_context.width = 0;
    framebuffer_console_context.height = 0;
    framebuffer_console_context.pitch = 0;
    framebuffer_console_context.column = 0;
    framebuffer_console_context.row = 0;
    framebuffer_console_context.columns = 0;
    framebuffer_console_context.rows = 0;

    if (response != 0 &&
        response->framebuffer_count > 0U &&
        response->framebuffers != 0 &&
        response->framebuffers[0] != 0 &&
        response->framebuffers[0]->address != 0 &&
        response->framebuffers[0]->bpp == framebuffer_bpp) {
        const struct limine_framebuffer *framebuffer = response->framebuffers[0];

        framebuffer_console_context.address = (volatile uint8_t *)framebuffer->address;
        framebuffer_console_context.width = framebuffer->width;
        framebuffer_console_context.height = framebuffer->height;
        framebuffer_console_context.pitch = framebuffer->pitch;
        framebuffer_console_context.columns = framebuffer->width / cell_width;
        framebuffer_console_context.rows = framebuffer->height / cell_height;

        if (framebuffer_console_context.columns > 0U &&
            framebuffer_console_context.rows > 0U) {
            framebuffer_console_context.available = 1;
            clear_screen(&framebuffer_console_context);
        }
    }

    return &framebuffer_console;
}

int arwill_x86_64_framebuffer_console_available(void) {
    return framebuffer_console_context.available;
}

const char *arwill_x86_64_framebuffer_console_status(void) {
    return framebuffer_console_context.available ? "ready" : "unavailable";
}
