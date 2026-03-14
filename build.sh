#!/usr/bin/env bash
# ================================================================
# Q+ OS Kernel Build Script (Linux/Kali)
# Compila el kernel completo y lo bootea en QEMU
#
# Uso:
#   chmod +x build.sh
#   ./build.sh          → compila + crea ISO
#   ./build.sh run      → compila + ISO + QEMU
#   ./build.sh debug    → compila + ISO + QEMU+GDB(:1234)
#   ./build.sh clean    → limpia build/
# ================================================================

set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR/os_demo"

QPC="$ROOT_DIR/compiler/build/qpc"

# Verificar que qpc existe
if [ ! -f "$QPC" ]; then
    echo "[ERROR] qpc not found at $QPC"
    echo "        Run: cd compiler && make"
    exit 1
fi

ACTION="${1:-build}"

case "$ACTION" in
    clean)
        make clean
        ;;
    run)
        make iso
        make run
        ;;
    debug)
        make iso
        echo ""
        echo "Starting QEMU in debug mode (port 1234)..."
        echo "In another terminal run:"
        echo "  x86_64-linux-gnu-gdb build/qplus_kernel.elf -ex 'target remote :1234'"
        echo ""
        make debug
        ;;
    build|*)
        make
        make iso
        echo ""
        echo "ISO ready at: os_demo/build/qplus_os.iso"
        echo "To run: ./build.sh run"
        ;;
esac
