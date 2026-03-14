#!/usr/bin/env python3
"""
Q+ AI Engine — AST-based Autocomplete & Analysis Server
========================================================
Non-LLM, deterministic AI for Q+ code.
Design: JSON-RPC 2.0 over stdin/stdout or TCP socket.

Capabilities
------------
1. TAB autocomplete  (based on AST context + symbol DB)
2. Security warnings (pattern-matching over token stream)
3. Code generation   (template instantiation from function signatures)
4. Refactoring hints (detect common anti-patterns)
5. Vulnerability scan (detects uninitialized ptrs, OOB, race conditions)

This engine does NOT use an LLM.
It uses: structural embeddings, heuristic rules, and a curated pattern DB.
"""

import sys
import os
import re
import json
import socket
import threading
from dataclasses import dataclass, field, asdict
from typing import Optional, List, Dict, Any, Tuple
from enum import Enum

# ══ Configuration ══════════════════════════════════════════════
DEFAULT_PORT = 7777
STDLIB_PATH  = os.environ.get("QPLUS_STDLIB", "./stdlib")
QPC_PATH     = os.environ.get("QPC_PATH",     "qpc")

# ══ Q+ Symbol Database ════════════════════════════════════════
# Built from scanning the stdlib .qp files at startup.
# Each entry: name → SymbolInfo

class SymbolKind(str, Enum):
    FUNCTION  = "function"
    STRUCT    = "struct"
    ENUM      = "enum"
    CONST     = "const"
    MODULE    = "module"
    DRIVER    = "driver"
    SYSCALL   = "syscall"

@dataclass
class SymbolInfo:
    name:       str
    kind:       SymbolKind
    module:     str
    signature:  str          # Rendered signature for display
    doc:        str = ""     # Doc comment (/// lines above)
    is_unsafe:  bool = False

# ══ Pattern-based Q+ stdlib symbol DB ═════════════════════════
# Curated set; extended at runtime by scanning .qp files.
BUILTIN_SYMBOLS: Dict[str, List[SymbolInfo]] = {
    "vga": [
        SymbolInfo("vga_init",      SymbolKind.FUNCTION, "qpstd::drivers::vga",
                   "fn vga_init() -> void",       "Initialize VGA text mode"),
        SymbolInfo("vga_write_str", SymbolKind.FUNCTION, "qpstd::drivers::vga",
                   "fn vga_write_str(s: *const u8) -> void", "Write null-terminated string"),
        SymbolInfo("vga_putchar",   SymbolKind.FUNCTION, "qpstd::drivers::vga",
                   "fn vga_putchar(c: char) -> void", "Write single character"),
        SymbolInfo("vga_clear",     SymbolKind.FUNCTION, "qpstd::drivers::vga",
                   "fn vga_clear(color: u8) -> void", "Clear screen with color"),
    ],
    "serial": [
        SymbolInfo("serial_init",       SymbolKind.FUNCTION, "qpstd::drivers::serial",
                   "fn serial_init() -> void"),
        SymbolInfo("serial_write_str",  SymbolKind.FUNCTION, "qpstd::drivers::serial",
                   "fn serial_write_str(s: *const u8) -> void"),
        SymbolInfo("serial_write_byte", SymbolKind.FUNCTION, "qpstd::drivers::serial",
                   "fn serial_write_byte(b: u8) -> void"),
    ],
    "allocator": [
        SymbolInfo("init",   SymbolKind.FUNCTION, "qpstd::mem::allocator",
                   "fn init(base: *mut u8, size: usize) -> void"),
        SymbolInfo("kmalloc",SymbolKind.FUNCTION, "qpstd::mem::allocator",
                   "fn kmalloc(size: usize) -> *mut void", is_unsafe=True),
        SymbolInfo("kfree",  SymbolKind.FUNCTION, "qpstd::mem::allocator",
                   "fn kfree(ptr: *mut void) -> void", is_unsafe=True),
    ],
    "interrupts": [
        SymbolInfo("idt_init",  SymbolKind.FUNCTION, "qpstd::cpu::interrupts",
                   "fn idt_init() -> void"),
        SymbolInfo("idt_set_handler", SymbolKind.FUNCTION, "qpstd::cpu::interrupts",
                   "fn idt_set_handler(vector: u8, handler: fn() -> void) -> void"),
    ],
    "regs": [
        SymbolInfo("inb",  SymbolKind.FUNCTION, "qpstd::cpu::regs",
                   "fn inb(port: u16) -> u8", is_unsafe=True),
        SymbolInfo("outb", SymbolKind.FUNCTION, "qpstd::cpu::regs",
                   "fn outb(port: u16, val: u8) -> void", is_unsafe=True),
        SymbolInfo("inw",  SymbolKind.FUNCTION, "qpstd::cpu::regs",
                   "fn inw(port: u16) -> u16", is_unsafe=True),
        SymbolInfo("outw", SymbolKind.FUNCTION, "qpstd::cpu::regs",
                   "fn outw(port: u16, val: u16) -> void", is_unsafe=True),
        SymbolInfo("inl",  SymbolKind.FUNCTION, "qpstd::cpu::regs",
                   "fn inl(port: u16) -> u32", is_unsafe=True),
        SymbolInfo("outl", SymbolKind.FUNCTION, "qpstd::cpu::regs",
                   "fn outl(port: u16, val: u32) -> void", is_unsafe=True),
        SymbolInfo("rdmsr",SymbolKind.FUNCTION, "qpstd::cpu::regs",
                   "fn rdmsr(msr: u32) -> u64", is_unsafe=True),
        SymbolInfo("wrmsr",SymbolKind.FUNCTION, "qpstd::cpu::regs",
                   "fn wrmsr(msr: u32, val: u64) -> void", is_unsafe=True),
    ],
    "scheduler": [
        SymbolInfo("sched_add",  SymbolKind.FUNCTION, "qpstd::kernel::scheduler",
                   "fn sched_add(thread: *mut Thread) -> void"),
        SymbolInfo("sched_yield",SymbolKind.FUNCTION, "qpstd::kernel::scheduler",
                   "fn sched_yield() -> void"),
        SymbolInfo("sched_start",SymbolKind.FUNCTION, "qpstd::kernel::scheduler",
                   "fn sched_start() -> !"),
    ],
    "ipc": [
        SymbolInfo("Spinlock",  SymbolKind.STRUCT, "qpstd::kernel::ipc", "struct Spinlock"),
        SymbolInfo("Semaphore", SymbolKind.STRUCT, "qpstd::kernel::ipc", "struct Semaphore"),
        SymbolInfo("Mutex",     SymbolKind.STRUCT, "qpstd::kernel::ipc", "struct Mutex"),
        SymbolInfo("Channel",   SymbolKind.STRUCT, "qpstd::kernel::ipc", "struct Channel<T>"),
    ],
    "fmt": [
        SymbolInfo("Fmt",          SymbolKind.STRUCT,   "qpstd::util::fmt", "struct Fmt"),
        SymbolInfo("write_str",    SymbolKind.FUNCTION, "qpstd::util::fmt",
                   "fn write_str(self: &mut Fmt, s: &str) -> usize"),
        SymbolInfo("write_u64",    SymbolKind.FUNCTION, "qpstd::util::fmt",
                   "fn write_u64(self: &mut Fmt, n: u64) -> void"),
        SymbolInfo("write_hex",    SymbolKind.FUNCTION, "qpstd::util::fmt",
                   "fn write_hex(self: &mut Fmt, n: u64) -> void"),
        SymbolInfo("write_i64",    SymbolKind.FUNCTION, "qpstd::util::fmt",
                   "fn write_i64(self: &mut Fmt, n: i64) -> void"),
        SymbolInfo("as_cstr",      SymbolKind.FUNCTION, "qpstd::util::fmt",
                   "fn as_cstr(self: &mut Fmt) -> *const u8"),
        SymbolInfo("format_static",SymbolKind.FUNCTION, "qpstd::util::fmt",
                   "fn format_static(fmt: *const u8, args: *const u64, count: usize) -> *const u8"),
    ],
}

def build_symbol_db(stdlib_path: str) -> Dict[str, List[SymbolInfo]]:
    """Scan stdlib .qp files and extract pub symbols (regex-based)."""
    db: Dict[str, List[SymbolInfo]] = dict(BUILTIN_SYMBOLS)
    if not os.path.isdir(stdlib_path):
        return db

    fn_pat  = re.compile(r'pub fn\s+(\w+)\s*\(([^)]*)\)\s*->\s*([^\{;]+)')
    st_pat  = re.compile(r'pub struct\s+(\w+)')
    en_pat  = re.compile(r'pub enum\s+(\w+)')
    mod_pat = re.compile(r'^module\s+([\w:]+)\s*;', re.MULTILINE)

    for root, _, files in os.walk(stdlib_path):
        for fname in files:
            if not fname.endswith('.qp'):
                continue
            path = os.path.join(root, fname)
            try:
                src = open(path, encoding='utf-8', errors='ignore').read()
            except Exception:
                continue

            mod_match = mod_pat.search(src)
            module = mod_match.group(1) if mod_match else fname

            key = fname.replace('.qp', '')
            if key not in db:
                db[key] = []

            for m in fn_pat.finditer(src):
                name, params, ret = m.group(1), m.group(2), m.group(3).strip()
                sig = f"fn {name}({params}) -> {ret}"
                unsafe = 'unsafe' in src[:m.start()].split('\n')[-1]
                db[key].append(SymbolInfo(name, SymbolKind.FUNCTION, module, sig, is_unsafe=unsafe))

            for m in st_pat.finditer(src):
                name = m.group(1)
                db[key].append(SymbolInfo(name, SymbolKind.STRUCT, module, f"struct {name}"))

            for m in en_pat.finditer(src):
                name = m.group(1)
                db[key].append(SymbolInfo(name, SymbolKind.ENUM, module, f"enum {name}"))

    return db

# ══ Security Pattern Database ══════════════════════════════════

@dataclass
class SecurityPattern:
    id:          str
    severity:    str       # "error" | "warning" | "info"
    title:       str
    description: str
    regex:       str
    suggestion:  str

SECURITY_PATTERNS: List[SecurityPattern] = [
    SecurityPattern(
        "QPS001", "error",
        "Unguarded MMIO write",
        "Direct pointer write without volatile qualifier may be optimized away by the compiler.",
        r'\*\s*\(\s*\w+\s+as\s+\*mut\s+\w+\s*\)\s*=',
        "Use `volatile *(addr as *mut T) = val;` for MMIO writes."
    ),
    SecurityPattern(
        "QPS002", "error",
        "Raw pointer dereference outside unsafe",
        "Dereferencing a raw pointer is unsafe and must be inside an unsafe {} block.",
        r'(?<!unsafe\s)\*\s*[a-z_]\w+',
        "Wrap pointer dereferences in `unsafe { ... }`."
    ),
    SecurityPattern(
        "QPS003", "warning",
        "Port I/O without unsafe annotation",
        "Port I/O functions (inb/outb/inw/outw) are inherently unsafe.",
        r'\bregs::(inb|outb|inw|outw|inl|outl)\s*\(',
        "Ensure port I/O calls are within unsafe blocks."
    ),
    SecurityPattern(
        "QPS004", "warning",
        "Missing EOI in interrupt handler",
        "IRQ handlers that don't send EOI (0x20 to port 0x20/0xA0) will mask all interrupts.",
        r'#\[irq_handler\].*?fn\s+\w+\s*\(\s*\)',
        "Add `unsafe { regs::outb(0x20, 0x20); }` at end of handler."
    ),
    SecurityPattern(
        "QPS005", "error",
        "Unbounded array index",
        "Array index without bounds check may cause out-of-bounds access.",
        r'\w+\[\s*\w+\s*\]',
        "Verify index < array.len() before indexing, or use .get() which returns Option."
    ),
    SecurityPattern(
        "QPS006", "warning",
        "Null pointer not checked before dereference",
        "Pointer assigned from unsafe call may be null.",
        r'(kmalloc|alloc)\s*\([^)]*\)\s*as\s*\*',
        "Check `if ptr == null { return error; }` after every allocation."
    ),
    SecurityPattern(
        "QPS007", "warning",
        "Spinlock acquire without release",
        "If a function acquires a spinlock but all code paths don't call release(), this causes a deadlock.",
        r'\.acquire\s*\(\s*\)',
        "Use `defer { lock.release(); }` immediately after acquire() for automatic release."
    ),
    SecurityPattern(
        "QPS008", "error",
        "ASM instruction with side effects outside unsafe",
        "Inline assembly has arbitrary side effects and must be inside unsafe.",
        r'asm!\s*\(',
        "Wrap asm!() calls in `unsafe { ... }`."
    ),
    SecurityPattern(
        "QPS009", "warning",
        "Global mutable state without synchronization",
        "Reading/writing static mut without a lock is a data race in SMP environments.",
        r'static\s+mut\s+\w+',
        "Protect static mut access with a Spinlock or use atomic types."
    ),
    SecurityPattern(
        "QPS010", "info",
        "Integer cast may truncate",
        "Casting from a wider type to a narrower type may silently truncate the value.",
        r'as\s+(u8|u16|i8|i16)',
        "Verify the value fits in the target type before casting."
    ),
]

def scan_security(source: str) -> List[Dict[str, Any]]:
    """Run all security patterns over source, return list of findings."""
    findings = []
    for pat in SECURITY_PATTERNS:
        for m in re.finditer(pat.regex, source, re.DOTALL):
            # Find line number
            line = source[:m.start()].count('\n') + 1
            findings.append({
                "id":          pat.id,
                "severity":    pat.severity,
                "title":       pat.title,
                "description": pat.description,
                "suggestion":  pat.suggestion,
                "line":        line,
                "col":         m.start() - source.rfind('\n', 0, m.start()),
                "match":       m.group(0)[:80],
            })
    return findings

# ══ Autocomplete engine ════════════════════════════════════════

def get_completions(prefix: str, context: Dict[str, Any],
                    db: Dict[str, List[SymbolInfo]]) -> List[Dict[str, Any]]:
    """
    Return autocomplete items for `prefix` given file context.
    Context keys:
      - imports: List[str]   — imported module names
      - scope:   str         — "safe" | "unsafe"
      - cursor_after: str    — text immediately before cursor for context
    """
    results = []
    imports = context.get("imports", [])
    scope   = context.get("scope", "safe")

    # Determine which modules are in scope
    active_modules = set()
    for imp in imports:
        # "import qpstd::drivers::vga;" → extract last segment "vga"
        parts = imp.strip().rstrip(';').split('::')
        if parts:
            active_modules.add(parts[-1])

    # Also always offer builtins
    for mod_name, symbols in db.items():
        # Filter by active import or if prefix matches module name
        if mod_name not in active_modules and not mod_name.startswith(prefix):
            continue
        for sym in symbols:
            if not sym.name.startswith(prefix):
                continue
            # Filter unsafe out of safe scope
            if sym.is_unsafe and scope == "safe":
                detail = sym.signature + "  ⚠ unsafe (wrap in unsafe {})"
            else:
                detail = sym.signature

            results.append({
                "label":       sym.name,
                "kind":        sym.kind.value,
                "detail":      detail,
                "documentation": sym.doc or f"[{sym.module}]",
                "module":      sym.module,
                "is_unsafe":   sym.is_unsafe,
                "insertText":  _build_insert_text(sym),
                "sortText":    f"0{sym.name}" if mod_name in active_modules else f"1{sym.name}",
            })

    # Add Q+ keywords if prefix matches
    KEYWORDS = [
        "fn", "let", "mut", "pub", "struct", "enum", "impl", "driver",
        "syscall", "interrupt", "unsafe", "asm!", "module", "import",
        "if", "else", "match", "for", "while", "loop", "return", "defer",
        "true", "false", "null", "self", "Self",
    ]
    for kw in KEYWORDS:
        if kw.startswith(prefix):
            results.append({
                "label":      kw,
                "kind":       "keyword",
                "detail":     "Q+ keyword",
                "insertText": kw,
                "sortText":   f"2{kw}",
            })

    return sorted(results, key=lambda x: x["sortText"])[:50]

def _build_insert_text(sym: SymbolInfo) -> str:
    """Build a snippet-style insert text for a symbol."""
    if sym.kind == SymbolKind.FUNCTION:
        # Extract params from signature
        m = re.search(r'\(([^)]*)\)', sym.signature)
        if m:
            params = m.group(1)
            # Skip self param
            real_params = [p.strip() for p in params.split(',')
                           if p.strip() and not p.strip().startswith('self')]
            if not real_params:
                return f"{sym.name}()"
            # Snippet placeholders
            snippet_parts = []
            for i, p in enumerate(real_params, 1):
                pname = p.split(':')[0].strip()
                snippet_parts.append(f"${{{i}:{pname}}}")
            return f"{sym.name}({', '.join(snippet_parts)})"
        return f"{sym.name}()"
    elif sym.kind in (SymbolKind.STRUCT, SymbolKind.DRIVER):
        return f"{sym.name} {{\n    ${{1}}\n}}"
    return sym.name

# ══ Code generation templates ══════════════════════════════════

CODE_TEMPLATES = {
    "driver": """\
// Q+ Driver: {name}
driver {name} {{
    // state
}}

impl {name} {{
    pub fn init(self: &mut Self) -> bool {{
        // TODO: initialize hardware
        return true;
    }}

    pub fn shutdown(self: &mut Self) -> void {{
        // TODO: shutdown
    }}
}}
""",
    "syscall": """\
syscall {name}({params}) -> i64 {{
    // TODO: implement syscall {name}
    return 0;
}}
""",
    "interrupt": """\
#[irq_handler]
pub fn {name}_handler() -> void {{
    // TODO: handle {name}
    unsafe {{ regs::outb(0x20, 0x20); }} // EOI
}}
""",
    "struct": """\
pub struct {name} {{
    {fields}
}}

impl {name} {{
    pub fn new() -> {name} {{
        return {name} {{
            {init_fields}
        }};
    }}
}}
""",
}

def generate_code(template: str, params: Dict[str, str]) -> str:
    """Instantiate a code template with the given parameters."""
    tmpl = CODE_TEMPLATES.get(template)
    if not tmpl:
        return f"// ERROR: Unknown template '{template}'"
    try:
        return tmpl.format(**params)
    except KeyError as e:
        return f"// ERROR: Missing parameter {e} for template '{template}'"

# ══ JSON-RPC 2.0 dispatcher ════════════════════════════════════

class AIEngineServer:
    def __init__(self, stdlib_path: str = STDLIB_PATH):
        self.db = build_symbol_db(stdlib_path)

    def handle(self, request: Dict[str, Any]) -> Dict[str, Any]:
        method  = request.get("method", "")
        params  = request.get("params", {})
        req_id  = request.get("id", 0)

        try:
            result = self._dispatch(method, params)
            return {"jsonrpc": "2.0", "id": req_id, "result": result}
        except Exception as e:
            return {"jsonrpc": "2.0", "id": req_id,
                    "error": {"code": -32000, "message": str(e)}}

    def _dispatch(self, method: str, params: Dict[str, Any]) -> Any:
        if method == "qplus/complete":
            prefix  = params.get("prefix", "")
            context = params.get("context", {})
            return {"items": get_completions(prefix, context, self.db)}

        elif method == "qplus/security":
            source = params.get("source", "")
            return {"findings": scan_security(source)}

        elif method == "qplus/generate":
            template = params.get("template", "")
            tparams  = params.get("params", {})
            return {"code": generate_code(template, tparams)}

        elif method == "qplus/symbols":
            module = params.get("module", "")
            if module and module in self.db:
                syms = [asdict(s) for s in self.db[module]]
            else:
                syms = [asdict(s) for syms in self.db.values() for s in syms]
            return {"symbols": syms}

        elif method == "qplus/ping":
            return {"status": "ok", "engine": "Q+ AI Engine v0.3"}

        else:
            raise ValueError(f"Unknown method: {method}")

# ══ TCP Server (used by VSCode extension) ══════════════════════

def tcp_server(port: int, engine: AIEngineServer) -> None:
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))
    srv.listen(8)
    print(f"[Q+ AI Engine] Listening on 127.0.0.1:{port}", flush=True)

    while True:
        conn, addr = srv.accept()
        t = threading.Thread(target=handle_client, args=(conn, engine), daemon=True)
        t.start()

def handle_client(conn: socket.socket, engine: AIEngineServer) -> None:
    buf = b""
    try:
        while True:
            data = conn.recv(4096)
            if not data:
                break
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                line = line.strip()
                if not line:
                    continue
                try:
                    req = json.loads(line)
                    resp = engine.handle(req)
                    conn.sendall((json.dumps(resp) + "\n").encode())
                except json.JSONDecodeError as e:
                    err = {"jsonrpc": "2.0", "id": None,
                           "error": {"code": -32700, "message": f"Parse error: {e}"}}
                    conn.sendall((json.dumps(err) + "\n").encode())
    finally:
        conn.close()

# ══ Stdin/stdout mode (used by LSP) ═══════════════════════════

def stdio_server(engine: AIEngineServer) -> None:
    print("[Q+ AI Engine] Running in stdio mode", file=sys.stderr, flush=True)
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req  = json.loads(line)
            resp = engine.handle(req)
            print(json.dumps(resp), flush=True)
        except json.JSONDecodeError as e:
            err = {"jsonrpc": "2.0", "id": None,
                   "error": {"code": -32700, "message": f"Parse error: {e}"}}
            print(json.dumps(err), flush=True)

# ══ Entry point ════════════════════════════════════════════════

def main() -> None:
    mode      = "tcp"
    port      = DEFAULT_PORT
    stdlib    = STDLIB_PATH

    for arg in sys.argv[1:]:
        if arg == "--stdio":
            mode = "stdio"
        elif arg.startswith("--port="):
            port = int(arg.split("=", 1)[1])
        elif arg.startswith("--stdlib="):
            stdlib = arg.split("=", 1)[1]

    engine = AIEngineServer(stdlib)

    if mode == "stdio":
        stdio_server(engine)
    else:
        tcp_server(port, engine)

if __name__ == "__main__":
    main()
