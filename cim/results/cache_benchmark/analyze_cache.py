#!/usr/bin/env python3
"""
analyze_cache.py

Parse QEMU Cache Modelling TCG Plugin logs and compute representative (median) counters and
delta-derived metrics for CIM & CPU sections.
https://www.qemu.org/2021/08/19/tcg-cache-modelling-plugin/

Expected files under ./results/[mnist|synthetic] like:
  run_base_1.log
  run_cim_overhead_1.log
  run_cim_setup_1.log
  run_cim_total_1.log
  run_cpu_overhead_1.log
  run_cpu_setup_1.log
  run_cpu_total_1.log
(with any number of repeats)

Each log contains a line like:
core #, data accesses, data misses, dmiss rate, insn accesses, insn misses, imiss rate
0       291556447      2309371         0.7921%  1294404362     3023663         0.2336%

Compute:
- Representative stats (median) for each test group.
- Overhead-only:           (overhead - base)
- Setup-only (1x):         (setup - overhead) / setup_iters
- Inference-only total:    (total - overhead - setup_1x)
- Inference-only per-iter: inference_only / iters

"""

from __future__ import annotations

import argparse
import glob
import os
import re
import statistics as stats
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

SETUP_ITERS = 10000  # For amplifying setup region for easier measurement
RESULTS_BASE_DIR = "./results"

CORE0_RE = re.compile(
    r"^\s*0\s+(\d+)\s+(\d+)\s+([0-9.]+)%\s+(\d+)\s+(\d+)\s+([0-9.]+)%\s*$"
)

@dataclass(frozen=True)
class Counters:
    dacc: int
    dmiss: int
    dmiss_rate_pct: float
    iacc: int
    imiss: int
    imiss_rate_pct: float

    def __sub__(self, other: "Counters") -> "Counters":
        dacc = self.dacc - other.dacc
        dmiss = self.dmiss - other.dmiss
        iacc = self.iacc - other.iacc
        imiss = self.imiss - other.imiss
        # recompute rates from counts (safer than subtracting %)
        dmiss_rate_pct = (100.0 * dmiss / dacc) if dacc > 0 else 0.0
        imiss_rate_pct = (100.0 * imiss / iacc) if iacc > 0 else 0.0
        return Counters(dacc, dmiss, dmiss_rate_pct, iacc, imiss, imiss_rate_pct)

    def div(self, n: float) -> "Counters":
        if n == 0:
            raise ValueError("division by zero")
        dacc = int(round(self.dacc / n))
        dmiss = int(round(self.dmiss / n))
        iacc = int(round(self.iacc / n))
        imiss = int(round(self.imiss / n))
        dmiss_rate_pct = (100.0 * dmiss / dacc) if dacc > 0 else 0.0
        imiss_rate_pct = (100.0 * imiss / iacc) if iacc > 0 else 0.0
        return Counters(dacc, dmiss, dmiss_rate_pct, iacc, imiss, imiss_rate_pct)


def parse_core0(path: str) -> Optional[Counters]:
    """Return Counters for core 0 line, or None if not found."""
    with open(path, "r", errors="replace") as f:
        for line in f:
            m = CORE0_RE.match(line)
            if m:
                dacc = int(m.group(1))
                dmiss = int(m.group(2))
                dmiss_rate_pct = float(m.group(3))
                iacc = int(m.group(4))
                imiss = int(m.group(5))
                imiss_rate_pct = float(m.group(6))
                return Counters(dacc, dmiss, dmiss_rate_pct, iacc, imiss, imiss_rate_pct)
    return None


def load_group(directory: str, prefix: str) -> List[Tuple[str, Counters]]:
    """Load all logs matching prefix_*.log in the given directory"""
    pattern = os.path.join(directory, f"{prefix}_*.log")
    out: List[Tuple[str, Counters]] = []
    for p in sorted(glob.glob(pattern)):
        c = parse_core0(p)
        if c is None:
            continue
        out.append((os.path.basename(p), c))
    return out

def median_counters(cs: List[Counters]) -> Counters:
    dacc = int(stats.median([c.dacc for c in cs]))
    dmiss = int(stats.median([c.dmiss for c in cs]))
    iacc = int(stats.median([c.iacc for c in cs]))
    imiss = int(stats.median([c.imiss for c in cs]))
    dmiss_rate_pct = (100.0 * dmiss / dacc) if dacc else 0.0
    imiss_rate_pct = (100.0 * imiss / iacc) if iacc else 0.0
    return Counters(dacc, dmiss, dmiss_rate_pct, iacc, imiss, imiss_rate_pct)


def fmt_counts(c: Counters) -> str:
    # fixed width columns; commas kept for readability
    return (
        f"Dacc={c.dacc:>12,}  "
        f"Dmiss={c.dmiss:>9,}  "
        f"Dmiss%={c.dmiss_rate_pct:>8.4f}  |  "
        f"Iacc={c.iacc:>12,}  "
        f"Imiss={c.imiss:>9,}  "
        f"Imiss%={c.imiss_rate_pct:>8.4f}"
    )

def fmt_row(label: str, c: Counters) -> str:
    return f"{label:<6s}  {fmt_counts(c)}"


def main() -> None:    
    ap = argparse.ArgumentParser()
    ap.add_argument("--iters", type=int, default=1000, help="number of inferences in each runs")
    ap.add_argument("--print-all-regions", action="store_true", help="print data for all test regions")
    ap.add_argument("--print-raw", action="store_true", help="print median raw values for each test group")
    args = ap.parse_args()

    iters = args.iters
    print_all_regions = bool(args.print_all_regions)
    print_raw = bool(args.print_raw)

    if not os.path.isdir(RESULTS_BASE_DIR):
        print(f"ERROR: Base results directory '{RESULTS_BASE_DIR}' not found!")
        return

    # Find all subdirectories in ./results (e.g., 'mnist', 'synthetic')
    networks = [d for d in os.listdir(RESULTS_BASE_DIR) if os.path.isdir(os.path.join(RESULTS_BASE_DIR, d))]
    
    if not networks:
        print(f"ERROR: No network subdirectories found in '{RESULTS_BASE_DIR}'. Expected folders like 'mnist' or 'synthetic'.")
        return

    print("Key:")
    if(print_all_regions):
        print("overhd: Overhead region: simulation overhead. Variable declaration, memory alignment")
        print("setup: Setup region: CIM device configuration, loading neural network")
        print(f"infer: Total inference region: All {iters} inferences in one one")
    print("iter: Number of cache accesses per inference")

    for network in sorted(networks):
        print(f"\n==============================================================================")
        print(f" NETWORK WORKLOAD: {network.upper()}")
        print(f"==============================================================================")

        network_dir = os.path.join(RESULTS_BASE_DIR, network)

        groups = {
            "base": load_group(network_dir, "run_base"),
            "cim_overhead": load_group(network_dir, "run_cim_overhead"),
            "cim_setup": load_group(network_dir, "run_cim_setup"),
            "cim_total": load_group(network_dir, "run_cim_total"),
            "cpu_overhead": load_group(network_dir, "run_cpu_overhead"),
            "cpu_setup": load_group(network_dir, "run_cpu_setup"),
            "cpu_total": load_group(network_dir, "run_cpu_total"),
        }

        if not groups["base"]:
            print(f"  [!] Skipping {network}: Missing 'run_base' logs.")
            continue

        # Representative (median) for each group
        rep: Dict[str, Dict[str, Counters]] = {}
        for name, items in groups.items():
            if not items:
                continue
            cs = [c for _, c in items]
            rep[name] = {
                "median": median_counters(cs),
            }

        # Print raw stats if desired
        if (print_raw):
            print("=== Representative raw counters per group (median) ===")
            for name in ("base", "cim_overhead", "cim_setup", "cim_total", "cpu_overhead", "cpu_setup", "cpu_total"):
                if name not in rep:
                    continue
                print(fmt_row(name, "median", rep[name]["median"]))

        base = rep["base"]["median"]

        # ------------ CIM section deltas ------------
        has_cim = all(g in rep for g in ("cim_overhead", "cim_setup", "cim_total"))
        if has_cim:
            cim_oh = rep["cim_overhead"]["median"]
            cim_setup_raw = rep["cim_setup"]["median"]
            cim_total = rep["cim_total"]["median"]

            cim_overhead_only = cim_oh - base
            cim_setup_amplified = cim_setup_raw - cim_oh
            cim_setup_only = cim_setup_amplified.div(SETUP_ITERS)
            
            cim_infer_total = (cim_total - cim_oh) - cim_setup_only
            cim_infer_per_iter = cim_infer_total.div(iters)

            print("\n=== CIM derived metrics (using median of each group) ===")
            if(print_all_regions):
                print(fmt_row("overhd", cim_overhead_only))
                print(fmt_row("setup",  cim_setup_only))
                print(fmt_row("infer",  cim_infer_total))
            print(fmt_row("iter",   cim_infer_per_iter))
        else:
            print("\n=== CIM derived metrics ===")
            print("  [!] Missing CIM logs, skipping calculations.")

        # ------------ CPU section deltas ------------
        has_cpu = all(g in rep for g in ("cpu_overhead", "cpu_setup", "cpu_total"))
        if has_cpu:
            cpu_oh = rep["cpu_overhead"]["median"]
            cpu_setup_raw = rep["cpu_setup"]["median"]
            cpu_total = rep["cpu_total"]["median"]

            cpu_overhead_only = cpu_oh - base
            cpu_setup_amplified = cpu_setup_raw - cpu_oh
            cpu_setup_only = cpu_setup_amplified.div(SETUP_ITERS)
            
            cpu_infer_total = (cpu_total - cpu_oh) - cpu_setup_only
            cpu_infer_per_iter = cpu_infer_total.div(iters)

            print("\n=== CPU derived metrics (using median of each group) ===")
            if(print_all_regions):
                print(fmt_row("overhd", cpu_overhead_only))
                print(fmt_row("setup",  cpu_setup_only))
                print(fmt_row("infer",  cpu_infer_total))
            print(fmt_row("iter",   cpu_infer_per_iter))
        else:
            print("\n=== CPU derived metrics ===")
            print("  [!] Missing CPU logs, skipping calculations.")

if __name__ == "__main__":
    main()