#include <stdint.h>

#include <limine.h>

void arwill_limine_entry(void);

__attribute__((used, section(".limine_requests_start_marker")))
static volatile uint64_t arwill_limine_requests_start_marker[4] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile uint64_t arwill_limine_base_revision[3] = LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_entry_point_request arwill_limine_entry_point_request = {
    .id = LIMINE_ENTRY_POINT_REQUEST_ID,
    .revision = 0,
    .response = 0,
    .entry = arwill_limine_entry,
};

__attribute__((used, section(".limine_requests_end_marker")))
static volatile uint64_t arwill_limine_requests_end_marker[2] = LIMINE_REQUESTS_END_MARKER;
