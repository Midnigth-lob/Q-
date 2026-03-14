/* Q+ OS Demo — x86_64 Multiboot2 Boot Stub
 * File: boot.S
 * Assembled with: nasm -f elf64 boot.S -o boot.o
 *
 * Responsibilities:
 *   1. Embed a Multiboot2 header so GRUB/QEMU loads us
 *   2. Set up a minimal stack
 *   3. Call kernel_main() (compiled from Q+ → C)
 */

bits 32

; ── Multiboot2 header constants ────────────────────────────────
MULTIBOOT2_MAGIC     equ 0xE85250D6
MULTIBOOT2_ARCH_I386 equ 0           ; i386 protected mode
HEADER_LEN           equ (mboot_end - mboot_start)
HEADER_CHECKSUM      equ -(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH_I386 + HEADER_LEN)

; Kernel virtual base (mapped at 2 MB physical for simplicity)
KERNEL_BASE equ 0xFFFFFFFF80000000
STACK_SIZE  equ 65536               ; 64 KiB

section .multiboot2, progbits, alloc
align 8
mboot_start:
    dd MULTIBOOT2_MAGIC
    dd MULTIBOOT2_ARCH_I386
    dd HEADER_LEN
    dd HEADER_CHECKSUM & 0xFFFFFFFF

    ; ── End tag ──────────────────────────────────────────────
    dw 0    ; type = end
    dw 0    ; flags
    dd 8    ; size
mboot_end:

; ── Bootstrap stack (BSS) ──────────────────────────────────────
section .bss
align 16
stack_bottom:
    resb STACK_SIZE
stack_top:

; ── Minimal GDT (64-bit mode) ─────────────────────────────────
section .rodata
align 8
gdt64:
    dq 0                    ; null descriptor
    dq 0x00AF9A000000FFFF   ; code, 64-bit, DPL=0
    dq 0x00AF92000000FFFF   ; data, 64-bit, DPL=0
gdt64_end:

gdt64_ptr:
    dw (gdt64_end - gdt64 - 1)
    dq gdt64

; ── _start: entered in 32-bit protected mode by bootloader ────
section .text
global _start
extern kernel_main          ; defined in Q+→C compiled kernel

_start:
    ; Disable interrupts while we set up
    cli

    ; Set up 32-bit stack (we'll replace it once in long mode)
    mov esp, stack_top - KERNEL_BASE   ; physical address to start

    ; ── Enter 64-bit long mode ────────────────────────────────
    ; Step 1: Enable PAE
    mov eax, cr4
    or  eax, (1 << 5)       ; CR4.PAE
    mov cr4, eax

    ; Step 2: Load identity-mapped PML4 (pre-built in data)
    ; For simplicity we do a minimal static mapping of the first 2MB
    extern boot_pml4         ; defined in paging_setup.c (generated C)
    mov eax, boot_pml4 - KERNEL_BASE
    mov cr3, eax

    ; Step 3: Enable long mode in EFER
    mov ecx, 0xC0000080      ; IA32_EFER MSR
    rdmsr
    or  eax, (1 << 8)        ; LME
    wrmsr

    ; Step 4: Enable paging + protected mode
    mov eax, cr0
    or  eax, (1 << 31) | (1 << 0)   ; PG | PE
    mov cr0, eax

    ; Step 5: Far jump to 64-bit code segment
    jmp 0x08:long_mode_entry

bits 64
long_mode_entry:
    ; Set all data segment registers to the 64-bit data descriptor
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Set up proper 64-bit stack (virtual address)
    mov rsp, stack_top

    ; Clear BSS
    extern _bss_start
    extern _bss_end
    mov rdi, _bss_start
    mov rcx, _bss_end
    sub rcx, rdi
    xor al, al
    rep stosb

    ; Call into Q+-generated kernel_main(multiboot_magic, multiboot_info_phys)
    ; EBX holds multiboot info structure physical address (set by bootloader)
    ; EAX holds multiboot magic
    xor rdi, rdi
    xor rsi, rsi
    ; These were in eax/ebx before the mode switch (we lose them — pass 0)
    ; In a production kernel you'd save them before entering long mode.
    call kernel_main

    ; Halt forever if kernel_main returns
.halt:
    hlt
    jmp .halt

; ── __syscall_entry — SYSCALL instruction handler ─────────────
; Called by user-mode SYSCALL: saves state, calls syscall_dispatch, returns
global __syscall_entry
extern syscall_dispatch
__syscall_entry:
    ; On entry: RCX=return RIP, R11=RFLAGS, RAX=syscall#
    ; Args: RDI RSI RDX R10 R8 R9
    push rcx          ; save user RIP
    push r11          ; save user RFLAGS

    ; Remap R10 → RCX for the C ABI (4th arg)
    mov  rcx, r10

    ; syscall_dispatch(num, a1, a2, a3, a4, a5, a6)
    ; num=RAX, a1=RDI, a2=RSI, a3=RDX, a4=R10→RCX, a5=R8, a6=R9
    ; RAX already set. Move to RDI for first arg:
    mov  rdi, rax
    ; a1..a6 already in place

    call syscall_dispatch

    ; Result in RAX — return to user
    pop  r11
    pop  rcx
    sysretq

; ── context_switch — switches register state ──────────────────
; extern fn context_switch(from: *mut CpuContext, to: *const CpuContext)
; from = RDI, to = RSI
global context_switch
context_switch:
    ; Save callee-saved regs + flags into *from (CpuContext layout)
    mov  [rdi + 0],  r15
    mov  [rdi + 8],  r14
    mov  [rdi + 16], r13
    mov  [rdi + 24], r12
    mov  [rdi + 32], rbp
    mov  [rdi + 40], rbx
    lea  rax, [rip + .return_here]
    mov  [rdi + 48], rax   ; rip = where we'll resume
    mov  [rdi + 56], rsp
    pushfq
    pop  qword [rdi + 64]  ; rflags
    ; cs/ss are kernel-fixed, skip

    ; Restore target thread context from *to
    mov  r15, [rsi + 0]
    mov  r14, [rsi + 8]
    mov  r13, [rsi + 16]
    mov  r12, [rsi + 24]
    mov  rbp, [rsi + 32]
    mov  rbx, [rsi + 40]
    mov  rsp, [rsi + 56]
    push qword [rsi + 64]  ; rflags
    popfq
    jmp  qword [rsi + 48]  ; jump to rip

.return_here:
    ret
