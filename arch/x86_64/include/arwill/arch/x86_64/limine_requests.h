#ifndef ARWILL_ARCH_X86_64_LIMINE_REQUESTS_H
#define ARWILL_ARCH_X86_64_LIMINE_REQUESTS_H

#include <limine.h>

const struct limine_hhdm_response *arwill_limine_hhdm_response(void);

const struct limine_memmap_response *arwill_limine_memmap_response(void);

#endif
