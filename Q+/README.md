# Q+ Programming Language

> A professional compiled systems programming language designed for building operating systems from scratch.

## Overview

Q+ combines **C-level hardware control** with **Rust-level memory safety** and **Zig-level simplicity**. It features first-class OS constructs (`driver`, `syscall`, `interrupt`, `mmio`), a compile-time security analyzer, and a deterministic AI engine for code assistance.

## Quick Start

### Prerequisites
- GCC (MinGW-w64 on Windows)
- Make

### Build the compiler

```bash
cd compiler
make
```

### Run the lexer on a sample file

```bash
.\build\qpc.exe lex tests\samples\hello.qp
```

### Run the test suite

```bash
make test
```

## Project Structure

```
Q+/
├── compiler/              # The qpc compiler (C)
│   ├── include/qpc/       # Header files
│   │   ├── common.h       # Types, arena allocator, utilities
│   │   ├── token.h        # Token definitions, keyword table
│   │   ├── source.h       # Source file management
│   │   ├── lexer.h        # Lexer interface
│   │   └── diagnostic.h   # Error/warning reporting
│   ├── src/               # Implementation
│   │   ├── main.c         # CLI entry point
│   │   ├── lexer.c        # Tokenizer
│   │   ├── source.c       # Source file loading
│   │   └── diagnostic.c   # Diagnostics with ANSI colors
│   ├── tests/             # Test suite
│   │   ├── test_lexer.c   # 30+ lexer tests
│   │   └── samples/       # Sample .qp files
│   └── Makefile
└── README.md
```

## Language Example

```qplus
module kernel;

import kernel::drivers::vga { VGABuffer };

#[no_mangle]
pub fn _start() -> ! {
    let vga = VGABuffer::get();
    vga.clear(0x00);
    vga.write_at(0, 0, "Q+ OS", 0x0A);

    loop {
        unsafe { asm!("hlt"); }
    }
}
```

## Status

- [x] Language specification (complete)
- [x] Lexer (complete — 100+ token types, 70+ keywords)
- [ ] Parser + AST
- [ ] Semantic analysis
- [ ] Type checker
- [ ] Ownership/borrow checker
- [ ] Security analyzer (12 passes)
- [ ] LLVM IR code generation
- [ ] Standard library
- [ ] IDE (Q+ Studio)
- [ ] AI engine

## License

Copyright (c) 2026 Q+ Project. All rights reserved.
