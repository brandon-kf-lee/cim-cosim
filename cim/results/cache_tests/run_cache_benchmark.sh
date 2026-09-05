#!/bin/sh
set -eu

# run_cache_profiling.sh
# Generates payload scripts to inject into QEMU and orchestrates all the desired
#   benchmark runs. Organizes base, overhead, setup, and total execution into separate states.
#
# Usage:
#   ./run_cache_profiling.sh [mnist|synthetic] [all|base|[cim/cpu]_overhead|[cim/cpu]_setup|[cim/cpu]_total] [runs]

#
# Examples:
#   ./run_cache_profiling.sh mnist all 3
#   ./run_cache_profiling.sh synthetic cim_total 5

# --- Configurations ---
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cache_benchmark_helper=${cache_benchmark_helper:-"$SCRIPT_DIR/cache_benchmark_helper.sh"}
DEPLOY_SCRIPT="/nfshome/bellee@chapman.edu/CIM/cim-mem-ctrl/mem_ctrl/test/deploy.sh"
CIM_DIRECTORY="/nfshome/bellee@chapman.edu/CIM/cim-mem-ctrl/cim"

NETWORK=${1:-mnist}
WHICH=${2:-all}
RUNS=${3:-1}

# --- Network Parsing ---
if [ "$NETWORK" = "synthetic" ]; then
    BIN_PREFIX="synthetic"
    NET_FILE="synthetic_network_q4.bin"
    SYNTH_FLAG=1
elif [ "$NETWORK" = "mnist" ]; then
    BIN_PREFIX="mnist"
    NET_FILE="mnist_network_q4.bin"
    SYNTH_FLAG=0
else
    echo "Error: Network must be 'mnist' or 'synthetic'." >&2
    exit 1
fi

# --- Script Generation ---
echo "[run_cache_profiling] Generating guest scripts for $NETWORK..."
mkdir -p "$SCRIPT_DIR/gen_scripts"

generate_script() {
    local name=$1
    local bin=$2
    local section=$3
    local iters=$4

    cat <<EOF > "$SCRIPT_DIR/gen_scripts/$name"
#!/bin/sh
# Automated script to load kernel modules, run benchmark, then poweroff
set -eu

# Minimal mounts needed when using init=...
mount -t proc proc /proc 2>/dev/null || true
mount -t sysfs sys /sys 2>/dev/null || true
mount -t devtmpfs dev /dev 2>/dev/null || true

# Load IRQ module (best-effort)
insmod /lib/modules/*/extra/sc_dev_irq.ko 2>/dev/null || true

cd /usr/bin/cim
EOF

    # If it's not the base OS run, append the benchmark command
    if [ "$bin" != "NONE" ]; then
        cat <<EOF >> "$SCRIPT_DIR/gen_scripts/$name"

./build/$bin \\
  --section $section \\
  --warmup 0 \\
  --iters $iters \\
  --network binaries/$NET_FILE \\
  || true
EOF
    fi

    # Append poweroff sequence
    cat <<EOF >> "$SCRIPT_DIR/gen_scripts/$name"

sync
poweroff -f
EOF
    chmod +x "$SCRIPT_DIR/gen_scripts/$name"
}

# Generate the 7 target scripts
generate_script "run_base.sh" "NONE" "none" 0
generate_script "run_cim_overhead.sh" "${BIN_PREFIX}_CIM" "overhead" 0
generate_script "run_cim_setup.sh" "${BIN_PREFIX}_CIM" "setup" 0
generate_script "run_cim_total.sh" "${BIN_PREFIX}_CIM" "total" 1000
generate_script "run_cpu_overhead.sh" "${BIN_PREFIX}_CPU" "overhead" 0
generate_script "run_cpu_setup.sh" "${BIN_PREFIX}_CPU" "setup" 0
generate_script "run_cpu_total.sh" "${BIN_PREFIX}_CPU" "total" 1000

# --- Deployment ---
echo "[run_cache_profiling] Deploying kernel module into guest rootfs..."
"$DEPLOY_SCRIPT" kmod

echo "[run_cache_profiling] Deploying generated scripts into guest rootfs..."
"$DEPLOY_SCRIPT" cp "$SCRIPT_DIR"/gen_scripts/run_*.sh /usr/bin/

echo "[run_cache_profiling] Deploying cache testbench files..."
"$DEPLOY_SCRIPT" cache_prof "$CIM_DIRECTORY"

# --- Execution Logic ---
run_one() {
  script="$1"
  echo "[run_cache_profiling] running $script x $RUNS" >&2
  "$cache_benchmark_helper" "$script" "$RUNS" "$SYNTH_FLAG"
}

case "$WHICH" in
  all)
    run_one run_base.sh
    run_one run_cim_overhead.sh
    run_one run_cim_setup.sh
    run_one run_cim_total.sh
    run_one run_cpu_overhead.sh
    run_one run_cpu_setup.sh
    run_one run_cpu_total.sh
    ;;
  base)         run_one run_base.sh ;;
  cim_overhead) run_one run_cim_overhead.sh ;;
  cim_setup)    run_one run_cim_setup.sh ;;
  cim_total)    run_one run_cim_total.sh ;;
  cpu_overhead) run_one run_cpu_overhead.sh ;;
  cpu_setup)    run_one run_cpu_setup.sh ;;
  cpu_total)    run_one run_cpu_total.sh ;;
  *)
    echo "Usage: $0 {mnist|synthetic} {all|base|cim_overhead|cim_setup|cim_total|cpu_overhead|cpu_setup|cpu_total} [runs]" >&2
    exit 2
    ;;
esac