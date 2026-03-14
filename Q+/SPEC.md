# Q+ Programming Language — Complete Specification
**Version 0.1 · March 2026**

---

## 1. Philosophy & Design Decisions

Q+ is a **compiled, statically-typed, systems programming language** designed exclusively for building operating systems, kernels, device drivers, and bare-metal firmware from scratch.

**Core principles:**
- **Safety by default** — safe code has no undefined behavior, no raw pointers, no OOB access
- **Explicit unsafety** — hardware access, interrupts, MMIO, and port I/O require `unsafe {}` blocks, always auditable
- **Zero hidden runtime** — no garbage collector, no runtime overhead, generates lean C11 that compiles with any cross-compiler
- **First-class OS constructs** — `driver`, `syscall`, `interrupt`, `mmio`, `port` are language keywords, not library hacks
- **Deterministic** — no exceptions, no implicit heap allocation, no magic; everything is explicit

**Inspired by:** Rust (ownership, safety model), Zig (comptime, explicitness), C (zero-cost abstraction, system control)

**Transpiles to C11** → compiled with a `gcc`/`clang` cross-compiler (e.g., `x86_64-elf-gcc`) → linked into a bootable ELF.  
*Rationale: C is the universal cross-compilation target. LLVM IR would require the full LLVM toolchain; transpiling to C allows any OS development cross-compiler to be used.*

---

## 2. Syntax — Complete EBNF Grammar

```ebnf
Program       ::= Decl*
Decl          ::= Attribute* Visibility? DeclInner
Visibility    ::= 'pub' | 'priv'
DeclInner     ::= FnDecl | StructDecl | EnumDecl | UnionDecl | TraitDecl
                | ImplBlock | ConstDecl | StaticDecl | TypeAlias
                | DriverDecl | SyscallDecl | ModuleDecl | ImportDecl

ModuleDecl    ::= 'module' Path ';'
ImportDecl    ::= 'import' Path '{' ImportItems '}' ';'
ImportItems   ::= (Ident (',' Ident)*)?

FnDecl        ::= ('interrupt')? 'fn' Ident '(' Params ')' ('->' Type)? (Block | ';')
DriverDecl    ::= 'driver' Ident '{' (Field | FnDecl)* '}'
SyscallDecl   ::= 'syscall' Ident '(' Params ')' ('->' Type)? Block

StructDecl    ::= 'struct' Ident '{' StructFields '}'
StructFields  ::= (Visibility? Ident ':' Type ','?)*
EnumDecl      ::= 'enum' Ident (':' Type)? '{' Variant (',' Variant)* '}'
Variant       ::= Ident ('=' Expr)? ('(' Type* ')')?
UnionDecl     ::= 'union' Ident '{' StructFields '}'
TraitDecl     ::= 'trait' Ident '{' FnDecl* '}'
ImplBlock     ::= 'impl' ('<' Ident '>')? (Ident 'for')? Type '{' FnDecl* '}'
ConstDecl     ::= 'const' Ident ':' Type '=' Expr ';'
StaticDecl    ::= 'static' 'mut'? Ident ':' Type '=' Expr ';'
TypeAlias     ::= 'type' Ident '=' Type ';'

Params        ::= (Param (',' Param)*)?
Param         ::= ('&' 'mut'? 'self') | Ident ':' Type
Type          ::= PrimitiveType | NamedType | RefType | PtrType | OwnType
                | SliceType | ArrayType | FnPtrType | NeverType | InferType
PrimitiveType ::= 'u8'|'u16'|'u32'|'u64'|'i8'|'i16'|'i32'|'i64'
                | 'f32'|'f64'|'bool'|'char'|'str'|'void'|'usize'|'isize'
RefType       ::= '&' 'mut'? Type
PtrType       ::= 'ptr' '<' 'volatile'? Type '>'
OwnType       ::= 'own' '<' Type '>'
SliceType     ::= 'slice' '<' Type '>'
ArrayType     ::= '[' Type ';' Expr ']'
NeverType     ::= '!'
InferType     ::= '_'
NamedType     ::= Ident ('<' Type (',' Type)* '>')?

Block         ::= '{' Stmt* Expr? '}'
Stmt          ::= LetStmt | AssignStmt | ReturnStmt | BreakStmt
                | ContinueStmt | DeferStmt | ExprStmt
LetStmt       ::= 'let' 'mut'? Ident (':' Type)? '=' Expr ';'
AssignStmt    ::= Expr AssignOp Expr ';'
ReturnStmt    ::= 'return' Expr? ';'
BreakStmt     ::= 'break' Ident? ';'
ContinueStmt  ::= 'continue' Ident? ';'
DeferStmt     ::= 'defer' Expr ';'
ExprStmt      ::= Expr ';'

Expr          ::= BinaryExpr | CastExpr | UnaryExpr | PostfixExpr | PrimaryExpr
PrimaryExpr   ::= Literal | Ident | Path | Block | IfExpr | MatchExpr
                | WhileExpr | ForExpr | LoopExpr | UnsafeBlock | AsmExpr
                | ArrayLit | StructLit | '(' Expr ')'
UnsafeBlock   ::= 'unsafe' Block
AsmExpr       ::= 'asm!' '(' StringLit ')'

IfExpr        ::= 'if' Expr Block ('else' (Block | IfExpr))?
MatchExpr     ::= 'match' Expr '{' MatchArm* '}'
MatchArm      ::= Pattern ('if' Expr)? '=>' (Expr | Block) ','?
ForExpr       ::= 'for' Ident 'in' Expr Block
WhileExpr     ::= 'while' Expr Block
LoopExpr      ::= 'loop' Block

AssignOp      ::= '='|'+='|'-='|'*='|'/='|'%='|'&='|'|='|'^='|'<<='|'>>='
BinaryOp      ::= '+'|'-'|'*'|'/'|'%'|'&'|'|'|'^'|'<<'|'>>'
                | '=='|'!='|'<'|'>'|'<='|'>='|'and'|'or'|'..'|'..='
UnaryOp       ::= '-'|'~'|'not'|'&'|'&mut'|'*'
```

---

## 3. Type System

### 3.1 Primitive Types

| Q+ Type  | C Equivalent  | Size    | Notes                          |
|----------|---------------|---------|--------------------------------|
| `u8`     | `uint8_t`     | 1 byte  | Unsigned integer               |
| `u16`    | `uint16_t`    | 2 bytes |                                |
| `u32`    | `uint32_t`    | 4 bytes |                                |
| `u64`    | `uint64_t`    | 8 bytes |                                |
| `i8`     | `int8_t`      | 1 byte  | Signed integer                 |
| `i16`    | `int16_t`     | 2 bytes |                                |
| `i32`    | `int32_t`     | 4 bytes |                                |
| `i64`    | `int64_t`     | 8 bytes |                                |
| `f32`    | `float`       | 4 bytes |                                |
| `f64`    | `double`      | 8 bytes |                                |
| `bool`   | `bool`        | 1 byte  | `true` / `false`               |
| `char`   | `uint32_t`    | 4 bytes | Unicode codepoint              |
| `str`    | `const char*` | fat ptr | `{ data: *u8, length: usize }` |
| `void`   | `void`        | —       | No value                       |
| `usize`  | `size_t`      | 8 bytes | Platform address size          |
| `isize`  | `ptrdiff_t`   | 8 bytes |                                |
| `!`      | `_Noreturn`   | —       | Never returns                  |

### 3.2 Composite Types

```qplus
// Struct
struct PageEntry { pub addr: u64, pub flags: u32 }

// Enum with repr and discriminants
enum TaskState : u8 { Ready = 0, Running = 1, Blocked = 2, Dead = 3 }

// Tagged union (enum with payloads)
enum Result<T, E> { Ok(T), Err(E) }
enum Option<T>    { Some(T), None }

// Type alias
type PhysAddr = u64;
type Pid = u32;

// Union (unsafe by nature — access must be in unsafe block)
union Register { full: u64, low: u32, byte: u8 }
```

### 3.3 Pointer and Reference Types

| Type              | Description                                   | Context        |
|-------------------|-----------------------------------------------|----------------|
| `&T`              | Immutable reference — guaranteed non-null     | Safe           |
| `&mut T`          | Mutable reference — single at any time        | Safe           |
| `ptr<T>`          | Raw pointer — may be null, no lifetime        | `unsafe` only  |
| `ptr<volatile T>` | Volatile raw pointer — for MMIO              | `unsafe` only  |
| `own<T>`          | Owning heap pointer — single owner, freed on drop | Safe      |
| `slice<T>`        | Fat pointer `{ ptr, len }` — bounds-checked   | Safe           |
| `[T; N]`          | Fixed-size stack array — size must be const   | Safe           |

---

## 4. Memory Model & Ownership

- **No implicit heap allocation.** All memory is either stack-allocated, in a static region, or explicitly allocated via an `Allocator`.
- **`own<T>`** is the owned heap pointer. When it goes out of scope, its destructor calls the associated allocator's `free`.
- **References (`&T`, `&mut T`)** enforce single mutable reference rules at compile time (borrow checker, phase 2 of roadmap).
- **`ptr<T>`** raw pointers have no lifetime or null checks — only valid inside `unsafe {}`.
- **`defer`** statement runs cleanup at scope exit (like Go), useful for releasing locks/resources.
- **No garbage collector, no reference counting, no runtime.** The output C file links against nothing except optionally `libgcc` for software FP.

---

## 5. Safe / Unsafe Modes

### Safe mode (default)
- No raw pointers (`ptr<T>`)
- No inline assembly
- No port I/O (`port::`)
- All array accesses bounds-checked
- No null dereference
- No casting between unrelated types

### Unsafe mode (`unsafe {}` block)
- Raw pointer arithmetic
- Inline assembly (`asm!`)
- Port I/O (`port::in_u8`, `port::out_u8`, etc.)
- MMIO reads/writes via `ptr<volatile T>`
- Type punning via `union`
- FFI (`extern fn` calls)

**Rule:** Every `unsafe {}` block is an audit scope. The security analyzer reports every unsafe block in the compilation. Interrupt handlers and drivers that call unsafe code are flagged with a warning.

---

## 6. OS-Specific Constructs

### 6.1 `driver`
Declares a hardware driver as a combined struct+method unit. Generates a C `typedef struct` with prefixed function names.

```qplus
driver PS2Keyboard {
    buffer: RingBuffer<u8, 256>,
    shift:  bool,

    pub fn init() -> PS2Keyboard { ... }

    #[irq_handler]
    interrupt fn irq_handler(self: &mut Self) -> void {
        let scancode: u8 = unsafe { port::in_u8(0x60) };
        self.buffer.push(scancode);
        unsafe { port::out_u8(0x20, 0x20); }  // EOI
    }

    pub fn read(self: &mut Self) -> Option<u8> {
        return self.buffer.pop();
    }
}
```

**Generated C:**
```c
typedef struct PS2Keyboard PS2Keyboard;
struct PS2Keyboard { RingBuffer_u8_256 buffer; bool shift; };
PS2Keyboard PS2Keyboard_init(void);
__attribute__((interrupt)) void PS2Keyboard_irq_handler(PS2Keyboard *self);
```

### 6.2 `syscall`
Marks a function as a kernel syscall entry point. Validated by the security analyzer (ptr args must be checked).

```qplus
syscall sys_write(fd: i32, buf: ptr<u8>, len: usize) -> isize {
    if not is_userspace_addr(buf, len) { return -14; }
    // ...
}
```

### 6.3 `interrupt fn`
An interrupt service routine. The compiler emits `__attribute__((interrupt))` and ensures no blocking calls inside.

```qplus
interrupt fn timer_isr(frame: &InterruptFrame) -> void {
    unsafe { TICKS += 1; }
    unsafe { port::out_u8(0x20, 0x20); }
}
```

### 6.4 `mmio`
Named MMIO region at a fixed physical address. Always generates `volatile` accesses.

```qplus
mmio UART0 at 0x3F8 {
    data:    u8,
    ier:     u8,
    lcr:     u8,
}
// Access: UART0.data = 0x41;  → *(volatile uint8_t*)(0x3F8 + offset) = 0x41;
```

### 6.5 Port I/O
```qplus
unsafe {
    port::out_u8(0x20, 0x20);          // outb 0x20, 0x20
    let status: u8 = port::in_u8(0x64);
    port::out_u16(port, value);
    port::out_u32(port, value);
}
```
**Generated C:** Uses `__asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));`

### 6.6 Inline Assembly
```qplus
unsafe { asm!("hlt"); }
unsafe { asm!("mov cr3, rax", cr3_value); }
```
**Generated C:** `__asm__ volatile ("hlt");`

---

## 7. Error Handling

Q+ has **no exceptions**. Errors are values.

```qplus
// Result<T, E> — the standard error type
fn init_uart(port: u16) -> Result<Serial, DriverError> {
    if port == 0 { return Err(DriverError::InvalidPort); }
    return Ok(Serial::init(port));
}

// ?  operator — propagate error (early return if Err)
fn setup() -> Result<void, DriverError> {
    let uart = init_uart(0x3F8)?;    // returns Err(...) if failed
    uart.write("OK\n");
    return Ok(());
}

// Option<T> — nullable value without null pointers
fn find_process(pid: u32) -> Option<&Process> {
    // returns Some(&proc) or None
}
```

---

## 8. Compilation Pipeline

```
Source (.qp)
     │
     ▼ Lexer (lexer.c)
  Tokens
     │
     ▼ Parser (parser.c)
   AST (85+ node types)
     │
     ▼ Semantic Analyzer (sema.c)
  Typed AST + Symbol Table
     │
     ▼ Security Analyzer (security.c) — 12 passes
  Annotated AST + Diagnostics
     │
     ▼ Code Generator (codegen.c)
  output.c  (freestanding C11)
     │
     ▼ gcc/clang cross-compiler
  kernel.o / driver.o
     │
     ▼ Linker (ld with kernel.ld)
  kernel.elf
     │
     ▼ objcopy / grub-mkrescue
  kernel.iso / kernel.img
     │
     ▼ QEMU / VirtualBox
  Running OS
```

**Compiler commands:**
```bash
# Q+ compile
./qpc build kernel/main.qp           # produces output.c

# Cross-compile (Kali / any Linux)
x86_64-elf-gcc -ffreestanding -O2 -c output.c -o kernel.o
x86_64-elf-ld -T kernel.ld -o kernel.elf kernel.o

# Run in QEMU
qemu-system-x86_64 -drive format=raw,file=kernel.elf -serial stdio
```

---

## 9. Security Analyzer — 12 Passes

| Pass | Severity | Description |
|------|----------|-------------|
| 1 | ERROR    | `ptr<T>` declared without initialization |
| 2 | WARNING  | Non-constant array index (potential OOB) |
| 3 | WARNING  | `unsafe {}` block without audit comment |
| 4 | ERROR    | Direct dereference of `null` literal |
| 5 | ERROR    | MMIO write without `volatile` qualifier |
| 6 | ERROR    | `port::` access outside `unsafe {}` |
| 7 | WARNING  | `syscall` receives `ptr<u8>` without visible bounds check |
| 8 | WARNING  | Multiple `static mut` mutations in one function (data race risk) |
| 9 | WARNING  | Driver buffer indexing without bounds check |
| 10| WARNING  | Narrowing `as` cast (possible silent truncation) |
| 11| WARNING  | `interrupt fn` body contains potentially blocking calls |
| 12| WARNING  | Stack array > 4096 bytes (stack overflow risk) |

---

## 10. AI Engine Design (Non-LLM)

The Q+ AI engine is a **deterministic, AST-aware autocomplete and code-generation system**. It is NOT a large language model and will never invent non-existent APIs.

### Architecture
```
Input:
  - Partial AST (from current file up to cursor)
  - Symbol table of current module
  - Type context at cursor position
  - Function/driver signatures from imported modules
  - Doc comments and #[tags]

Model pipeline:
  1. AST Embedder   — converts node types and signatures to 128-d vectors
  2. Context Window — assembles embedding of surrounding 32 nodes
  3. Retrieval      — k-NN search over known Q+ stdlib signatures (k=8)
  4. Candidate Gen  — grammar-constrained beam search (k=5 candidates)
  5. Ranker         — ML classifier (logistic regression on AST features)
  6. Output filter  — rejects any candidate referencing undefined symbol

Output:
  - Up to 5 ranked completions (TAB to accept top, Shift-TAB for next)
  - Always syntactically valid Q+
  - Never references undefined functions or types
  - Includes full type annotation on generated `let` bindings
```

### Capabilities
| Feature | Description |
|---------|-------------|
| TAB autocomplete | Completes identifiers, method names, type names, field access |
| Driver scaffolding | Generates `driver` struct + methods from a description comment |
| Syscall generation | Fills in bounds checks, error return paths automatically |
| Refactoring | Renames across a module, extracts blocks to functions |
| Vulnerability detection | Flags patterns matching security pass rules before compile |
| Code optimization | Suggests replacing for-loops with slice operations |

---

## 11. IDE — Q+ Studio

The official IDE is a VSCode extension (`qplus-vscode`) that provides:

| Feature | Implementation |
|---------|---------------|
| Syntax highlighting | tmLanguage grammar (`.qp` files) |
| Error underlining | Language Server Protocol (planned) |
| Security warnings | Inline gutter markers from security.c output |
| TAB autocomplete | AI engine via local socket connection |
| BYOK models | OpenAI-compatible API endpoint in settings |
| Elite model | Token-authenticated server-side model (key never stored client-side) |
| Debugger | GDB + QEMU remote stub for kernel debugging |
| Memory view | GDB custom dashboard for registers/memory/interrupts |
| Templates | Snippets for driver, syscall, interrupt, kernel module |

---

## 12. Standard Library API Reference

### `qpstd::mem::allocator`
| API | Description |
|-----|-------------|
| `BumpAllocator::new(base, size)` | Create bump allocator over physical region |
| `BumpAllocator::alloc(size, align)` | Allocate `size` bytes with alignment |
| `BumpAllocator::reset()` | Reset cursor to base (full free) |
| `SlabAllocator::new(mem, block_size, count)` | Slab allocator for fixed-size objects |
| `SlabAllocator::alloc()` | O(1) allocation |
| `SlabAllocator::free(ptr)` | O(1) deallocation |

### `qpstd::mem::paging`
| API | Description |
|-----|-------------|
| `PageTableEntry::new(phys, flags)` | Create PTE |
| `PageTable::zero()` | Clear all entries |
| `load_pml4(phys)` | Write CR3 |
| `tlb_flush_page(va)` | INVLPG |
| `tlb_flush_all()` | Full TLB flush via CR3 reload |

### `qpstd::cpu::interrupts`
| API | Description |
|-----|-------------|
| `enable_interrupts()` / `disable_interrupts()` | STI / CLI |
| `halt()` | HLT |
| `Idt::new()` | Create 256-entry IDT |
| `Idt::set_handler(vec, fn_ptr)` | Register interrupt handler |
| `Idt::load()` | LIDT |
| `pic_remap(off1, off2)` | Remap PIC to avoid conflicts with exceptions |
| `pic_send_eoi(irq)` | Send End of Interrupt |

### `qpstd::cpu::regs`
| API | Description |
|-----|-------------|
| `rdmsr(msr)` / `wrmsr(msr, val)` | Read/write MSR |
| `read_cr0..4()` / `write_cr0/3/4(val)` | Control registers |
| `CpuContext::new_kernel(rip, stack)` | Create kernel task context |
| `switch_context(old, new)` | Task context switch |

### `qpstd::drivers::vga`
| API | Description |
|-----|-------------|
| `VGABuffer::get()` | Initialize VGA driver at 0xB8000 |
| `VGABuffer::clear(color)` | Clear screen |
| `VGABuffer::put_char(ch)` | Write character with auto-scroll |
| `VGABuffer::write(str)` | Write string |
| `VGABuffer::write_hex(u64)` | Write hex number |

### `qpstd::drivers::serial`
| API | Description |
|-----|-------------|
| `Serial::init(port)` | Init UART at 115200 baud |
| `Serial::write(str)` | Write string |
| `Serial::put_char(u8)` | Write byte |
| `Serial::read_char()` | Blocking read |

### `qpstd::kernel::scheduler`
| API | Description |
|-----|-------------|
| `Scheduler::new()` | Create round-robin scheduler |
| `Scheduler::add_task(entry, name)` | Add task, returns PID |
| `Scheduler::schedule()` | Switch to next ready task |
| `Scheduler::block_current()` | Block caller task |
| `Scheduler::unblock(pid)` | Unblock a task |
| `Scheduler::exit_current()` | Terminate current task |

### `qpstd::collections::ringbuffer`
| API | Description |
|-----|-------------|
| `RingBuffer<T,N>::new()` | Stack-allocated SPSC queue |
| `push(val)` | Enqueue, returns `false` if full |
| `pop()` | Dequeue, returns `Option<T>` |
| `peek()` | Look without dequeue |
| `clear()` | Reset |

### `qpstd::fs::vfs`
| API | Description |
|-----|-------------|
| `VfsNode::read(buf, offset, len)` | Read from file |
| `VfsNode::write(buf, offset, len)` | Write to file |
| `FdTable::alloc(node, flags)` | Open a file → fd |
| `FdTable::get(fd)` | Get FileDescriptor |
| `FdTable::close(fd)` | Close fd |

---

## 13. Full Boot Pipeline — Examples

### 13.1 Kernel Entry Point

```qplus
// kernel/main.qp
module kernel;

import qpstd::drivers::vga  { VGABuffer };
import qpstd::drivers::serial { Serial, COM1 };
import qpstd::cpu::interrupts { enable_interrupts, pic_remap, Idt };
import qpstd::mem::allocator  { BumpAllocator };

const HEAP_BASE: usize = 0x200000;
const HEAP_SIZE: usize = 0x100000;  // 1 MB

static mut HEAP: BumpAllocator = BumpAllocator::new(HEAP_BASE, HEAP_SIZE);

#[no_mangle]
#[link_section(".text.boot")]
pub fn _start() -> ! {
    let mut serial = Serial::init(COM1);
    serial.write("Q+ Kernel booting...\n");

    let mut vga = VGABuffer::get();
    vga.clear(0x00);
    vga.write("Q+ OS v0.1 booting");

    // Remap PIC: IRQ 0-7 → INT 0x20-0x27, IRQ 8-15 → INT 0x28-0x2F
    unsafe { pic_remap(0x20, 0x28); }

    // Load IDT
    let mut idt = Idt::new();
    // idt.set_handler(0x20, timer_isr as usize);
    idt.load();

    enable_interrupts();

    serial.write("Init complete.\n");

    loop {
        unsafe { cpu::halt(); }
    }
}

#[panic_handler]
fn panic(msg: &str) -> ! {
    let mut vga = VGABuffer::get();
    vga.set_color(0xCF);  // Red on white
    vga.write("KERNEL PANIC: ");
    vga.write(msg);
    loop { unsafe { asm!("hlt"); } }
}
```

### 13.2 Simple Keyboard Driver

```qplus
// kernel/drivers/keyboard.qp
module kernel::drivers::keyboard;

import qpstd::collections::ringbuffer { RingBuffer };

pub const KB_IRQ:  u8  = 1;
pub const KB_PORT: u16 = 0x60;

pub driver PS2Keyboard {
    buffer: RingBuffer<u8, 256>,

    pub fn new() -> PS2Keyboard {
        return PS2Keyboard { buffer: RingBuffer::new() };
    }

    #[irq_handler]
    interrupt fn handle_irq(self: &mut Self) -> void {
        let scancode: u8 = unsafe { port::in_u8(KB_PORT) };
        if not (scancode & 0x80 != 0) {   // key press (not release)
            self.buffer.push(scancode);
        }
        unsafe { port::out_u8(0x20, 0x20); }  // EOI
    }

    pub fn read(self: &mut Self) -> Option<u8> {
        return self.buffer.pop();
    }
}
```

### 13.3 Syscall Example

```qplus
// kernel/syscalls/write.qp
module kernel::syscalls::write;

import qpstd::fs::vfs { FdTable };

pub static mut FD_TABLE: FdTable = FdTable::new();

syscall sys_write(fd: i32, buf: ptr<u8>, len: usize) -> isize {
    // Security: validate user pointer is in userspace range
    if not is_userspace_range(buf, len) {
        return -14;  // -EFAULT
    }
    if len == 0 { return 0; }
    if len > 65536 { return -7; }  // -E2BIG

    match unsafe { FD_TABLE }.get(fd) {
        None => return -9,   // -EBADF
        Some(file) => {
            let written: isize = file.node.write(buf, file.offset, len)?;
            file.offset += written as u64;
            return written;
        },
    }
}

fn is_userspace_range(ptr: ptr<u8>, len: usize) -> bool {
    let addr: usize = ptr as usize;
    return addr >= 0x400000 and addr + len < 0x0000_8000_0000_0000;
}
```

### 13.4 Generated C Output (hello.qp → output.c)

```c
/* Generated by qpc (Q+ Compiler) — DO NOT EDIT */
/* Freestanding C11 — no libc required           */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Q+ fat-pointer slice type */
typedef struct { void *ptr; size_t len; } qp_slice;

/* Port I/O (x86) */
static inline void qp_port_out_u8(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t qp_port_in_u8(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* MMIO access helpers */
#define QP_MMIO_READ8(addr)       (*(volatile uint8_t  *)(uintptr_t)(addr))
#define QP_MMIO_WRITE32(addr,val) (*(volatile uint32_t *)(uintptr_t)(addr) = (val))

/* module: kernel */
#define PAGE_SIZE (4096)
#define KERNEL_BASE (0xFFFF800000000000ULL)

__attribute__((noreturn)) void __attribute__((section(".text.boot"))) _start(void) {
    /* ... generated body ... */
    for (;;) { __asm__ volatile ("hlt"); }
}
```

---

## 14. Complete Build Pipeline for Kali Linux

See `README.md` for the step-by-step Kali Linux commands and QEMU execution.

---

## 15. ABI & Calling Conventions

- **Kernel functions**: System V AMD64 ABI (`rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9` for first 6 args)
- **Syscall ABI** (custom, x86-64): `rax`=syscall number, `rdi..r9`=args, `rax`=return
- **Interrupt handlers**: stack frame from CPU, no extra arguments
- **Driver methods**: first argument is `Type*` (pointer to driver struct)
- **No name mangling** by default (Q+ uses `#[no_mangle]` for exported symbols)
- **Struct layout**: C-compatible by default; `#[packed]` removes padding; `#[align(N)]` forces alignment
