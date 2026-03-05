#!/usr/bin/env bash
# build_sheepshaver.sh — One-shot script: builds GTK4 deps then SheepShaver.
#
# Run from any directory:
#   bash SheepShaver/src/build_sheepshaver.sh
#
# Or make it executable and run directly:
#   ./SheepShaver/src/build_sheepshaver.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEPS_DIR="$SCRIPT_DIR/deps"
UNIX_DIR="$SCRIPT_DIR/Unix"
DEPS_INSTALL="$DEPS_DIR/install"
DEPS_STAMP="$DEPS_INSTALL/lib/pkgconfig/gtk4.pc"

# ── Step 1: Build GTK4 and dependencies ────────────────────────────────────
if [ ! -f "$DEPS_STAMP" ]; then
    echo ""
    echo "=========================================================="
    echo " Step 1/3: Building GTK4 4.18.3 + dependencies"
    echo "=========================================================="
    cd "$DEPS_DIR"
    bash build_deps.sh
else
    echo ""
    echo " Step 1/3: GTK4 already built — skipping"
fi

# ── Step 2: Configure SheepShaver ─────────────────────────────────────────
echo ""
echo "=========================================================="
echo " Step 2/3: Configuring SheepShaver"
echo "=========================================================="
cd "$UNIX_DIR"
export PKG_CONFIG_PATH="$DEPS_INSTALL/lib/pkgconfig:$DEPS_INSTALL/lib/x86_64-linux-gnu/pkgconfig:${PKG_CONFIG_PATH:-}"
export LDFLAGS="${LDFLAGS:-} -Wl,-rpath,$DEPS_INSTALL/lib"
./configure "$@"

# ── Step 3: Build SheepShaver ─────────────────────────────────────────────
echo ""
echo "=========================================================="
echo " Step 3/3: Building SheepShaver"
echo "=========================================================="
make -j"$(nproc 2>/dev/null || echo 4)"

echo ""
echo "=========================================================="
echo " Build complete."
echo " Binaries are in: $UNIX_DIR"
echo "=========================================================="
