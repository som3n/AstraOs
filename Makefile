CC=gcc
LD=ld
ASM=nasm

CFLAGS=-m32 -ffreestanding -O2 -Wall -Wextra -Iinclude
LDFLAGS=-m elf_i386

BUILD_DIR=build
OBJ_DIR=$(BUILD_DIR)/obj
ISO_DIR=$(BUILD_DIR)/iso

DISK_IMG=astra_disk.img

KERNEL_BIN=$(BUILD_DIR)/kernel.bin
ISO_FILE=$(BUILD_DIR)/astra.iso

USER_DIR=user
USER_OBJ_DIR=$(OBJ_DIR)/userprog
USER_BUILD_DIR=$(BUILD_DIR)/user
USER_ELF=$(USER_BUILD_DIR)/INIT.ELF

ARTIFACTS_DIR=$(BUILD_DIR)/artifacts
ARTIFACTS_TGZ=$(BUILD_DIR)/astraos_artifacts.tar.gz

C_SOURCES= \
	src/kernel/kernel.c \
	src/kernel/shell.c \
	src/kernel/string.c \
	src/kernel/print.c \
	src/kernel/syscall.c \
	src/kernel/syscall_api.c \
	src/kernel/elf32.c \
	src/kernel/exec.c \
	src/cpu/idt.c \
	src/cpu/isr.c \
	src/cpu/irq.c \
	src/cpu/gdt.c \
	src/cpu/tss.c \
	src/cpu/power.c \
	src/cpu/timer.c \
	src/cpu/usermode.c \
	src/drivers/vga.c \
	src/drivers/pic.c \
	src/drivers/keyboard.c \
	src/drivers/ports.c\
	src/drivers/ata.c \
	src/memory/kmalloc.c \
	src/memory/paging.c \
	src/fs/fat16.c \
	src/user/init.c

USER_APPS=INIT LS PWD ECHO CAT

USER_APP_INIT_C=user/apps/init.c
USER_APP_LS_C=user/apps/ls.c
USER_APP_PWD_C=user/apps/pwd.c
USER_APP_ECHO_C=user/apps/echo.c
USER_APP_CAT_C=user/apps/cat_hello.c

USER_ELF_INIT=$(USER_BUILD_DIR)/INIT.ELF
USER_ELF_LS=$(USER_BUILD_DIR)/LS.ELF
USER_ELF_PWD=$(USER_BUILD_DIR)/PWD.ELF
USER_ELF_ECHO=$(USER_BUILD_DIR)/ECHO.ELF
USER_ELF_CAT=$(USER_BUILD_DIR)/CAT.ELF

USER_ELFS=$(USER_ELF_INIT) $(USER_ELF_LS) $(USER_ELF_PWD) $(USER_ELF_ECHO) $(USER_ELF_CAT)

ASM_SOURCES= \
	src/boot/multiboot.asm \
	src/boot/idt_load.asm \
	src/boot/irq.asm \
	src/boot/gdt_flush.asm \
	src/boot/tss_flush.asm \
	src/boot/isr.asm \
	src/boot/userlib.asm

C_OBJECTS=$(patsubst src/%.c,$(OBJ_DIR)/%.o,$(C_SOURCES))
ASM_OBJECTS=$(patsubst src/%.asm,$(OBJ_DIR)/%.o,$(ASM_SOURCES))
USER_C_OBJECTS_COMMON=$(USER_OBJ_DIR)/src/kernel/syscall_api.o
USER_ASM_OBJECTS_COMMON=$(USER_OBJ_DIR)/user/start.o

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
	mkdir -p $(OBJ_DIR)/user
	mkdir -p $(USER_OBJ_DIR)/user
	mkdir -p $(USER_OBJ_DIR)/user/apps
	mkdir -p $(USER_OBJ_DIR)/src/kernel
	mkdir -p $(USER_BUILD_DIR)

# Compile C files into object files
$(OBJ_DIR)/%.o: src/%.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

# Compile Assembly files into object files
$(OBJ_DIR)/%.o: src/%.asm | dirs
	$(ASM) -f elf32 $< -o $@

# Compile user C files into object files
$(USER_OBJ_DIR)/%.o: %.c | dirs
	$(CC) $(CFLAGS) -nostdlib -fno-pie -no-pie -c $< -o $@

# Compile user ASM files into object files
$(USER_OBJ_DIR)/%.o: %.asm | dirs
	$(ASM) -f elf32 $< -o $@

# Build user ELFs (ET_EXEC at 0x00200000)
userprogs: $(USER_ELFS)

$(USER_C_OBJECTS_COMMON): src/kernel/syscall_api.c | dirs
	$(CC) $(CFLAGS) -nostdlib -fno-pie -no-pie -c $< -o $@

$(USER_ASM_OBJECTS_COMMON): user/start.asm | dirs
	$(ASM) -f elf32 $< -o $@

$(USER_OBJ_DIR)/user/apps/%.o: user/apps/%.c | dirs
	$(CC) $(CFLAGS) -nostdlib -fno-pie -no-pie -c $< -o $@

$(USER_ELF_INIT): $(USER_OBJ_DIR)/user/apps/init.o $(USER_ASM_OBJECTS_COMMON) $(USER_C_OBJECTS_COMMON) user/linker.ld
	$(LD) $(LDFLAGS) -T user/linker.ld -o $@ $(USER_ASM_OBJECTS_COMMON) $< $(USER_C_OBJECTS_COMMON)

$(USER_ELF_LS): $(USER_OBJ_DIR)/user/apps/ls.o $(USER_ASM_OBJECTS_COMMON) $(USER_C_OBJECTS_COMMON) user/linker.ld
	$(LD) $(LDFLAGS) -T user/linker.ld -o $@ $(USER_ASM_OBJECTS_COMMON) $< $(USER_C_OBJECTS_COMMON)

$(USER_ELF_PWD): $(USER_OBJ_DIR)/user/apps/pwd.o $(USER_ASM_OBJECTS_COMMON) $(USER_C_OBJECTS_COMMON) user/linker.ld
	$(LD) $(LDFLAGS) -T user/linker.ld -o $@ $(USER_ASM_OBJECTS_COMMON) $< $(USER_C_OBJECTS_COMMON)

$(USER_ELF_ECHO): $(USER_OBJ_DIR)/user/apps/echo.o $(USER_ASM_OBJECTS_COMMON) $(USER_C_OBJECTS_COMMON) user/linker.ld
	$(LD) $(LDFLAGS) -T user/linker.ld -o $@ $(USER_ASM_OBJECTS_COMMON) $< $(USER_C_OBJECTS_COMMON)

$(USER_ELF_CAT): $(USER_OBJ_DIR)/user/apps/cat_hello.o $(USER_ASM_OBJECTS_COMMON) $(USER_C_OBJECTS_COMMON) user/linker.ld
	$(LD) $(LDFLAGS) -T user/linker.ld -o $@ $(USER_ASM_OBJECTS_COMMON) $< $(USER_C_OBJECTS_COMMON)

install-userprogs: userprogs
	mmd -i $(DISK_IMG) ::/BIN || true
	mcopy -o -i $(DISK_IMG) $(USER_ELF_INIT) ::/BIN/INIT.ELF
	mcopy -o -i $(DISK_IMG) $(USER_ELF_LS) ::/BIN/LS.ELF
	mcopy -o -i $(DISK_IMG) $(USER_ELF_PWD) ::/BIN/PWD.ELF
	mcopy -o -i $(DISK_IMG) $(USER_ELF_ECHO) ::/BIN/ECHO.ELF
	mcopy -o -i $(DISK_IMG) $(USER_ELF_CAT) ::/BIN/CAT.ELF
	mdir -i $(DISK_IMG) ::/BIN

# Build a single folder with the "current required binaries":
# - ISO boot image
# - kernel binary
# - user INIT.ELF
# - disk image copy with /BIN/INIT.ELF installed
out: $(ISO_FILE) userprogs
	rm -rf $(ARTIFACTS_DIR)
	mkdir -p $(ARTIFACTS_DIR)
	cp $(ISO_FILE) $(ARTIFACTS_DIR)/
	cp $(KERNEL_BIN) $(ARTIFACTS_DIR)/
	cp $(USER_ELF_INIT) $(ARTIFACTS_DIR)/INIT.ELF
	cp $(USER_ELF_LS) $(ARTIFACTS_DIR)/LS.ELF
	cp $(USER_ELF_PWD) $(ARTIFACTS_DIR)/PWD.ELF
	cp $(USER_ELF_ECHO) $(ARTIFACTS_DIR)/ECHO.ELF
	cp $(USER_ELF_CAT) $(ARTIFACTS_DIR)/CAT.ELF
	cp $(DISK_IMG) $(ARTIFACTS_DIR)/$(DISK_IMG)
	mmd -i $(ARTIFACTS_DIR)/$(DISK_IMG) ::/BIN || true
	mcopy -o -i $(ARTIFACTS_DIR)/$(DISK_IMG) $(USER_ELF_INIT) ::/BIN/INIT.ELF
	mcopy -o -i $(ARTIFACTS_DIR)/$(DISK_IMG) $(USER_ELF_LS) ::/BIN/LS.ELF
	mcopy -o -i $(ARTIFACTS_DIR)/$(DISK_IMG) $(USER_ELF_PWD) ::/BIN/PWD.ELF
	mcopy -o -i $(ARTIFACTS_DIR)/$(DISK_IMG) $(USER_ELF_ECHO) ::/BIN/ECHO.ELF
	mcopy -o -i $(ARTIFACTS_DIR)/$(DISK_IMG) $(USER_ELF_CAT) ::/BIN/CAT.ELF
	mdir -i $(ARTIFACTS_DIR)/$(DISK_IMG) ::/BIN > $(ARTIFACTS_DIR)/BIN.TXT

bundle: out
	tar -czf $(ARTIFACTS_TGZ) -C $(ARTIFACTS_DIR) .

# Link all object files into kernel binary
$(KERNEL_BIN): $(C_OBJECTS) $(ASM_OBJECTS) linker.ld
	$(LD) $(LDFLAGS) -T linker.ld -o $(KERNEL_BIN) $(ASM_OBJECTS) $(C_OBJECTS)

# Build ISO image
$(ISO_FILE): $(KERNEL_BIN) config/grub.cfg
	cp $(KERNEL_BIN) $(ISO_DIR)/boot/kernel.bin
	cp config/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub2-mkrescue -o $(ISO_FILE) $(ISO_DIR)

run:
	qemu-system-i386 -boot d -cdrom $(ISO_FILE) -drive format=raw,file=$(DISK_IMG)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all dirs run clean userprogs install-userprogs out bundle
