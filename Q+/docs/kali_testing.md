# Q+ on Kali Linux — Complete Testing Guide

## Prerequisites: Install tools

```bash
# Update and install cross-compiler toolchain + QEMU
sudo apt update && sudo apt upgrade -y
sudo apt install -y \
    gcc-x86-64-linux-gnu \
    binutils-x86-64-linux-gnu \
    nasm \
    qemu-system-x86 \
    grub-pc-bin \
    xorriso \
    make \
    python3 \
    python3-pip \
    git \
    gdb \
    cgdb          # optional: better GDB front-end

# Verify
x86_64-linux-gnu-gcc --version
nasm --version
qemu-system-x86_64 --version
```

---

## Step 1: Build the Q+ Compiler (qpc)

```bash
cd ~/Desktop/Q+/compiler

# Build (debug mode)
make

# Verify
./build/qpc.exe --help
# Expected: prints usage

# Run lexer test suite
make test
# Expected: === Results: 29/29 passed ===
```

### Quick smoke tests

```bash
# Lex a file
./build/qpc.exe lex tests/samples/hello.qp

# Parse
./build/qpc.exe parse tests/samples/hello.qp

# Compile hello.qp → C
./build/qpc.exe build tests/samples/hello.qp
# → produces hello.c in current dir (or stdout)

# Compile driver sample
./build/qpc.exe build tests/samples/driver.qp

# Compile syscall sample
./build/qpc.exe build tests/samples/syscall.qp
```

---

## Step 2: Build the full OS Kernel

```bash
cd ~/Desktop/Q+/os_demo

# Full pipeline: Q+ → C → .o → ELF → ISO
make

# Expected output:
#   [QPC]  ../stdlib/mem/allocator.qp → build/qp_c/allocator.c
#   [CC]   build/qp_c/allocator.c   → build/obj/allocator.o
#   ...
#   [LD]   Linking kernel ELF...
#   ============================================
#     Q+ Kernel built: build/qplus_kernel.bin
#   ============================================
```

### Create bootable ISO

```bash
make iso
# → build/qplus_os.iso
```

---

## Step 3: Run in QEMU (no GUI)

```bash
cd ~/Desktop/Q+/os_demo
make run

# Expected serial output:
#   Q+ OS Demo  ___  _     _
#   ...
#   [boot] Serial OK
#   [boot] Allocator OK
#   [boot] IDT OK
#   [boot] Timer OK
#   [boot] Syscalls OK
#   [boot] Interrupts enabled
#   [boot] Init process created
#   [boot] Starting scheduler...
#   [init] Init task started.
#   [init] Q+ kernel running. Type something:

# To exit QEMU: Ctrl+A then X
```

### Run with VGA display (GUI window)

```bash
qemu-system-x86_64 \
    -cdrom build/qplus_os.iso \
    -m 128M \
    -serial stdio \
    -vga std

# Q+ boot banner appears in VGA window
# Serial debug output appears in terminal
```

---

## Step 4: Debug the kernel with GDB

### Terminal 1: Start QEMU in debug mode

```bash
cd ~/Desktop/Q+/os_demo
make debug
# QEMU pauses, waiting for GDB on port 1234
```

### Terminal 2: Attach GDB

```bash
x86_64-linux-gnu-gdb build/qplus_kernel.elf

# Inside GDB:
(gdb) target remote :1234
(gdb) set architecture i386:x86-64
(gdb) symbol-file build/qplus_kernel.elf
(gdb) break kernel_main
(gdb) continue

# Set breakpoints on Q+ functions (compiled to C):
(gdb) break vga_init
(gdb) break sys_write
(gdb) break syscall_dispatch

# Inspect memory:
(gdb) x/32xb 0xb8000          # VGA buffer
(gdb) x/32xg 0xFFFF800000200000 # kernel .text

# Inspect registers:
(gdb) info registers
(gdb) print $rsp
(gdb) print $cr3              # page table base
```

### cgdb (better UI):

```bash
cgdb -ex "target remote :1234" build/qplus_kernel.elf
```

---

## Step 5: Test the AI Engine

```bash
cd ~/Desktop/Q+/ai_engine

# Start the engine
python3 server.py --port=7777 --stdlib=../stdlib

# In another terminal, send test requests:

# Ping
echo '{"jsonrpc":"2.0","id":1,"method":"qplus/ping","params":{}}' | \
    nc 127.0.0.1 7777
# → {"jsonrpc":"2.0","id":1,"result":{"status":"ok","engine":"Q+ AI Engine v0.3"}}

# Autocomplete "vga" 
echo '{"jsonrpc":"2.0","id":2,"method":"qplus/complete","params":{"prefix":"vga","context":{"imports":["qpstd::drivers::vga"],"scope":"safe"}}}' | \
    nc 127.0.0.1 7777

# Security scan
echo '{"jsonrpc":"2.0","id":3,"method":"qplus/security","params":{"source":"static mut X: u32 = 0;\nfn foo() { X += 1; }"}}' | \
    nc 127.0.0.1 7777

# Code generation: driver template
echo '{"jsonrpc":"2.0","id":4,"method":"qplus/generate","params":{"template":"driver","params":{"name":"MyUART"}}}' | \
    nc 127.0.0.1 7777
```

---

## Step 6: Install the VSCode Extension

```bash
cd ~/Desktop/Q+/qplus-vscode

# Install npm dependencies
npm install

# Compile TypeScript
npm run compile

# Package as .vsix
npx vsce package
# → qplus-0.3.0.vsix

# Install in VS Code / VS Codium
code --install-extension qplus-0.3.0.vsix
```

### Configure the extension

Open VS Code Settings (Ctrl+,) and search for "Q+":

| Setting | Value |
|---------|-------|
| `qplus.compilerPath` | `/path/to/Q+/compiler/build/qpc` |
| `qplus.stdlibPath`   | `/path/to/Q+/stdlib` |
| `qplus.aiEngine.enabled` | `true` |
| `qplus.aiEngine.serverPort` | `7777` |

The AI engine starts automatically when you open a `.qp` file.

---

## Step 7: Security analysis test

```bash
# Create a test file with known vulnerabilities:
cat > /tmp/vuln_test.qp << 'EOF'
module test;
static mut GLOBAL: u32 = 0;
fn read_mmio(addr: u64) -> u32 {
    return *(addr as *const u32);  // QPS002: raw deref outside unsafe
}
fn write_mmio(addr: u64, val: u32) -> void {
    *(addr as *mut u32) = val;     // QPS001: non-volatile MMIO write
}
fn bad_loop() -> void {
    loop { GLOBAL += 1; }         // QPS009: unprotected static mut
}
EOF

# Scan it
echo "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"qplus/security\",\"params\":{\"source\":\"$(cat /tmp/vuln_test.qp | sed 's/"/\\"/g' | tr '\n' ' ')\"}}" | \
    nc 127.0.0.1 7777

# Expected: findings for QPS001, QPS002, QPS009
```

---

## Step 8: Run with VirtualBox instead of QEMU

```bash
# Convert ISO to VMDK (optional)
VBoxManage convertfromraw build/qplus_kernel.bin qplus.vmdk --format VMDK

# Or just mount the ISO:
# In VirtualBox: New → Linux 64-bit → Mount build/qplus_os.iso as CD-ROM → Start
```

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| `qpc` not found | `export PATH=$PATH:~/Desktop/Q+/compiler/build` |
| `grub-mkrescue` fails | `sudo apt install grub-pc-bin xorriso` |
| QEMU black screen | Check serial output; VGA needs `multiboot2` header in ELF |
| AI engine port in use | Change port in config: `--port=7778` |
| GDB can't find symbols | `symbol-file build/qplus_kernel.elf` in GDB |
| `nasm` errors in boot.S | Ensure `nasm -f elf64`; 64-bit output format required |
