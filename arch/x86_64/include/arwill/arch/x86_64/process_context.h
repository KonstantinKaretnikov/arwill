#ifndef ARWILL_ARCH_X86_64_PROCESS_CONTEXT_H
#define ARWILL_ARCH_X86_64_PROCESS_CONTEXT_H

#include <arwill/kernel/process.h>

const struct arwill_process_context_backend *
arwill_x86_64_process_context_backend(void);

#endif
