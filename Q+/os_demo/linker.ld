/* Q+ OS Demo — Kernel Linker Script
 * Target: x86_64 bare metal, higher-half kernel at 0xFFFFFFFF80000000
 * Used by: ld -T linker.ld kernel.o ... -o qplus_kernel.elf
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/* Physical load address of kernel (GRUB loads at 2MB) */
PHYS_BASE = 0x0000000000200000;
/* Virtual higher-half base */
VIRT_BASE = 0xFFFFFFFF80000000;
/* Offset between virtual and physical addresses */
VIRT_OFFSET = VIRT_BASE - PHYS_BASE;

SECTIONS {
    /* ── Boot section: compiled at its real physical address ── */
    . = PHYS_BASE;

    .multiboot2 : {
        KEEP(*(.multiboot2))
    }

    /* ── Kernel sections: linked at higher-half virtual address ─ */
    . += VIRT_OFFSET;

    .text ALIGN(4096) : AT(ADDR(.text) - VIRT_OFFSET) {
        *(.text.boot)       /* boot entry code, placed first */
        *(.text)
        *(.text.*)
    }

    .rodata ALIGN(4096) : AT(ADDR(.rodata) - VIRT_OFFSET) {
        *(.rodata)
        *(.rodata.*)
    }

    .data ALIGN(4096) : AT(ADDR(.data) - VIRT_OFFSET) {
        *(.data)
        *(.data.*)
    }

    .bss ALIGN(4096) : AT(ADDR(.bss) - VIRT_OFFSET) {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _bss_end = .;
    }

    /* Symbol: end of kernel image in physical memory */
    _kernel_end_phys = LOADADDR(.bss) + SIZEOF(.bss);

    /* ── Discard ──────────────────────────────────────────── */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.debug*)
    }
}
