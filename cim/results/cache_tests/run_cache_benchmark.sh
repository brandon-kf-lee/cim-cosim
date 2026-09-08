#!/bin/sh
set -e

# run_cache_benchmark.sh
# Generates payload scripts to inject into QEMU and orchestrates all the desired
#   benchmark runs. Organizes base, overhead, setup, and total execution into separate states.

# ---------- Configurations ----------
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cache_benchmark_helper=${cache_benchmark_helper:-"$SCRIPT_DIR/cache_benchmark_helper.sh"}
DEPLOY_SCRIPT="/nfshome/bellee@chapman.edu/CIM/cim-mem-ctrl/mem_ctrl/test/deploy.sh"
CIM_DIRECTORY="/nfshome/bellee@chapman.edu/CIM/cim-mem-ctrl/cim"

# Set BENCH_DEBUG=1 to see progress prints
BENCH_DEBUG="${BENCH_DEBUG:-1}"

# ---------- Helper Functions ----------
# Print usage
usage() {
  echo "Usage: $0 [OPTIONS]" >&2
  echo "" >&2
  echo "Options:" >&2
  echo "  --network <type>   Choose 'mnist' or 'synthetic' (default: mnist)" >&2
  echo "  --impl <target>    Choose the implmentation and region to benchmark" >&2
  echo "                     Allowed values (default: all):" >&2
  echo "                     (all|base|cim_overhead|cim_setup|cim_total|cpu_overhead|cpu_setup|cpu_total)" >&2
  echo "  --warmup <N>       Number of unmeasured warmup inferences per run (default: 0)" >&2
  echo "  --iters <N>        Number of measured inference iterations per run (default: 1000)" >&2
  echo "  --runs <N>         Number of independent benchmark runs (default: 1)" >&2
  exit 2
}

# Debug printing
dbg() {
  if [ "$BENCH_DEBUG" -eq 1 ]; then
    echo "[run_cache_benchmark] $*" >&2
  fi
}

# Run one generated benchmarking script
run_one() {
  script="$1"
  dbg "running $script x $RUNS" >&2
  "$cache_benchmark_helper" "$script" "$RUNS" "$SYNTH_FLAG"
}


# ---------- Default Benchmark Values ----------
NETWORK="mnist"
IMPL="all"
WARMUP=0
ITERS=1000
RUNS=1

# ---------- Command Line Parsing ----------
while [ $# -gt 0 ]; do
  case "$1" in
    --network)
      NETWORK="$2"
      shift 2
      ;;
    --impl)
      IMPL="$2"
      shift 2
      ;;
    --warmup)
      WARMUP="$2"
      shift 2
      ;;
    --iters)
      ITERS="$2"
      shift 2
      ;;
    --runs)
      RUNS="$2"
      shift 2
      ;;
    -h|--help)
      usage
      ;;
    *)
      dbg "Error: Unknown argument \"$1\"." >&2
      usage
      ;;
  esac
done

# ---------- Network Parsing ----------
if [ "$NETWORK" = "synthetic" ]; then
    BIN_PREFIX="synthetic"
    NET_FILE="synthetic_network_q4.bin"
    SYNTH_FLAG=1
elif [ "$NETWORK" = "mnist" ]; then
    BIN_PREFIX="mnist"
    NET_FILE="mnist_network_q4.bin"
    SYNTH_FLAG=0
else
    dbg "Unknown network \""$NETWORK"\"."
    usage
fi

# ---------- Script Generation ----------
dbg "Generating guest scripts for $NETWORK..."
mkdir -p "$SCRIPT_DIR/gen_scripts"

generate_script() {
    local name=$1
    local bin=$2
    local section=$3
    local warmup=$4
    local iters=$5

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
  --warmup $warmup \\
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
generate_script "run_base.sh" "NONE" "none" 0 0
generate_script "run_cim_overhead.sh" "${BIN_PREFIX}_CIM_cache" "overhead" 0 0
generate_script "run_cim_setup.sh" "${BIN_PREFIX}_CIM_cache" "setup" 0 0
generate_script "run_cim_total.sh" "${BIN_PREFIX}_CIM_cache" "total" 0 1000
generate_script "run_cpu_overhead.sh" "${BIN_PREFIX}_CPU_cache" "overhead" 0 0
generate_script "run_cpu_setup.sh" "${BIN_PREFIX}_CPU_cache" "setup" 0 0
generate_script "run_cpu_total.sh" "${BIN_PREFIX}_CPU_cache" "total" 0 1000

# ---------- Deployment ----------
dbg "Deploying kernel module into guest rootfs..."
"$DEPLOY_SCRIPT" kmod

dbg "Deploying generated scripts into guest rootfs..."
"$DEPLOY_SCRIPT" cp "$SCRIPT_DIR"/gen_scripts/run_*.sh /usr/bin/

dbg "Deploying cache benchmark files..."
"$DEPLOY_SCRIPT" cache_bench "$CIM_DIRECTORY"

# ---------- Execution Logic ----------
case "$IMPL" in
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
  *)            usage ;;
esac