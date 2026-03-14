## Q+ OS Demo — Build Pipeline
## Cross-compiles Q+ kernel for x86_64 and boots in QEMU
##
## Prerequisites (on Kali/Ubuntu):
##   sudo apt install gcc-x86-64-linux-gnu nasm qemu-system-x86 grub-pc-bin xorriso
##   Also: build qpc first (see ../compiler/Makefile)
##
## Usage:
##   make          → full build
##   make run      → boot in QEMU (serial only, no GUI)
##   make debug    → QEMU + GDB waiting on port 1234
##   make iso      → create bootable ISO
##   make clean

# ── Toolchain ──────────────────────────────────────────────────
QPC        = ../compiler/build/qpc
CC         = x86_64-linux-gnu-gcc
AS         = nasm
LD         = x86_64-linux-gnu-ld
OBJCOPY    = x86_64-linux-gnu-objcopy

# ── Compiler flags ─────────────────────────────────────────────
CFLAGS  = -ffreestanding -fno-stack-protector -fno-pie -fno-pic \
          -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
          -nostdinc -I../stdlib -O2 -std=c11 \
          -Wall -Wextra -Wno-unused-parameter

ASFLAGS = -f elf64

LDFLAGS = -T linker.ld -nostdlib -z max-page-size=0x1000

# ── Source files ───────────────────────────────────────────────
# Q+ stdlib files to compile (Q+ → C → .o)
QP_SOURCES = \
    ../stdlib/mem/allocator.qp   \
    ../stdlib/mem/paging.qp      \
    ../stdlib/mem/vmm.qp         \
    ../stdlib/cpu/interrupts.qp  \
    ../stdlib/cpu/regs.qp        \
    ../stdlib/drivers/vga.qp     \
    ../stdlib/drivers/serial.qp  \
    ../stdlib/drivers/timer.qp   \
    ../stdlib/drivers/keyboard.qp \
    ../stdlib/drivers/disk.qp    \
    ../stdlib/kernel/scheduler.qp \
    ../stdlib/kernel/process.qp  \
    ../stdlib/kernel/ipc.qp      \
    ../stdlib/kernel/syscall_table.qp \
    ../stdlib/fs/vfs.qp          \
    ../stdlib/fs/fat32.qp        \
    ../stdlib/util/string.qp     \
    ../stdlib/util/fmt.qp        \
    ../stdlib/collections/ringbuffer.qp \
    kernel/kernel_main.qp

# Assembly sources
AS_SOURCES = boot/boot.S

BUILD_DIR = build
OBJ_DIR   = $(BUILD_DIR)/obj

# ── Generated C files (qp → .c) ───────────────────────────────
QP_C_FILES  = $(patsubst %.qp, $(BUILD_DIR)/%.c, $(notdir $(QP_SOURCES)))
OBJS_FROM_QP = $(patsubst %.c, $(OBJ_DIR)/%.o, $(notdir $(QP_C_FILES)))
OBJS_FROM_AS = $(patsubst %.S, $(OBJ_DIR)/%.o, $(notdir $(AS_SOURCES)))

KERNEL_ELF = $(BUILD_DIR)/qplus_kernel.elf
KERNEL_BIN = $(BUILD_DIR)/qplus_kernel.bin

# ── Targets ────────────────────────────────────────────────────
.PHONY: all run debug iso clean dirs

all: dirs $(KERNEL_BIN)
	@echo ""
	@echo "============================================"
	@echo "  Q+ Kernel built: $(KERNEL_BIN)"
	@echo "  Size: $$(wc -c < $(KERNEL_BIN)) bytes"
	@echo "============================================"

dirs:
	@mkdir -p $(BUILD_DIR) $(OBJ_DIR) $(BUILD_DIR)/isodir/boot/grub $(BUILD_DIR)/qp_c

# ── Q+ → C (compile each .qp to a .c in build/) ─────────────
$(BUILD_DIR)/qp_c/%.c: $(filter %/$$(*).qp, $(QP_SOURCES) kernel/%.qp)
	@echo "[QPC] $< → $@"
	$(QPC) build $< -o $@

# Explicit rules for kernel sources
$(BUILD_DIR)/qp_c/allocator.c:   ../stdlib/mem/allocator.qp   ; $(QPC) build $< -o $@
$(BUILD_DIR)/qp_c/paging.c:      ../stdlib/mem/paging.qp      ; $(QPC) build $< -o $@
$(BUILD_DIR)/qp_c/vmm.c:         ../stdlib/mem/vmm.qp         ; $(QPC) build $< -o $@
$(BUILD_DIR)/qp_c/interrupts.c:  ../stdlib/cpu/interrupts.qp  ; $(QPC) build $< -o $@
$(BUILD_DIR)/qp_c/regs.c:        ../stdlib/cpu/regs.qp        ; $(QPC) build $< -o $@
$(BUILD_DIR)/qp_c/vga.c:         ../stdlib/drivers/vga.qp     ; $(QPC) build $< -o $@
$(BUILD_DIR)/qp_c/serial.c:      ../stdlib/drivers/serial.qp  ; $(QPC) build $< -o $@
$(BUILD_DIR)/qp_c/timer.c:       ../stdlib/drivers/timer.qp   ; $(QPC) build $< -o $@
$(BUILD_DIR)/qp_c/keyboard.c:    ../stdlib/drivers/keyboard.qp; $(QPC) build $< -o $@
$(BUILD_DIR)/qp_c/disk.c:        ../stdlib/drivers/disk.qp    ; $(QPC) build $< -o $@
$(BUILD_DIR)/qp_c/scheduler.c:   ../stdlib/kernel/scheduler.qp; $(QPC) build $< -o $@
$(BUILD_DIR)/qp_c/process.c:     ../stdlib/kernel/process.qp  ; $(QPC) build $< -o $@
$(BUILD_DIR)/qp_c/ipc.c:         ../stdlib/kernel/ipc.qp      ; $(QPC) build $< -o $@
$(BUILD_DIR)/qp_c/syscall_table.c: ../stdlib/kernel/syscall_table.qp; $(QPC) build $< -o $@
$(BUILD_DIR)/qp_c/vfs.c:         ../stdlib/fs/vfs.qp          ; $(QPC) build $< -o $@
$(BUILD_DIR)/qp_c/fat32.c:       ../stdlib/fs/fat32.qp        ; $(QPC) build $< -o $@
$(BUILD_DIR)/qp_c/string.c:      ../stdlib/util/string.qp     ; $(QPC) build $< -o $@
$(BUILD_DIR)/qp_c/fmt.c:         ../stdlib/util/fmt.qp        ; $(QPC) build $< -o $@
$(BUILD_DIR)/qp_c/ringbuffer.c:  ../stdlib/collections/ringbuffer.qp; $(QPC) build $< -o $@
$(BUILD_DIR)/qp_c/kernel_main.c: kernel/kernel_main.qp        ; $(QPC) build $< -o $@

# ── .c → .o ───────────────────────────────────────────────────
$(OBJ_DIR)/%.o: $(BUILD_DIR)/qp_c/%.c
	@echo "[CC]  $< → $@"
	$(CC) $(CFLAGS) -c -o $@ $<

# ── Assembly → .o ─────────────────────────────────────────────
$(OBJ_DIR)/boot.o: boot/boot.S
	@echo "[AS]  $< → $@"
	$(AS) $(ASFLAGS) -o $@ $<

ALL_OBJS = $(OBJ_DIR)/boot.o \
    $(OBJ_DIR)/allocator.o $(OBJ_DIR)/paging.o $(OBJ_DIR)/vmm.o \
    $(OBJ_DIR)/interrupts.o $(OBJ_DIR)/regs.o \
    $(OBJ_DIR)/vga.o $(OBJ_DIR)/serial.o $(OBJ_DIR)/timer.o \
    $(OBJ_DIR)/keyboard.o $(OBJ_DIR)/disk.o \
    $(OBJ_DIR)/scheduler.o $(OBJ_DIR)/process.o \
    $(OBJ_DIR)/ipc.o $(OBJ_DIR)/syscall_table.o \
    $(OBJ_DIR)/vfs.o $(OBJ_DIR)/fat32.o \
    $(OBJ_DIR)/string.o $(OBJ_DIR)/fmt.o \
    $(OBJ_DIR)/ringbuffer.o $(OBJ_DIR)/kernel_main.o

# ── Link kernel ELF ───────────────────────────────────────────
$(KERNEL_ELF): $(ALL_OBJS)
	@echo "[LD]  Linking kernel ELF..."
	$(LD) $(LDFLAGS) -o $@ $^
	@echo "[LD]  Done: $@"

# ── Strip to flat binary ──────────────────────────────────────
$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

# ── Bootable ISO via GRUB ─────────────────────────────────────
iso: $(KERNEL_ELF)
	cp $(KERNEL_ELF) $(BUILD_DIR)/isodir/boot/qplus_kernel.elf
	echo 'set timeout=0'                             > $(BUILD_DIR)/isodir/boot/grub/grub.cfg
	echo 'set default=0'                            >> $(BUILD_DIR)/isodir/boot/grub/grub.cfg
	echo 'menuentry "Q+ OS" {'                      >> $(BUILD_DIR)/isodir/boot/grub/grub.cfg
	echo '    multiboot2 /boot/qplus_kernel.elf'   >> $(BUILD_DIR)/isodir/boot/grub/grub.cfg
	echo '    boot'                                 >> $(BUILD_DIR)/isodir/boot/grub/grub.cfg
	echo '}'                                        >> $(BUILD_DIR)/isodir/boot/grub/grub.cfg
	grub-mkrescue -o $(BUILD_DIR)/qplus_os.iso $(BUILD_DIR)/isodir
	@echo "ISO ready: $(BUILD_DIR)/qplus_os.iso"

# ── QEMU run ──────────────────────────────────────────────────
run: iso
	qemu-system-x86_64 \
	    -cdrom $(BUILD_DIR)/qplus_os.iso \
	    -m 128M \
	    -serial stdio \
	    -display none \
	    -no-reboot \
	    -no-shutdown

# ── QEMU debug (connect with: gdb qplus_kernel.elf -ex "target remote :1234") ──
debug: iso
	qemu-system-x86_64 \
	    -cdrom $(BUILD_DIR)/qplus_os.iso \
	    -m 128M \
	    -serial stdio \
	    -display none \
	    -no-reboot \
	    -no-shutdown \
	    -s -S

clean:
	rm -rf $(BUILD_DIR)
	@echo "Clean done."
