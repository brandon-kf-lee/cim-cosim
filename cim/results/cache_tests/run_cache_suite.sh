#!/bin/sh
set -eu

# This script wraps run_cache_test.sh to run:
#  - all: base + cim_overhead + cim_setup + cim_total + cpu_overhead + cpu_setup + cpu_total
#  - or a single benchmark name
#
# Usage:
#   ./run_cache_suite.sh all 3
#   ./run_cache_suite.sh cim_total 5
#
# Logs will be created by run_cache_test.sh as:
#   results/cache_tests/<script>_<i>.log

# Ensure all files are loaded into the guest already:
# ./deploy.sh \
#  cp \
#  /nfshome/bellee@chapman.edu/CIM/cim-mem-ctrl/cim/results/cache_tests/run_base.sh \
#  /nfshome/bellee@chapman.edu/CIM/cim-mem-ctrl/cim/results/cache_tests/run_cim_overhead.sh \
#  /nfshome/bellee@chapman.edu/CIM/cim-mem-ctrl/cim/results/cache_tests/run_cim_setup.sh \
#  /nfshome/bellee@chapman.edu/CIM/cim-mem-ctrl/cim/results/cache_tests/run_cim_total.sh \
#  /nfshome/bellee@chapman.edu/CIM/cim-mem-ctrl/cim/results/cache_tests/run_cpu_overhead.sh \
#  /nfshome/bellee@chapman.edu/CIM/cim-mem-ctrl/cim/results/cache_tests/run_cpu_setup.sh \
#  /nfshome/bellee@chapman.edu/CIM/cim-mem-ctrl/cim/results/cache_tests/run_cpu_total.sh \
#  /usr/bin/

# Default to run_cache_test.sh in the same directory as this script, unless overridden.
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
RUN_CACHE_TEST=${RUN_CACHE_TEST:-"$SCRIPT_DIR/run_cache_test.sh"}

WHICH=${1:-all}
RUNS=${2:-1}

run_one() {
  script="$1"
  echo "[run_cache_suite] running $script x $RUNS" >&2
  "$RUN_CACHE_TEST" "$script" "$RUNS"
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
  base)
    run_one run_base.sh
    ;;
  cim_overhead)
    run_one run_cim_overhead.sh
    ;;
  cim_setup)
    run_one run_cim_setup.sh
    ;;
  cim_total)
    run_one run_cim_total.sh
    ;;
  cpu_overhead)
    run_one run_cpu_overhead.sh
    ;;
  cpu_setup)
    run_one run_cpu_setup.sh
    ;;
  cpu_total)
    run_one run_cpu_total.sh
    ;;
  *)
    echo "Usage: $0 {all|base|cim_overhead|cim_setup|cim_total|cpu_overhead|cpu_setup|cpu_total} [runs]" >&2
    exit 2
    ;;
esac