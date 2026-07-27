#include <arwill/kernel/realtime.h>

static int leap_year(unsigned year) {
    return year % 4U == 0U &&
        (year % 100U != 0U || year % 400U == 0U);
}

static unsigned days_in_month(unsigned year, unsigned month) {
    static const uint8_t days[] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };
    if (month == 0U || month > 12U) {
        return 0U;
    }
    return month == 2U && leap_year(year) ? 29U : days[month - 1U];
}

int arwill_realtime_unix_seconds(
    const struct arwill_realtime *realtime,
    uint64_t *seconds
) {
    if (realtime == 0 || realtime->unix_seconds == 0 || seconds == 0) {
        return 0;
    }
    return realtime->unix_seconds(realtime->context, seconds);
}

int arwill_realtime_calendar_to_unix(
    unsigned year,
    unsigned month,
    unsigned day,
    unsigned hour,
    unsigned minute,
    unsigned second,
    uint64_t *seconds
) {
    if (seconds == 0 || year < 1970U || year > 2399U ||
        day == 0U || day > days_in_month(year, month) ||
        hour > 23U || minute > 59U || second > 60U) {
        return 0;
    }

    uint64_t days = 0U;
    for (unsigned current = 1970U; current < year; current++) {
        days += leap_year(current) ? 366U : 365U;
    }
    for (unsigned current = 1U; current < month; current++) {
        days += days_in_month(year, current);
    }
    days += day - 1U;
    *seconds = days * 86400U + (uint64_t)hour * 3600U +
        (uint64_t)minute * 60U + second;
    return 1;
}
