# Q+ Programming Language

> A compiled, safe, systems programming language for operating systems, kernels, and bare-metal firmware.

```
Philosophy:  C-level control · Rust-level safety · Zig-level simplicity
Target:      x86-64, ARM64, RISC-V
Output:      Freestanding C11 → ELF binary → Bootable kernel
```

---

## Status

| Component              | Status |
|------------------------|--------|
| Lexer                  | ✅ Complete (30+ tests) |
| Parser                 | ✅ Complete (full recursive descent, 85+ AST nodes) |
| Semantic Analyzer      | ✅ Complete (symbol table, 2-pass, scope resolution) |
| Security Analyzer      | ✅ Complete (12 analysis passes) |
| Code Generator         | ✅ Complete (C11 transpiler, all node types) |
| Standard Library       | ✅ Complete skeleton (11 files, all major components) |
| VSCode Extension       | ✅ Syntax highlighting for `.qp` files |
| Language Specification | ✅ SPEC.md (15 sections, EBNF grammar, full API reference) |

---

## Project Structure

```
Q+/
├── compiler/
│   ├── src/
│   │   ├── main.c          — CLI: lex / parse / build
│   │   ├── lexer.c         — Tokenizer (100+ token types)
│   │   ├── parser.c        — Recursive descent parser → AST
│   │   ├── sema.c          — Semantic analyzer + symbol table
│   │   ├── security.c      — 12-pass security analyzer
│   │   └── codegen.c       — C11 transpiler
│   ├── include/qpc/
│   │   ├── common.h        — Arena allocator, types, macros
│   │   ├── token.h         — Token kinds + keyword table
│   │   ├── ast.h           — 85+ AST node types
│   │   ├── sema.h          — Symbol table API
│   │   ├── security.h      — Security pass API
│   │   └── codegen.h       — Codegen API
│   ├── tests/
│   │   ├── test_lexer.c    — 30 lexer unit tests
│   │   └── samples/
│   │       ├── hello.qp    — Minimal kernel entry
│   │       └── driver.qp   — PS2 keyboard driver
│   └── Makefile
├── stdlib/
│   ├── mem/
│   │   ├── allocator.qp    — BumpAllocator, SlabAllocator
│   │   └── paging.qp       — x86-64 page tables, PhysAddr/VirtAddr
│   ├── cpu/
│   │   ├── interrupts.qp   — IDT, PIC 8259, ISR registration
│   │   └── regs.qp         — MSR, CR0-CR4, CpuContext, switch_context
│   ├── drivers/
│   │   ├── vga.qp          — VGA text mode 80x25
│   │   ├── serial.qp       — UART 16550 at 115200 baud
│   │   └── timer.qp        — PIT 8253/8254
│   ├── kernel/
│   │   └── scheduler.qp    — Round-robin task scheduler
│   ├── collections/
│   │   └── ringbuffer.qp   — Lock-free SPSC ring buffer
│   ├── fs/
│   │   └── vfs.qp          — VFS: VfsNode, FdTable, VfsOps trait
│   └── util/
│       └── string.qp       — str_len/eq/copy, mem_set/copy/cmp, int→str
├── qplus-vscode/
│   ├── package.json
│   ├── language-configuration.json
│   └── syntaxes/
│       └── qplus.tmLanguage.json   — Syntax highlighting grammar
├── SPEC.md                 — Complete language specification
└── README.md
```

---

## Build on Windows (MSYS2/MinGW)

```powershell
cd C:\Users\User\Desktop\Q+\compiler

# Build the compiler
mingw32-make clean
mingw32-make

# Run lexer tests
mingw32-make test

# Compile a Q+ source file to C
.\build\qpc.exe build tests\samples\hello.qp
# → Produces output.c

# Parse only (print AST)
.\build\qpc.exe parse tests\samples\driver.qp
```

---

## Build & Test on Kali Linux

### Step 1 — Install dependencies

```bash
sudo apt update
sudo apt install -y build-essential gcc make qemu-system-x86 \
    binutils-source nasm xorriso grub-pc-bin grub-common \
    gcc-multilib git
```

### Step 2 — Install cross-compiler

```bash
# Option A: Use an existing x86_64-elf cross-compiler
sudo apt install -y gcc-x86-64-linux-gnu

# Option B: Build your own (osdev standard)
# Follow: https://osdev.wiki/GCC_Cross-Compiler
```

### Step 3 — Build qpc (the Q+ compiler)

```bash
cd ~/Desktop/Q+/compiler
make clean && make
# → builds build/qpc (Linux binary)
```

### Step 4 — Compile a Q+ kernel source

```bash
# Compile Q+ → C
./build/qpc build tests/samples/hello.qp
# → output.c

# Verify the generated C is valid
gcc -fsyntax-only -Wall -ffreestanding output.c
```

### Step 5 — Cross-compile to bare-metal binary

```bash
# Compile output.c to a flat kernel ELF
x86_64-linux-gnu-gcc \
    -ffreestanding \
    -nostdlib \
    -nostdinc \
    -fno-stack-protector \
    -mno-red-zone \
    -O2 \
    -T kernel.ld \
    -o kernel.elf \
    output.c

# Or just an object file
x86_64-linux-gnu-gcc -ffreestanding -c output.c -o kernel.o
```

### Step 6 — Create a minimal linker script (`kernel.ld`)

```bash
cat > kernel.ld << 'EOF'
ENTRY(_start)
SECTIONS {
    . = 0x100000;
    .text   : { *(.text.boot) *(.text)   }
    .rodata : { *(.rodata)               }
    .data   : { *(.data)                 }
    .bss    : { *(.bss) *(COMMON)        }
}
EOF
```

### Step 7 — Create bootable ISO with GRUB

```bash
mkdir -p iso/boot/grub

# Copy kernel
cp kernel.elf iso/boot/kernel.elf

# Write GRUB config
cat > iso/boot/grub/grub.cfg << 'EOF'
set timeout=0
set default=0
menuentry "Q+ Kernel" {
    multiboot /boot/kernel.elf
    boot
}
EOF

# Build ISO
grub-mkrescue -o qplus.iso iso/
```

### Step 8 — Run in QEMU

```bash
# Basic run
qemu-system-x86_64 -cdrom qplus.iso -serial stdio

# With more options (debug-friendly)
qemu-system-x86_64 \
    -cdrom qplus.iso \
    -serial stdio \
    -m 128M \
    -no-reboot \
    -d int,cpu_reset \
    2> qemu.log
```

### Step 9 — Debug with GDB + QEMU

```bash
# Terminal 1: Launch QEMU with GDB stub
qemu-system-x86_64 \
    -cdrom qplus.iso \
    -serial stdio \
    -m 128M \
    -s -S    # -s = gdbserver on :1234, -S = pause at startup

# Terminal 2: Attach GDB
gdb kernel.elf \
    -ex "target remote :1234" \
    -ex "break _start" \
    -ex "continue"
```

### Step 10 — Run qpc tests

```bash
cd ~/Desktop/Q+/compiler

# Lexer test suite (30 tests)
make test

# Parse all samples
make parse_hello
make parse_driver

# Build (codegen) all samples
make build_hello
make build_driver
```

### Full pipeline one-liner

```bash
cd ~/Desktop/Q+/compiler && \
make clean && make && \
./build/qpc build tests/samples/hello.qp && \
x86_64-linux-gnu-gcc -ffreestanding -nostdlib -fno-stack-protector \
    -mno-red-zone -O2 -c output.c -o kernel.o && \
echo "SUCCESS: kernel.o ready for linking"
```

---

## VSCode Extension

```bash
# Install in VSCode (from Q+/ directory)
cd qplus-vscode
# Copy to VSCode extensions folder (Linux)
cp -r . ~/.vscode/extensions/qplus-lang-0.1.0/
# Then reload VSCode

# Or install via CLI
code --install-extension .
```

---

## Example: Hello Kernel

```qplus
// tests/samples/hello.qp
module kernel;

const KERNEL_VERSION: u32 = 0x0001;
const VGA_BASE: usize = 0xB8000;

#[no_mangle]
pub fn _start() -> ! {
    // Write 'Q' directly to VGA memory
    unsafe {
        let vga: ptr<volatile u16> = VGA_BASE as ptr<volatile u16>;
        *vga = 0x0F51;  // White 'Q' on black background
    }
    loop {
        unsafe { asm!("hlt"); }
    }
}
```

**Compile and run:**
```bash
./build/qpc build tests/samples/hello.qp
# → output.c generated
# → Compile, link, run in QEMU (see steps above)
```

---

## Language Quick Reference

```qplus
// Types
let x: u32 = 42;
let s: &str = "hello";
let p: ptr<u8> = null;           // only in unsafe

// Functions
fn add(a: i32, b: i32) -> i32 { return a + b; }
interrupt fn timer() -> void { ... }
syscall sys_exit(code: i32) -> ! { ... }

// Control flow
if x > 0 { ... } else { ... }
match state { TaskState::Ready => ..., _ => ..., }
for byte in buffer[0..len] { ... }
loop { unsafe { asm!("hlt"); } }
defer resource.release();

// OS constructs
driver VGA { ... }
unsafe { port::out_u8(0x20, 0x20); }
unsafe { asm!("sti"); }
unsafe { mmio::write32(0xFEE00300, value); }

// Error handling
let result: Result<u8, Error> = try_read()?;
```

---

## License

MIT License — See LICENSE file.
