#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <arwill/kernel/console.h>

enum {
    output_capacity = 512
};

struct test_console_context {
    char output[output_capacity];
    size_t length;
    unsigned presentation_count;
    const char *presented_name;
    const char *presented_version;
};

static void capture_write(void *raw_context, const char *text) {
    struct test_console_context *context =
        (struct test_console_context *)raw_context;

    while (*text != '\0' && context->length + 1U < sizeof(context->output)) {
        context->output[context->length++] = *text++;
    }
    context->output[context->length] = '\0';
}

static void capture_presentation(
    void *raw_context,
    const char *system_name,
    const char *system_version
) {
    struct test_console_context *context =
        (struct test_console_context *)raw_context;
    context->presentation_count++;
    context->presented_name = system_name;
    context->presented_version = system_version;
}

static int expect(int condition, const char *message) {
    if (condition) {
        return 1;
    }

    fprintf(stderr, "console test failed: %s\n", message);
    return 0;
}

int main(void) {
    struct test_console_context fallback_context = { 0 };
    const struct arwill_console fallback_console = {
        .context = &fallback_context,
        .write = capture_write,
        .show_boot_banner = 0,
    };

    arwill_console_show_boot_banner(&fallback_console, "Arwill", "1.2.3");
    if (!expect(strstr(fallback_context.output, "ARCHITECTURE IS THE PRODUCT") != 0,
            "fallback includes the manifesto line") ||
        !expect(strstr(fallback_context.output, "Arwill 1.2.3 ready") != 0,
            "fallback includes identity and readiness")) {
        return 1;
    }

    struct test_console_context specialized_context = { 0 };
    const struct arwill_console specialized_console = {
        .context = &specialized_context,
        .write = capture_write,
        .show_boot_banner = capture_presentation,
    };

    arwill_console_show_boot_banner(&specialized_console, "Arwill", "4.5.6");
    if (!expect(specialized_context.presentation_count == 1U,
            "specialized presentation is called once") ||
        !expect(specialized_context.presented_name != 0 &&
                strcmp(specialized_context.presented_name, "Arwill") == 0,
            "specialized presentation receives the name") ||
        !expect(specialized_context.presented_version != 0 &&
                strcmp(specialized_context.presented_version, "4.5.6") == 0,
            "specialized presentation receives the version") ||
        !expect(specialized_context.length == 0U,
            "specialized presentation replaces the fallback")) {
        return 1;
    }

    return 0;
}
