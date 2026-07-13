#ifndef ARWILL_KERNEL_CONFIG_H
#define ARWILL_KERNEL_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include <arwill/kernel/filesystem.h>

enum {
    arwill_config_remote_key_capacity = 65
};

enum arwill_config_log_level {
    arwill_config_log_info
};

struct arwill_config {
    const struct arwill_filesystem *filesystem;
    uint32_t version;
    int remote_enabled;
    uint16_t remote_port;
    char remote_key[arwill_config_remote_key_capacity];
    enum arwill_config_log_level log_level;
    int loaded_from_file;
    int valid;
};

void arwill_config_init(
    struct arwill_config *config,
    const struct arwill_filesystem *filesystem
);

int arwill_config_load(struct arwill_config *config);

int arwill_config_set(
    struct arwill_config *config,
    const char *key,
    const char *value
);

int arwill_config_set_remote_key(
    struct arwill_config *config,
    const char *value
);

const char *arwill_config_log_level_name(enum arwill_config_log_level level);

#endif
