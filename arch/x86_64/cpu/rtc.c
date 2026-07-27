#include <stdint.h>

#include <arwill/arch/x86_64/rtc.h>
#include <arwill/kernel/realtime.h>

enum {
    cmos_address_port = 0x70,
    cmos_data_port = 0x71,
    cmos_seconds = 0x00,
    cmos_minutes = 0x02,
    cmos_hours = 0x04,
    cmos_day = 0x07,
    cmos_month = 0x08,
    cmos_year = 0x09,
    cmos_status_a = 0x0a,
    cmos_status_b = 0x0b,
    cmos_century = 0x32
};

struct rtc_sample {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t century;
    uint8_t status_b;
};

static void out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint8_t in8(uint16_t port) {
    uint8_t value = 0U;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint8_t cmos_read(uint8_t index) {
    out8(cmos_address_port, (uint8_t)(0x80U | index));
    return in8(cmos_data_port);
}

static int update_in_progress(void) {
    return (cmos_read(cmos_status_a) & 0x80U) != 0U;
}

static void read_sample(struct rtc_sample *sample) {
    sample->second = cmos_read(cmos_seconds);
    sample->minute = cmos_read(cmos_minutes);
    sample->hour = cmos_read(cmos_hours);
    sample->day = cmos_read(cmos_day);
    sample->month = cmos_read(cmos_month);
    sample->year = cmos_read(cmos_year);
    sample->century = cmos_read(cmos_century);
    sample->status_b = cmos_read(cmos_status_b);
}

static int samples_equal(
    const struct rtc_sample *left,
    const struct rtc_sample *right
) {
    return left->second == right->second &&
        left->minute == right->minute &&
        left->hour == right->hour &&
        left->day == right->day &&
        left->month == right->month &&
        left->year == right->year &&
        left->century == right->century &&
        left->status_b == right->status_b;
}

static unsigned from_bcd(uint8_t value) {
    return (unsigned)(value & 0x0fU) +
        (unsigned)((value >> 4U) & 0x0fU) * 10U;
}

static int rtc_unix_seconds(void *context, uint64_t *seconds) {
    (void)context;
    struct rtc_sample first;
    struct rtc_sample second;
    for (unsigned attempt = 0U; attempt < 8U; attempt++) {
        while (update_in_progress()) {
        }
        read_sample(&first);
        while (update_in_progress()) {
        }
        read_sample(&second);
        if (samples_equal(&first, &second)) {
            const int binary = (first.status_b & 0x04U) != 0U;
            const int twenty_four_hour = (first.status_b & 0x02U) != 0U;
            const int pm = (first.hour & 0x80U) != 0U;
            unsigned hour = binary
                ? (unsigned)(first.hour & 0x7fU)
                : from_bcd((uint8_t)(first.hour & 0x7fU));
            if (!twenty_four_hour) {
                hour %= 12U;
                if (pm) {
                    hour += 12U;
                }
            }
            const unsigned year_low =
                binary ? first.year : from_bcd(first.year);
            const unsigned century =
                binary ? first.century : from_bcd(first.century);
            const unsigned year = century >= 19U && century <= 23U
                ? century * 100U + year_low : 2000U + year_low;
            return arwill_realtime_calendar_to_unix(
                year,
                binary ? first.month : from_bcd(first.month),
                binary ? first.day : from_bcd(first.day),
                hour,
                binary ? first.minute : from_bcd(first.minute),
                binary ? first.second : from_bcd(first.second),
                seconds
            );
        }
    }
    return 0;
}

static const struct arwill_realtime realtime = {
    .context = 0,
    .unix_seconds = rtc_unix_seconds
};

const struct arwill_realtime *arwill_x86_64_rtc(void) {
    return &realtime;
}
