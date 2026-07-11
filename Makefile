PROJECT_NAME := Arwill
PROJECT_VERSION := 0.0.1

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
ISO_ROOT := $(BUILD_DIR)/iso-root
KERNEL := $(BUILD_DIR)/kernel.elf
ISO := $(BUILD_DIR)/arwill.iso
SERIAL_LOG := $(BUILD_DIR)/serial-smoke.log

BREW_LLVM_PREFIX := $(shell brew --prefix llvm 2>/dev/null)
BREW_LLD_PREFIX := $(shell brew --prefix lld 2>/dev/null)
CLANG ?= $(if $(BREW_LLVM_PREFIX),$(BREW_LLVM_PREFIX)/bin/clang,clang)
LD_LLD ?= $(if $(BREW_LLD_PREFIX),$(BREW_LLD_PREFIX)/bin/ld.lld,ld.lld)
XORRISO ?= xorriso
QEMU ?= qemu-system-x86_64

CFLAGS := --target=x86_64-elf
CFLAGS += -std=c11 -ffreestanding -fno-stack-protector -fno-stack-check
CFLAGS += -fno-pic -fno-pie -m64 -mno-red-zone -mcmodel=kernel
CFLAGS += -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion
CFLAGS += -Wmissing-prototypes -Wstrict-prototypes
CFLAGS += -Iinclude -Iarch/x86_64/include -Iplatform/qemu/include -Ithird_party/limine
CFLAGS += -DARWILL_PROJECT_NAME=\"$(PROJECT_NAME)\"
CFLAGS += -DARWILL_PROJECT_VERSION=\"$(PROJECT_VERSION)\"

LDFLAGS := -nostdlib -static -z max-page-size=0x1000
LDFLAGS += -T arch/x86_64/linker.ld

SOURCES := \
	kernel/console.c \
	kernel/main.c \
	arch/x86_64/boot/entry.c \
	arch/x86_64/boot/limine_requests.c \
	arch/x86_64/cpu/idle.c \
	platform/qemu/x86_64/serial_console.c

OBJECTS := $(SOURCES:%.c=$(OBJ_DIR)/%.o)

.PHONY: setup build run check clean check-tools check-artifacts smoke

setup:
	@scripts/setup_limine.sh

build: check-tools setup $(ISO)

run: build
	$(QEMU) -M q35 -m 128M -cdrom $(ISO) -boot d -serial stdio -display none -no-reboot -no-shutdown

check: build check-artifacts smoke

clean:
	rm -rf $(BUILD_DIR)

check-tools:
	@scripts/check_prereqs.sh "$(CLANG)" "$(LD_LLD)" "$(XORRISO)" "$(QEMU)"

check-artifacts: $(ISO)
	@scripts/check_artifacts.sh "$(KERNEL)" "$(ISO)"

smoke: $(ISO)
	@scripts/smoke_qemu.sh "$(QEMU)" "$(ISO)" "$(SERIAL_LOG)"

$(KERNEL): $(OBJECTS) arch/x86_64/linker.ld
	@mkdir -p $(dir $@)
	$(LD_LLD) $(LDFLAGS) -o $@ $(OBJECTS)

$(OBJ_DIR)/%.o: %.c
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

third_party/limine/limine:
	@scripts/setup_limine.sh
