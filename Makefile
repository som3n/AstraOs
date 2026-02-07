CC=gcc
LD=ld
ASM=nasm

CFLAGS=-m32 -ffreestanding -O2 -Wall -Wextra -Iinclude
LDFLAGS=-m elf_i386

BUILD_DIR=build
OBJ_DIR=$(BUILD_DIR)/obj
ISO_DIR=$(BUILD_DIR)/iso

KERNEL_BIN=$(BUILD_DIR)/kernel.bin
ISO_FILE=$(BUILD_DIR)/astra.iso

C_SOURCES= \
	src/kernel/kernel.c \
	src/kernel/shell.c \
	src/kernel/string.c \
	src/kernel/print.c \
	src/cpu/idt.c \
	src/cpu/isr.c \
	src/cpu/irq.c \
	src/cpu/gdt.c \
	src/cpu/power.c \
	src/cpu/timer.c \
	src/drivers/vga.c \
	src/drivers/pic.c \
	src/drivers/keyboard.c \
	src/drivers/ports.c\
	src/drivers/ata.c \
	src/memory/kmalloc.c \
	src/fs/fat16.c 

ASM_SOURCES= \
	src/boot/multiboot.asm \
	src/boot/idt_load.asm \
	src/boot/irq.asm \
	src/boot/gdt_flush.asm

C_OBJECTS=$(patsubst src/%.c,$(OBJ_DIR)/%.o,$(C_SOURCES))
ASM_OBJECTS=$(patsubst src/%.asm,$(OBJ_DIR)/%.o,$(ASM_SOURCES))

all: $(ISO_FILE)

# Create build directories
dirs:
	mkdir -p $(OBJ_DIR)/kernel
	mkdir -p $(OBJ_DIR)/cpu
	mkdir -p $(OBJ_DIR)/drivers
	mkdir -p $(OBJ_DIR)/boot
	mkdir -p $(ISO_DIR)/boot/grub
	mkdir -p $(OBJ_DIR)/memory
	mkdir -p $(OBJ_DIR)/fs

# Compile C files into object files
$(OBJ_DIR)/%.o: src/%.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

# Compile Assembly files into object files
$(OBJ_DIR)/%.o: src/%.asm | dirs
	$(ASM) -f elf32 $< -o $@

# Link all object files into kernel binary
$(KERNEL_BIN): $(C_OBJECTS) $(ASM_OBJECTS) linker.ld
	$(LD) $(LDFLAGS) -T linker.ld -o $(KERNEL_BIN) $(ASM_OBJECTS) $(C_OBJECTS)

# Build ISO image
$(ISO_FILE): $(KERNEL_BIN) config/grub.cfg
	cp $(KERNEL_BIN) $(ISO_DIR)/boot/kernel.bin
	cp config/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub2-mkrescue -o $(ISO_FILE) $(ISO_DIR)

run:
	qemu-system-i386 -boot d -cdrom build/astra.iso -drive format=raw,file=astra_disk.img

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all dirs run clean
