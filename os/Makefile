# NovaOS build
CC      := gcc
AS      := nasm
LD      := ld
CFLAGS  := -m32 -std=gnu11 -ffreestanding -fno-pie -fno-pic -fno-stack-protector \
           -fno-asynchronous-unwind-tables -mno-sse -mno-sse2 -mno-mmx -mno-80387 \
           -mno-red-zone -O2 -Wall -Wextra -Ikernel
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib -z noexecstack

BUILD   := build
C_SRC   := $(wildcard kernel/*.c)
OBJS    := $(BUILD)/boot.o $(BUILD)/isr.o $(patsubst kernel/%.c,$(BUILD)/%.o,$(C_SRC))

KERNEL  := $(BUILD)/novaos.elf
ISO     := $(BUILD)/novaos.iso

.PHONY: all iso run run-kernel clean

all: $(KERNEL)

$(BUILD):
	mkdir -p $@

$(BUILD)/boot.o: boot/boot.asm | $(BUILD)
	$(AS) -f elf32 $< -o $@

$(BUILD)/isr.o: kernel/isr.asm | $(BUILD)
	$(AS) -f elf32 $< -o $@

$(BUILD)/%.o: kernel/%.c kernel/*.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

iso: $(ISO)

$(ISO): $(KERNEL) grub.cfg
	mkdir -p $(BUILD)/iso/boot/grub
	cp $(KERNEL) $(BUILD)/iso/boot/novaos.elf
	cp grub.cfg $(BUILD)/iso/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(BUILD)/iso 2>/dev/null

# Boot the ISO through GRUB (same path as real hardware / VirtualBox).
run: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -m 256M -vga std

# Quick boot without GRUB (uses QEMU's built-in multiboot loader).
run-kernel: $(KERNEL)
	qemu-system-i386 -kernel $(KERNEL) -m 256M -vga std

clean:
	rm -rf $(BUILD)
