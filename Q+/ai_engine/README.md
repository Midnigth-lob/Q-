# Q+ AI Engine — README

## What is the Q+ AI Engine?

A **deterministic, non-LLM AI assistant** specialized for the Q+ programming language. It does NOT use a chat model or large language model. Instead it uses:

- **Structural AST pattern matching** — understands Q+ syntax directly
- **Curated stdlib symbol database** — knows every function signature in the standard library
- **Regex-based security pattern scanner** — detects 10 vulnerability classes
- **Code template instantiation** — generates correct Q+ code from parameterized templates

It is precise, fast, and **never invents functions that don't exist**.

---

## Architecture

```
VSCode Extension                       AI Engine Server (Python)
┌─────────────────────┐               ┌─────────────────────────┐
│  extension.ts       │               │  server.py              │
│  ┌──────────────┐   │  JSON-RPC 2.0 │  ┌───────────────────┐  │
│  │ Completion   │───┼──────────────►│  │ Symbol DB         │  │
│  │ Provider     │   │   TCP :7777   │  │ (stdlib scan)     │  │
│  └──────────────┘   │               │  └───────────────────┘  │
│  ┌──────────────┐   │               │  ┌───────────────────┐  │
│  │ Hover        │───┼──────────────►│  │ Security Scanner  │  │
│  │ Provider     │   │               │  │ (10 patterns)     │  │
│  └──────────────┘   │               │  └───────────────────┘  │
│  ┌──────────────┐   │               │  ┌───────────────────┐  │
│  │ Security     │───┼──────────────►│  │ Code Generator    │  │
│  │ Diag Panel   │   │               │  │ (templates)       │  │
│  └──────────────┘   │               │  └───────────────────┘  │
└─────────────────────┘               └─────────────────────────┘
```

---

## JSON-RPC API

### `qplus/ping`
Check engine health.
```json
{"jsonrpc":"2.0","id":1,"method":"qplus/ping","params":{}}
→ {"result":{"status":"ok","engine":"Q+ AI Engine v0.3"}}
```

### `qplus/complete`
Get autocomplete items for a prefix in a given context.
```json
{
  "method": "qplus/complete",
  "params": {
    "prefix": "vga",
    "context": {
      "imports": ["qpstd::drivers::vga"],
      "scope": "safe"
    }
  }
}
→ {"result":{"items":[
    {"label":"vga_init","kind":"function","detail":"fn vga_init() -> void",...},
    ...
]}}
```

### `qplus/security`
Scan source code for security issues.
```json
{"method":"qplus/security","params":{"source":"<Q+ source code>"}}
→ {"result":{"findings":[
    {"id":"QPS009","severity":"warning","title":"Global mutable state...","line":3,...}
]}}
```

### `qplus/generate`
Generate Q+ code from a template.
```json
{"method":"qplus/generate","params":{"template":"driver","params":{"name":"UART"}}}
→ {"result":{"code":"driver UART {\n    // state\n}\n..."}}
```

Supported templates: `driver`, `syscall`, `interrupt`, `struct`

### `qplus/symbols`
List all known symbols (optionally filtered by module).
```json
{"method":"qplus/symbols","params":{"module":"vga"}}
→ {"result":{"symbols":[...]}}
```

---

## Security Patterns Detected

| ID | Severity | Title |
|----|----------|-------|
| QPS001 | Error   | Unguarded MMIO write (non-volatile) |
| QPS002 | Error   | Raw pointer dereference outside unsafe |
| QPS003 | Warning | Port I/O outside unsafe |
| QPS004 | Warning | Missing EOI in interrupt handler |
| QPS005 | Error   | Unbounded array index |
| QPS006 | Warning | Null pointer not checked after allocation |
| QPS007 | Warning | Spinlock acquire without defer-release |
| QPS008 | Error   | asm!() outside unsafe |
| QPS009 | Warning | Global static mut without synchronization |
| QPS010 | Info    | Integer cast may truncate |

---

## Running the Engine

```bash
# TCP mode (used by VSCode extension)
python3 server.py --port=7777 --stdlib=../stdlib

# stdio mode (used by an LSP client directly)
python3 server.py --stdio --stdlib=../stdlib
```

---

## Extending the Symbol Database

The engine scans all `.qp` files in the stdlib directory at startup and extracts `pub fn`, `pub struct`, and `pub enum` declarations automatically. To add new patterns:

1. Edit `BUILTIN_SYMBOLS` in `server.py` for curated entries
2. Edit `SECURITY_PATTERNS` to add new vulnerability patterns
3. Add new templates to `CODE_TEMPLATES`

No retrain or model update needed — changes take effect immediately on restart.

---

## About the IntelliSense / TAB Autocomplete

**Q: Does the AI engine use VS Code's built-in IntelliSense?**

No. The Q+ AI engine is a **separate process** (`server.py`) that communicates with the VSCode extension via TCP JSON-RPC. The extension registers a `vscode.CompletionItemProvider` that calls the AI engine instead of relying on VS Code's generic IntelliSense or TypeScript language server.

When you press **TAB** in a `.qp` file:
1. VS Code calls `QPlusCompletionProvider.provideCompletionItems()`
2. The provider sends `qplus/complete` to the AI engine on port 7777
3. The engine returns ranked suggestions based on imports, scope, and prefix
4. VS Code displays them in the standard completion popup
5. TAB selects the top suggestion as a snippet (with placeholders)

This means the completions are **Q+-specific, context-aware, and never invent symbols** — unlike generic IntelliSense which can hallucinate.
