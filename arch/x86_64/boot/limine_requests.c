#include <stdint.h>

#include <limine.h>

#include <arwill/arch/x86_64/limine_requests.h>

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

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request arwill_limine_memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request arwill_limine_hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = 0,
};

__attribute__((used, section(".limine_requests_end_marker")))
static volatile uint64_t arwill_limine_requests_end_marker[2] = LIMINE_REQUESTS_END_MARKER;

const struct limine_hhdm_response *arwill_limine_hhdm_response(void) {
    return arwill_limine_hhdm_request.response;
}

const struct limine_memmap_response *arwill_limine_memmap_response(void) {
    return arwill_limine_memmap_request.response;
}
