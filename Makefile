PROJECT_NAME := Arwill
PROJECT_VERSION := 0.24.0

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
ISO_ROOT := $(BUILD_DIR)/iso-root
KERNEL := $(BUILD_DIR)/kernel.elf
ISO := $(BUILD_DIR)/arwill.iso
DISK_IMAGE := $(BUILD_DIR)/arwill.img
ARFS_SEED := $(BUILD_DIR)/arwill-arfs-seed.img
SMOKE_DISK := $(BUILD_DIR)/arwill-smoke.img
SERIAL_LOG := $(BUILD_DIR)/serial-smoke.log
IDE_SLOT_LOG := $(BUILD_DIR)/ide-slot-smoke.log
ARFS_REGION_LBA := 32768
ARFS_REGION_SECTORS := 2048
HELLO_APP := $(BUILD_DIR)/apps/hello.awp
CALC_APP := $(BUILD_DIR)/apps/calc.awp
EDIT_APP := $(BUILD_DIR)/apps/edit.awp
NETSERVE_APP := $(BUILD_DIR)/apps/netserve.awp
CURL_APP := $(BUILD_DIR)/apps/curl.awp
IPV4_HOST_TEST := $(BUILD_DIR)/tests/ipv4_test
IPV4_HOST_TEST_SOURCES := tests/ipv4_test.c kernel/awp_network.c kernel/clock.c kernel/console.c kernel/ipv4.c kernel/network.c kernel/tcp.c kernel/tcp_stream.c
IPV4_HOST_TEST_HEADERS := include/arwill/kernel/clock.h include/arwill/kernel/console.h \
	include/arwill/kernel/cpu.h include/arwill/kernel/ipv4.h \
	include/arwill/kernel/network.h include/arwill/kernel/tcp.h \
	include/arwill/kernel/tcp_stream.h include/arwill/kernel/awp_network.h
CONFIG_LOG_HOST_TEST := $(BUILD_DIR)/tests/config_log_test
CONFIG_LOG_HOST_TEST_SOURCES := tests/config_log_test.c kernel/clock.c kernel/config.c kernel/filesystem.c kernel/log.c kernel/service.c kernel/text.c
CONFIG_LOG_HOST_TEST_HEADERS := include/arwill/kernel/clock.h include/arwill/kernel/config.h \
	include/arwill/kernel/filesystem.h include/arwill/kernel/ipv4.h \
	include/arwill/kernel/log.h include/arwill/kernel/service.h \
	include/arwill/kernel/text.h
BLOCK_DEVICE_HOST_TEST := $(BUILD_DIR)/tests/block_device_test
BLOCK_DEVICE_HOST_TEST_SOURCES := tests/block_device_test.c kernel/block_device.c
BLOCK_DEVICE_HOST_TEST_HEADERS := include/arwill/kernel/block_device.h
WEB_PROTOCOL_HOST_TEST := $(BUILD_DIR)/tests/web_protocol_test
WEB_PROTOCOL_HOST_TEST_SOURCES := tests/web_protocol_test.c \
	libs/libhttp/http.c libs/libnet/dns.c
WEB_PROTOCOL_HOST_TEST_HEADERS := include/arwill/user/http.h \
	include/arwill/user/dns.h

BREW_LLVM_PREFIX := $(shell brew --prefix llvm 2>/dev/null)
BREW_LLD_PREFIX := $(shell brew --prefix lld 2>/dev/null)
CLANG ?= $(if $(BREW_LLVM_PREFIX),$(BREW_LLVM_PREFIX)/bin/clang,clang)
LD_LLD ?= $(if $(BREW_LLD_PREFIX),$(BREW_LLD_PREFIX)/bin/ld.lld,ld.lld)
XORRISO ?= xorriso
QEMU ?= qemu-system-x86_64
QEMU_MACHINE := pc
QEMU_POWEROFF_EXIT_STATUS := 33
QEMU_POWEROFF_ARGS := -device isa-debug-exit,iobase=0xf4,iosize=0x04
QEMU_STORAGE_ARGS := -drive file=$(DISK_IMAGE),format=raw,if=ide,index=0,media=disk
QEMU_REMOTE_CONSOLE_BIND ?= 127.0.0.1
QEMU_REMOTE_CONSOLE_HOST_PORT ?= 23232
QEMU_REMOTE_CONSOLE_GUEST_PORT ?= 23232
QEMU_NETWORK_ARGS := -netdev user,id=net0,hostfwd=tcp:$(QEMU_REMOTE_CONSOLE_BIND):$(QEMU_REMOTE_CONSOLE_HOST_PORT)-:$(QEMU_REMOTE_CONSOLE_GUEST_PORT) -device e1000,netdev=net0,mac=52:54:00:12:34:56

CFLAGS := --target=x86_64-elf
CFLAGS += -std=c11 -ffreestanding -fno-stack-protector -fno-stack-check
CFLAGS += -fno-pic -fno-pie -m64 -mno-red-zone -mcmodel=kernel
CFLAGS += -mgeneral-regs-only
CFLAGS += -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion
CFLAGS += -Wmissing-prototypes -Wstrict-prototypes
CFLAGS += -MMD -MP
CFLAGS += -Iinclude -Iarch/x86_64/include -Iplatform/qemu/include -Ithird_party/limine
CFLAGS += -DARWILL_PROJECT_NAME=\"$(PROJECT_NAME)\"
CFLAGS += -DARWILL_PROJECT_VERSION=\"$(PROJECT_VERSION)\"
CFLAGS += -DARWILL_ARFS_REGION_LBA=$(ARFS_REGION_LBA)
CFLAGS += -DARWILL_ARFS_REGION_SECTORS=$(ARFS_REGION_SECTORS)

LDFLAGS := -nostdlib -static -z max-page-size=0x1000
LDFLAGS += -T arch/x86_64/linker.ld

SOURCES := \
	kernel/arfs.c \
	kernel/awp_network.c \
	kernel/boot_catalog.c \
	kernel/block_device.c \
	kernel/clock.c \
	kernel/console.c \
	kernel/config.c \
	kernel/device.c \
	kernel/filesystem.c \
	kernel/interrupts.c \
	kernel/input.c \
	kernel/ipv4.c \
	kernel/log.c \
	kernel/main.c \
	kernel/memory.c \
	kernel/network.c \
	kernel/pci.c \
	kernel/power.c \
	kernel/process.c \
	kernel/scheduler.c \
	kernel/service.c \
	kernel/shell.c \
	kernel/tcp.c \
	kernel/tcp_stream.c \
	kernel/text.c \
	kernel/user.c \
	arch/x86_64/boot/framebuffer_console.c \
	arch/x86_64/boot/entry.c \
	arch/x86_64/boot/limine_requests.c \
	arch/x86_64/cpu/idle.c \
	arch/x86_64/cpu/interrupts.c \
	arch/x86_64/cpu/pci.c \
	arch/x86_64/cpu/process_context.c \
	arch/x86_64/cpu/user_mode.c \
	platform/qemu/x86_64/ata_pio.c \
	platform/qemu/x86_64/e1000.c \
	platform/qemu/x86_64/power.c \
	platform/qemu/x86_64/serial_console.c

OBJECTS := $(SOURCES:%.c=$(OBJ_DIR)/%.o)
DEPENDENCIES := $(OBJECTS:.o=.d)

-include $(DEPENDENCIES)

.PHONY: setup build image run utm-recreate check check-host clean check-tools check-artifacts smoke smoke-ide-slot FORCE

setup:
	@scripts/setup_limine.sh

build: check-tools setup $(ISO) $(DISK_IMAGE)

image: build

run: build
	@set +e; \
	$(QEMU) -M $(QEMU_MACHINE) -m 128M -boot c -serial stdio -monitor none -display none -no-reboot $(QEMU_POWEROFF_ARGS) $(QEMU_STORAGE_ARGS) $(QEMU_NETWORK_ARGS); \
	status=$$?; \
	if [ "$$status" -eq "$(QEMU_POWEROFF_EXIT_STATUS)" ]; then exit 0; fi; \
	exit "$$status"

utm-recreate:
	@scripts/recreate_utm.sh

check: build check-host check-artifacts smoke smoke-ide-slot

check-host: $(IPV4_HOST_TEST) $(CONFIG_LOG_HOST_TEST) $(BLOCK_DEVICE_HOST_TEST) $(WEB_PROTOCOL_HOST_TEST)
	@$(IPV4_HOST_TEST)
	@$(CONFIG_LOG_HOST_TEST)
	@$(BLOCK_DEVICE_HOST_TEST)
	@$(WEB_PROTOCOL_HOST_TEST)

clean:
	rm -rf $(BUILD_DIR)

check-tools:
	@scripts/check_prereqs.sh "$(CLANG)" "$(LD_LLD)" "$(XORRISO)" "$(QEMU)"

check-artifacts: $(ISO) $(DISK_IMAGE)
	@scripts/check_artifacts.sh "$(KERNEL)" "$(ISO)" "$(DISK_IMAGE)" \
		"$(ARFS_REGION_LBA)" "$(ARFS_REGION_SECTORS)"

smoke: $(DISK_IMAGE)
	@cp "$(DISK_IMAGE)" "$(SMOKE_DISK)"
	@scripts/smoke_qemu.sh "$(QEMU)" "$(QEMU_MACHINE)" "$(SMOKE_DISK)" \
		"$(SERIAL_LOG)" "$(QEMU_POWEROFF_EXIT_STATUS)" "$(ARFS_REGION_LBA)"

smoke-ide-slot: $(DISK_IMAGE)
	@scripts/smoke_ide_slot.sh "$(QEMU)" "$(QEMU_MACHINE)" "$(DISK_IMAGE)" \
		"$(IDE_SLOT_LOG)" "$(QEMU_POWEROFF_EXIT_STATUS)"

$(IPV4_HOST_TEST): $(IPV4_HOST_TEST_SOURCES) $(IPV4_HOST_TEST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(CLANG) -std=c11 -Wall -Wextra -Werror -Wpedantic -Wconversion \
		-Wsign-conversion -Iinclude $(IPV4_HOST_TEST_SOURCES) -o $@

$(CONFIG_LOG_HOST_TEST): $(CONFIG_LOG_HOST_TEST_SOURCES) $(CONFIG_LOG_HOST_TEST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(CLANG) -std=c11 -Wall -Wextra -Werror -Wpedantic -Wconversion \
		-Wsign-conversion -Iinclude $(CONFIG_LOG_HOST_TEST_SOURCES) -o $@

$(BLOCK_DEVICE_HOST_TEST): $(BLOCK_DEVICE_HOST_TEST_SOURCES) $(BLOCK_DEVICE_HOST_TEST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(CLANG) -std=c11 -Wall -Wextra -Werror -Wpedantic -Wconversion \
		-Wsign-conversion -Iinclude $(BLOCK_DEVICE_HOST_TEST_SOURCES) -o $@

$(WEB_PROTOCOL_HOST_TEST): $(WEB_PROTOCOL_HOST_TEST_SOURCES) $(WEB_PROTOCOL_HOST_TEST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(CLANG) -std=c11 -Wall -Wextra -Werror -Wpedantic -Wconversion \
		-Wsign-conversion -Iinclude $(WEB_PROTOCOL_HOST_TEST_SOURCES) -o $@

$(KERNEL): $(OBJECTS) arch/x86_64/linker.ld
	@mkdir -p $(dir $@)
	$(LD_LLD) $(LDFLAGS) -o $@ $(OBJECTS)

$(OBJ_DIR)/%.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(ISO): $(KERNEL) platform/qemu/limine.conf third_party/limine/limine
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot/limine $(ISO_ROOT)/EFI/BOOT
	cp $(KERNEL) $(ISO_ROOT)/boot/kernel.elf
	cp platform/qemu/limine.conf $(ISO_ROOT)/boot/limine/limine.conf
	cp third_party/limine/limine-bios.sys $(ISO_ROOT)/boot/limine/limine-bios.sys
	cp third_party/limine/limine-bios-cd.bin $(ISO_ROOT)/boot/limine/limine-bios-cd.bin
	cp third_party/limine/limine-uefi-cd.bin $(ISO_ROOT)/boot/limine/limine-uefi-cd.bin
	cp third_party/limine/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI
	$(XORRISO) -as mkisofs -R -r -J \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
		-apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_ROOT) -o $(ISO)
	third_party/limine/limine bios-install $(ISO)

$(ARFS_SEED): scripts/create_test_disk.sh $(HELLO_APP) $(CALC_APP) $(EDIT_APP) $(NETSERVE_APP) $(CURL_APP) Makefile
	@sh scripts/create_test_disk.sh "$@" "$(PROJECT_VERSION)" "$(HELLO_APP)" "$(CALC_APP)" "$(EDIT_APP)" "$(NETSERVE_APP)" "$(CURL_APP)"

$(DISK_IMAGE): $(ISO) $(ARFS_SEED) scripts/create_disk_image.sh Makefile
	@sh scripts/create_disk_image.sh "$@" "$(ISO)" "$(ARFS_SEED)" \
		"$(ARFS_REGION_LBA)" "$(ARFS_REGION_SECTORS)"

FORCE:

$(HELLO_APP): apps/hello/build.sh Makefile
	@sh apps/hello/build.sh "$@"

$(CALC_APP): apps/calc/build.sh apps/calc/calc.c apps/calc/start.S apps/calc/linker.ld Makefile
	@sh apps/calc/build.sh "$@"

$(EDIT_APP): apps/edit/build.sh apps/edit/edit.c apps/edit/start.S apps/edit/linker.ld Makefile
	@sh apps/edit/build.sh "$@"

$(NETSERVE_APP): apps/netserve/build.sh apps/netserve/netserve.c apps/netserve/start.S apps/netserve/linker.ld Makefile
	@sh apps/netserve/build.sh "$@"

$(CURL_APP): apps/curl/build.sh apps/curl/curl.c apps/curl/start.S \
		apps/curl/linker.ld libs/libhttp/http.c libs/libnet/dns.c \
		include/arwill/user/http.h include/arwill/user/dns.h Makefile
	@sh apps/curl/build.sh "$@"

third_party/limine/limine:
	@scripts/setup_limine.sh
