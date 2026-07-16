#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <arwill/kernel/clock.h>
#include <arwill/kernel/config.h>
#include <arwill/kernel/filesystem.h>
#include <arwill/kernel/log.h>
#include <arwill/kernel/service.h>

enum {
    fake_file_capacity = 1024
};

struct fake_storage {
    char contents[fake_file_capacity];
    size_t length;
    int exists;
    unsigned writes;
};

struct fake_time {
    uint64_t milliseconds;
};

static int copy_text(struct fake_storage *storage, const char *text) {
    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    if (length > sizeof(storage->contents)) {
        return 0;
    }
    for (size_t index = 0; index < length; index++) {
        storage->contents[index] = text[index];
    }
    storage->length = length;
    storage->exists = 1;
    return 1;
}

static int fake_read_file(void *context, const char *path,
    struct arwill_fs_file *file) {
    struct fake_storage *storage = (struct fake_storage *)context;
    (void)path;
    if (storage == 0 || file == 0 || !storage->exists) {
        return 0;
    }
    file->type = arwill_fs_file_text;
    file->contents = storage->contents;
    file->size_bytes = storage->length;
    return 1;
}

static int fake_write_bytes(void *context, const char *path,
    enum arwill_fs_file_type type, const uint8_t *contents, size_t size) {
    struct fake_storage *storage = (struct fake_storage *)context;
    (void)path;
    if (storage == 0 || type != arwill_fs_file_text ||
        size > sizeof(storage->contents) || (contents == 0 && size != 0U)) {
        return 0;
    }
    for (size_t index = 0; index < size; index++) {
        storage->contents[index] = (char)contents[index];
    }
    storage->length = size;
    storage->exists = 1;
    storage->writes++;
    return 1;
}

static uint64_t fake_milliseconds(void *context) {
    const struct fake_time *time = (const struct fake_time *)context;
    return time == 0 ? 0U : time->milliseconds;
}

int arwill_tcp_stream_listen(struct arwill_tcp_stream *stream,
    uint16_t port, uint32_t initial_sequence) {
    (void)initial_sequence;
    if (stream == 0 || port == 0U) {
        return 0;
    }
    stream->listener.port = port;
    stream->listening = 1;
    return 1;
}

void arwill_tcp_stream_stop(struct arwill_tcp_stream *stream) {
    if (stream != 0) {
        stream->listening = 0;
    }
}

int arwill_ipv4_tcp_listen(struct arwill_ipv4_stack *stack,
    struct arwill_tcp_stream *stream, uint16_t port) {
    (void)stack;
    return arwill_tcp_stream_listen(stream, port, 0x41520000U);
}

static int expect(int condition, const char *message) {
    if (condition) {
        return 1;
    }
    fprintf(stderr, "config/log test failed: %s\n", message);
    return 0;
}

int main(void) {
    struct fake_storage storage = { 0 };
    const struct arwill_filesystem filesystem = {
        .name = "fake",
        .context = &storage,
        .list = 0,
        .read_file = fake_read_file,
        .create_directory = 0,
        .write_bytes = fake_write_bytes,
        .remove = 0,
        .storage_stats = 0,
    };
    struct arwill_config config;

    arwill_config_init(&config, &filesystem);
    if (!expect(arwill_config_load(&config), "missing file uses defaults") ||
        !expect(config.valid && config.remote_enabled,
            "defaults remain valid and enabled") ||
        !expect(config.remote_port == 23232U, "default remote port")) {
        return 1;
    }

    if (!copy_text(&storage,
            "config.version=1\n"
            "remote.enabled=true\n"
            "remote.port=24000\n"
            "remote.key=owner-key\n"
            "log.level=info\n") ||
        !expect(arwill_config_load(&config), "valid file parses") ||
        !expect(config.remote_port == 24000U, "configured remote port") ||
        !expect(config.remote_key[0] == 'o', "configured remote key")) {
        return 1;
    }

    if (!expect(arwill_config_set(&config, "remote.port", "23232"),
            "configuration persists") ||
        !expect(storage.writes == 1U, "one write performed") ||
        !expect(config.remote_port == 23232U, "new port applied") ||
        !expect(!arwill_config_set(&config, "remote.port", "0"),
            "invalid port rejected")) {
        return 1;
    }
    if (!expect(arwill_config_set_remote_key(&config, "long-owner-key"),
            "long key persists") ||
        !expect(arwill_config_set_remote_key(&config, "short"),
            "shorter key persists") ||
        !expect(config.remote_key[5] == '\0' && config.remote_key[14] == '\0',
            "shorter key clears old tail")) {
        return 1;
    }

    struct fake_time time = { 0 };
    const struct arwill_clock clock = {
        .name = "fake",
        .context = &time,
        .monotonic_milliseconds = fake_milliseconds,
    };
    struct arwill_event_log log;
    arwill_event_log_init(&log, &clock);
    struct arwill_ipv4_stack ipv4 = { 0 };
    struct arwill_service_manager services;
    arwill_service_manager_init(
        &services, &ipv4, &ipv4.endpoints[0].stream, &config, &log, 1
    );
    if (!expect(services.remote_console_state == arwill_service_running,
            "enabled service starts") ||
        !expect(ipv4.endpoints[0].stream.listener.port == 23232U, "service uses config port") ||
        !expect(arwill_service_remote_console_stop(&services), "service stops") ||
        !expect(services.remote_console_state == arwill_service_stopped,
            "stopped state")) {
        return 1;
    }

    if (!copy_text(&storage,
            "config.version=1\n"
            "remote.enabled=true\n"
            "remote.port=24001\n"
            "remote.key=owner-key\n"
            "log.level=info\n") ||
        !expect(arwill_service_remote_console_restart(&services),
            "restart reloads configuration") ||
        !expect(config.remote_port == 24001U &&
            ipv4.endpoints[0].stream.listener.port == 24001U,
            "restarted service applies new port")) {
        return 1;
    }

    if (!copy_text(&storage,
            "config.version=1\n"
            "remote.enabled=true\n"
            "remote.port=23232\n"
            "remote.port=23233\n"
            "remote.key=owner-key\n"
            "log.level=info\n") ||
        !expect(!arwill_service_remote_console_restart(&services),
            "invalid restart rejected") ||
        !expect(!config.valid && !config.remote_enabled,
            "invalid file disables remote access") ||
        !expect(services.remote_console_state == arwill_service_failed,
            "invalid restart enters failed state")) {
        return 1;
    }

    arwill_event_log_init(&log, &clock);
    for (uint64_t index = 0; index < 70U; index++) {
        time.milliseconds = index * 10U;
        arwill_event_log_record(&log, arwill_log_info, arwill_log_system,
            arwill_log_boot, index, 0U);
    }
    struct arwill_log_entry first;
    struct arwill_log_entry last;
    if (!expect(log.count == arwill_event_log_capacity, "log capacity bounded") ||
        !expect(log.overwritten == 6U, "overwrite count") ||
        !expect(arwill_event_log_entry(&log, 0U, &first), "oldest entry") ||
        !expect(arwill_event_log_entry(&log, log.count - 1U, &last),
            "newest entry") ||
        !expect(first.argument0 == 6U && first.milliseconds == 60U,
            "oldest retained value") ||
        !expect(last.argument0 == 69U && last.milliseconds == 690U,
            "newest retained value")) {
        return 1;
    }

    printf("config/log host tests: passed\n");
    return 0;
}
