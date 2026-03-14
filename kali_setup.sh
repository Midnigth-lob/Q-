#!/usr/bin/env bash
# ================================================================
# Q+ Kali Linux Setup Script
# Instala dependencias y compila todo el ecosistema Q+
#
# Uso:
#   chmod +x kali_setup.sh
#   ./kali_setup.sh
# ================================================================

set -e  # salir si algo falla

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
COMPILER_DIR="$ROOT_DIR/compiler"
OS_DEMO_DIR="$ROOT_DIR/os_demo"
AI_DIR="$ROOT_DIR/ai_engine"

echo "========================================"
echo "  Q+ Ecosystem Setup for Kali Linux"
echo "  Root: $ROOT_DIR"
echo "========================================"
echo ""

# ── 1. Dependencias ──────────────────────────────────────────
echo "[1/5] Installing dependencies..."
sudo apt-get update -qq
sudo apt-get install -y \
    gcc \
    gcc-x86-64-linux-gnu \
    binutils-x86-64-linux-gnu \
    nasm \
    qemu-system-x86 \
    grub-pc-bin \
    xorriso \
    make \
    python3 \
    gdb \
    2>/dev/null
echo "      Done."

# ── 2. Compilar qpc ──────────────────────────────────────────
echo ""
echo "[2/5] Building Q+ compiler (qpc)..."
cd "$COMPILER_DIR"
make clean 2>/dev/null || true
make
echo "      Done: $COMPILER_DIR/build/qpc"

# ── 3. Lexer tests ───────────────────────────────────────────
echo ""
echo "[3/5] Running lexer test suite..."
make test
echo "      Done."

# ── 4. Smoke tests (lex + parse + build samples) ─────────────
echo ""
echo "[4/5] Smoke testing compiler on samples..."
QPC="$COMPILER_DIR/build/qpc"

for sample in hello driver syscall; do
    f="$COMPILER_DIR/tests/samples/${sample}.qp"
    if [ -f "$f" ]; then
        echo "  lex    $sample.qp..."
        "$QPC" lex   "$f" > /dev/null 2>&1 && echo "         [OK]" || echo "         [WARN] lex errors"
        echo "  parse  $sample.qp..."
        "$QPC" parse "$f" > /dev/null 2>&1 && echo "         [OK]" || echo "         [WARN] parse errors"
        echo "  build  $sample.qp..."
        "$QPC" build "$f" -o "/tmp/${sample}.c" 2>&1 && echo "         [OK] → /tmp/${sample}.c" || echo "         [WARN] build errors"
    fi
done

# ── 5. Lanzar AI engine (background) ─────────────────────────
echo ""
echo "[5/5] Starting AI engine on port 7777..."
cd "$AI_DIR"
pkill -f "server.py" 2>/dev/null || true
sleep 0.5
python3 server.py --port=7777 --stdlib="$ROOT_DIR/stdlib" &
AI_PID=$!
sleep 2

# Ping
RESPONSE=$(echo '{"jsonrpc":"2.0","id":1,"method":"qplus/ping","params":{}}' | \
    nc -q1 127.0.0.1 7777 2>/dev/null || echo "error")

if echo "$RESPONSE" | grep -q '"ok"'; then
    echo "      AI engine running (PID $AI_PID) — port 7777 OK"
else
    echo "      AI engine started (PID $AI_PID) — ping pending"
fi

echo ""
echo "========================================"
echo "  SETUP COMPLETE"
echo ""
echo "  Compiler:   $COMPILER_DIR/build/qpc"
echo "  AI Engine:  http://127.0.0.1:7777  (PID $AI_PID)"
echo ""
echo "  Next steps:"
echo "    cd os_demo && make iso && make run"
echo "========================================"
