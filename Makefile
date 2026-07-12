PROJECT_NAME := Arwill
PROJECT_VERSION := 0.14.0

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
ISO_ROOT := $(BUILD_DIR)/iso-root
KERNEL := $(BUILD_DIR)/kernel.elf
ISO := $(BUILD_DIR)/arwill.iso
TEST_DISK := $(BUILD_DIR)/arwill-test-disk.img
SERIAL_LOG := $(BUILD_DIR)/serial-smoke.log
HELLO_APP := $(BUILD_DIR)/apps/hello.awp
CALC_APP := $(BUILD_DIR)/apps/calc.awp

BREW_LLVM_PREFIX := $(shell brew --prefix llvm 2>/dev/null)
BREW_LLD_PREFIX := $(shell brew --prefix lld 2>/dev/null)
CLANG ?= $(if $(BREW_LLVM_PREFIX),$(BREW_LLVM_PREFIX)/bin/clang,clang)
LD_LLD ?= $(if $(BREW_LLD_PREFIX),$(BREW_LLD_PREFIX)/bin/ld.lld,ld.lld)
XORRISO ?= xorriso
QEMU ?= qemu-system-x86_64
QEMU_MACHINE := pc
QEMU_POWEROFF_EXIT_STATUS := 33
QEMU_POWEROFF_ARGS := -device isa-debug-exit,iobase=0xf4,iosize=0x04
QEMU_STORAGE_ARGS := -drive file=$(TEST_DISK),format=raw,if=ide,index=0,media=disk
QEMU_NETWORK_ARGS := -netdev user,id=net0 -device e1000,netdev=net0,mac=52:54:00:12:34:56

CFLAGS := --target=x86_64-elf
CFLAGS += -std=c11 -ffreestanding -fno-stack-protector -fno-stack-check
CFLAGS += -fno-pic -fno-pie -m64 -mno-red-zone -mcmodel=kernel
CFLAGS += -mgeneral-regs-only
CFLAGS += -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion
CFLAGS += -Wmissing-prototypes -Wstrict-prototypes
CFLAGS += -Iinclude -Iarch/x86_64/include -Iplatform/qemu/include -Ithird_party/limine
CFLAGS += -DARWILL_PROJECT_NAME=\"$(PROJECT_NAME)\"
CFLAGS += -DARWILL_PROJECT_VERSION=\"$(PROJECT_VERSION)\"

LDFLAGS := -nostdlib -static -z max-page-size=0x1000
LDFLAGS += -T arch/x86_64/linker.ld

SOURCES := \
	kernel/arfs.c \
	kernel/boot_catalog.c \
	kernel/block_device.c \
	kernel/clock.c \
	kernel/console.c \
	kernel/device.c \
	kernel/filesystem.c \
	kernel/interrupts.c \
	kernel/input.c \
	kernel/ipv4.c \
	kernel/main.c \
	kernel/memory.c \
	kernel/network.c \
	kernel/pci.c \
	kernel/power.c \
	kernel/process.c \
	kernel/scheduler.c \
	kernel/shell.c \
	kernel/tcp.c \
	kernel/user.c \
	arch/x86_64/boot/framebuffer_console.c \
	arch/x86_64/boot/entry.c \
	arch/x86_64/boot/limine_requests.c \
	arch/x86_64/cpu/idle.c \
	arch/x86_64/cpu/interrupts.c \
	arch/x86_64/cpu/pci.c \
	arch/x86_64/cpu/user_mode.c \
	platform/qemu/x86_64/ata_pio.c \
	platform/qemu/x86_64/e1000.c \
	platform/qemu/x86_64/power.c \
	platform/qemu/x86_64/serial_console.c

OBJECTS := $(SOURCES:%.c=$(OBJ_DIR)/%.o)

.PHONY: setup build run check clean check-tools check-artifacts smoke FORCE

setup:
	@scripts/setup_limine.sh

build: check-tools setup $(ISO)

run: build $(TEST_DISK)
	@set +e; \
	$(QEMU) -M $(QEMU_MACHINE) -m 128M -cdrom $(ISO) -boot d -serial stdio -monitor none -display none -no-reboot $(QEMU_POWEROFF_ARGS) $(QEMU_STORAGE_ARGS) $(QEMU_NETWORK_ARGS); \
	status=$$?; \
	if [ "$$status" -eq "$(QEMU_POWEROFF_EXIT_STATUS)" ]; then exit 0; fi; \
	exit "$$status"

check: build check-artifacts smoke

clean:
	rm -rf $(BUILD_DIR)

check-tools:
	@scripts/check_prereqs.sh "$(CLANG)" "$(LD_LLD)" "$(XORRISO)" "$(QEMU)"

check-artifacts: $(ISO)
	@scripts/check_artifacts.sh "$(KERNEL)" "$(ISO)"

smoke: $(ISO) $(TEST_DISK)
	@scripts/smoke_qemu.sh "$(QEMU)" "$(QEMU_MACHINE)" "$(ISO)" "$(TEST_DISK)" "$(SERIAL_LOG)" "$(QEMU_POWEROFF_EXIT_STATUS)"

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

$(TEST_DISK): scripts/create_test_disk.sh $(HELLO_APP) $(CALC_APP) Makefile FORCE
	@sh scripts/create_test_disk.sh "$@" "$(PROJECT_VERSION)" "$(HELLO_APP)" "$(CALC_APP)"

FORCE:

$(HELLO_APP): apps/hello/build.sh Makefile
	@sh apps/hello/build.sh "$@"

$(CALC_APP): apps/calc/build.sh apps/calc/calc.c apps/calc/start.S apps/calc/linker.ld Makefile
	@sh apps/calc/build.sh "$@"

third_party/limine/limine:
	@scripts/setup_limine.sh
