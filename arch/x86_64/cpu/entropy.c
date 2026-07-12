#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/entropy.h>

enum {
    cpuid_feature_leaf = 1,
    cpuid_rdrand_bit = 30,
    rdrand_retry_limit = 16,
};

static void cpuid(
    uint32_t leaf,
    uint32_t *eax,
    uint32_t *ebx,
    uint32_t *ecx,
    uint32_t *edx
) {
    uint32_t output_eax;
    uint32_t output_ebx;
    uint32_t output_ecx;
    uint32_t output_edx;

    __asm__ volatile(
        "cpuid"
        : "=a"(output_eax), "=b"(output_ebx), "=c"(output_ecx), "=d"(output_edx)
        : "a"(leaf), "c"(0U)
    );

    *eax = output_eax;
    *ebx = output_ebx;
    *ecx = output_ecx;
    *edx = output_edx;
}

static int rdrand64(uint64_t *value) {
    unsigned char succeeded;

    __asm__ volatile(
        "rdrand %0; setc %1"
        : "=r"(*value), "=qm"(succeeded)
    );

    return succeeded != 0U;
}

static void clear_bytes(uint8_t *output, size_t length) {
    for (size_t index = 0; index < length; index++) {
        output[index] = 0U;
    }
}

const char *arwill_entropy_source_name(void) {
    return "x86_64 rdrand";
}

int arwill_entropy_available(void) {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    cpuid(0U, &eax, &ebx, &ecx, &edx);
    if (eax < cpuid_feature_leaf) {
        return 0;
    }

    cpuid(cpuid_feature_leaf, &eax, &ebx, &ecx, &edx);
    return (ecx & ((uint32_t)1U << cpuid_rdrand_bit)) != 0U;
}

int arwill_entropy_fill(uint8_t *output, size_t length) {
    size_t written = 0;

    if ((output == 0 && length != 0U) || !arwill_entropy_available()) {
        return 0;
    }

    while (written < length) {
        uint64_t value = 0;
        int acquired = 0;

        for (size_t retry = 0; retry < rdrand_retry_limit; retry++) {
            if (rdrand64(&value)) {
                acquired = 1;
                break;
            }
        }

        if (!acquired) {
            clear_bytes(output, length);
            return 0;
        }

        for (size_t byte = 0; byte < sizeof(value) && written < length; byte++) {
            output[written] = (uint8_t)(value >> (byte * 8U));
            written++;
        }
    }

    return 1;
}
