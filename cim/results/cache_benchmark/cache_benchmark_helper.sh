#!/bin/sh

# cache_benchmark_helper.sh
# Boots QEMU directly into one payload specified by run_cache_benchmark.sh, 
#   attach QEMU cache plugin and SystemC CIM device. 


# Because the CIM benchmark needs access to the entire system (user + kernelspace),
# this script was created to automatically start the testbench on bootup, then 
# immediately exit. The same will then be done with a baseline run (with no 
# computation), then the two results will be subtracted to get the cache effects in 
# the desired program only.

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

# Passed in from run_cache_benchmark.sh
INIT_SCRIPT=${1:-run_cim.sh}
RUNS=${2:-1}
SYNTH_FLAG=${3:-0} # 0 for MNIST, 1 for Synthetic

BASE=$(basename "$INIT_SCRIPT")
BASE=${BASE%.sh}

# Output logfile directory
if [ "$SYNTH_FLAG" -eq 1 ]; then
  RESULTS_DIR="$RESULTS/synthetic"
else
  RESULTS_DIR="$RESULTS/mnist"
fi
mkdir -p "$RESULTS_DIR"

# --- SystemC Compilation Phase ---
echo "[cache_benchmark_helper] Recompiling SystemC server (SYNTHETIC=$SYNTH_FLAG)..."
(
  cd "$SYSTEMC_DIR"
  make clean >/dev/null
  make SYNTHETIC="$SYNTH_FLAG" >/dev/null
)

cleanup_systemc() {
  # best-effort cleanup
  [ -n "${SYSTEMC_PID:-}" ] && kill "$SYSTEMC_PID" 2>/dev/null || true
  wait "${SYSTEMC_PID:-}" 2>/dev/null || true
  rm -f "$SYSTEMC_SOCK" 2>/dev/null || true
}

i=1
while [ "$i" -le "$RUNS" ]; do
  LOGFILE="$RESULTS_DIR/${BASE}_${i}.log"
  rm -f "$LOGFILE"

  echo "[cache_benchmark_helper] run $i/$RUNS: init=$INIT_SCRIPT log=$LOGFILE" >&2

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
    -d plugin -D "$LOGFILE" \
    -icount shift=0

  # Stop SystemC server after QEMU exits
  cleanup_systemc
  trap - INT TERM HUP

  i=$((i + 1))
done