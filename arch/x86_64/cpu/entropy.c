#include <stddef.h>
#include <stdint.h>

#include <arwill/arch/x86_64/entropy.h>
#include <arwill/kernel/entropy.h>

static int rdrand_available(void) {
    uint32_t eax = 1U;
    uint32_t ebx = 0U;
    uint32_t ecx = 0U;
    uint32_t edx = 0U;
    __asm__ volatile(
        "cpuid"
        : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
    );
    (void)ebx;
    (void)edx;
    return (ecx & (1U << 30U)) != 0U;
}

static int rdrand64(uint64_t *value) {
    unsigned char ready = 0U;
    __asm__ volatile(
        "rdrand %0; setc %1"
        : "=r"(*value), "=qm"(ready)
        :
        : "cc"
    );
    return ready != 0U;
}

static int x86_64_entropy_fill(
    void *context,
    uint8_t *output,
    size_t length
) {
    (void)context;
    if ((output == 0 && length != 0U) || !rdrand_available()) {
        return 0;
    }
    size_t offset = 0U;
    while (offset < length) {
        uint64_t value = 0U;
        int ready = 0;
        for (unsigned attempt = 0U; attempt < 16U && !ready; attempt++) {
            ready = rdrand64(&value);
        }
        if (!ready) {
            for (size_t index = 0U; index < length; index++) {
                output[index] = 0U;
            }
            return 0;
        }
        for (unsigned byte = 0U; byte < 8U && offset < length; byte++) {
            output[offset++] = (uint8_t)(value >> (byte * 8U));
        }
    }
    return 1;
}

static const struct arwill_entropy entropy = {
    .context = 0,
    .fill = x86_64_entropy_fill
};

const struct arwill_entropy *arwill_x86_64_entropy(void) {
    return &entropy;
}
