#!/bin/sh

# Because the CIM benchmark needs access to the entire system (user + kernelspace),
# this script was created to automatically start the CIM testbench on bootup, then 
# immediately exit. The same will then be done with a baseline run (with no CIM)
# execution, then the two results will be subtracted to get the cache effects in 
# the CIM program only.

set -eu

QEMU=/nfshome/bellee@chapman.edu/CIM/cim-mem-ctrl/tools/qemu/build/qemu-system-riscv64
PLUGIN=/nfshome/bellee@chapman.edu/CIM/cim-mem-ctrl/tools/qemu/build/contrib/plugins/libcache.so
RESULTS=/nfshome/bellee@chapman.edu/CIM/cim-mem-ctrl/cim/results/cache_tests/results

IMAGES_DIR=/tmp/buildroot_build/images
FW_JUMP=$IMAGES_DIR/fw_jump.bin
KERNEL=$IMAGES_DIR/Image
ROOTFS=$IMAGES_DIR/rootfs.ext2

# SystemC server
SYSTEMC_DIR=/nfshome/bellee@chapman.edu/CIM/cim-mem-ctrl/mem_ctrl
SYSTEMC_BIN=$SYSTEMC_DIR/listen
SYSTEMC_SOCK=/tmp/systemc.sock

# Usage:
#   ./run_cache_test.sh [init_script] [runs]
# Examples:
#   ./run_cache_test.sh run_cim.sh 3
#   ./run_cache_test.sh run_base.sh 5
INIT_SCRIPT=${1:-run_cim.sh}
RUNS=${2:-1}

BASE=$(basename "$INIT_SCRIPT")
BASE=${BASE%.sh}

mkdir -p "$RESULTS"

cleanup_systemc() {
  # best-effort cleanup
  [ -n "${SYSTEMC_PID:-}" ] && kill "$SYSTEMC_PID" 2>/dev/null || true
  wait "${SYSTEMC_PID:-}" 2>/dev/null || true
  rm -f "$SYSTEMC_SOCK" 2>/dev/null || true
}

i=1
while [ "$i" -le "$RUNS" ]; do
  LOGFILE="$RESULTS/${BASE}_${i}.log"
  rm -f "$LOGFILE"

  echo "[run_cache_test] run $i/$RUNS: init=$INIT_SCRIPT log=$LOGFILE" >&2

  # Ensure no stale socket from a previous run
  rm -f "$SYSTEMC_SOCK" 2>/dev/null || true

  # Start SystemC server
  (
    cd "$SYSTEMC_DIR"
    exec "$SYSTEMC_BIN"
  ) &
  SYSTEMC_PID=$!

  # If this script is interrupted, kill SystemC
  trap cleanup_systemc INT TERM HUP

  # Give the server a moment to bind & listen
  sleep 0.2

  # Run QEMU (returns when guest powers off)
  "$QEMU" \
    -M virt -nographic \
    -bios "$FW_JUMP" \
    -kernel "$KERNEL" \
    -append "rootwait root=/dev/vda rw init=/usr/bin/$INIT_SCRIPT" \
    -drive file="$ROOTFS",format=raw \
    -plugin file="$PLUGIN",cores=1,dcachesize=32768,dassoc=4,dblksize=64,icachesize=32768,iassoc=4,iblksize=64,evict=lru,limit=20 \
    -d plugin -D "$LOGFILE"

  # Stop SystemC server after QEMU exits
  cleanup_systemc
  trap - INT TERM HUP

  i=$((i + 1))
done