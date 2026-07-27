#include <stdint.h>
#include <stdio.h>

#include <arwill/kernel/entropy.h>
#include <arwill/kernel/realtime.h>

static int failures;

static int expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
    return condition;
}

static int fake_fill(void *context, uint8_t *output, size_t length) {
    uint8_t value = *(const uint8_t *)context;
    for (size_t index = 0U; index < length; index++) {
        output[index] = (uint8_t)(value + index);
    }
    return 1;
}

int main(void) {
    uint8_t seed = 0x40U;
    uint8_t output[4] = { 0U, 0U, 0U, 0U };
    const struct arwill_entropy entropy = {
        .context = &seed,
        .fill = fake_fill
    };
    (void)expect(arwill_entropy_fill(&entropy, output, sizeof(output)) &&
        output[0] == 0x40U && output[3] == 0x43U,
        "entropy contract delegates bounded fill");
    (void)expect(!arwill_entropy_fill(0, output, sizeof(output)),
        "entropy rejects unavailable provider");

    uint64_t seconds = 0U;
    (void)expect(arwill_realtime_calendar_to_unix(
            1970U, 1U, 1U, 0U, 0U, 0U, &seconds) && seconds == 0U,
        "Unix epoch converts");
    (void)expect(arwill_realtime_calendar_to_unix(
            2000U, 2U, 29U, 12U, 34U, 56U, &seconds) &&
            seconds == 951827696U,
        "Gregorian leap day converts");
    (void)expect(arwill_realtime_calendar_to_unix(
            2026U, 7U, 25U, 0U, 0U, 0U, &seconds) &&
            seconds == 1784937600U,
        "current-era date converts");
    (void)expect(!arwill_realtime_calendar_to_unix(
            2100U, 2U, 29U, 0U, 0U, 0U, &seconds),
        "non-leap century date is rejected");

    if (failures != 0) {
        return 1;
    }
    puts("secure runtime tests passed");
    return 0;
}
