#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "Initializing submodules"
cd "$PROJECT_ROOT"
git submodule init
git submodule update --recursive

echo " Cloning SystemC (3.0.2) "
if [ ! -d "$SCRIPT_DIR/systemc" ]; then
    git clone --branch 3.0.2 --depth 1 \
        https://github.com/accellera-official/systemc.git \
        "$SCRIPT_DIR/systemc"
else
    echo "SystemC already exists, skipping"
fi

echo "Cloning Buildroot (2025.11.1)"
if [ ! -d "$SCRIPT_DIR/buildroot" ]; then
    git clone --branch 2025.11.1 --depth 1 \
        https://github.com/buildroot/buildroot.git \
        "$SCRIPT_DIR/buildroot"
else
    echo "Buildroot already exists, skipping"
fi

echo ""
echo "Done"
echo "See tools/README.md for build instructions"