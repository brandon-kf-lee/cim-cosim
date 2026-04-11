#!/usr/bin/env python3
"""
analyze_bench.py

Reads inst_per_infr and amortized CSVs produced by run_bench.sh, computes median/mean/IQR
(per-iteration), writes summary CSVs, and generates basic plots.

run_bench.sh runs inside the emulated QEMU system, so the output CSV files must
be extracted to the host to be able to run this script.

Input files (default):
  - inst_per_infr.csv
  - amortized.csv

Outputs:
  - inst_per_infr.svg
  - inst_per_infr_summary.csv

  - amortized.svg
  - amortized_summary.csv
"""

import argparse
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.ticker import LogLocator, FuncFormatter

NUM_COLS = [
    "iters", "warmup", "repeat",
    "instructions", "cycles", "context_switches",
    "acc_correct", "acc_total", "acc_pct",
]

STR_COLS = ["test", "impl", "mode", "measure"]

def q1(x): return x.quantile(0.25)
def q3(x): return x.quantile(0.75)


# Format for plain numbers
def log_plain_number_fmt(x, pos=None):
    # Show 10, 100, 1000, ... (no 10^k)
    if x <= 0:
        return ""
    # Only label exact powers of 10
    e = np.log10(x)
    if abs(e - round(e)) < 1e-10:
        return f"{int(x):,}"
    return ""

# Format for SI units (k/M/G)
def si_fmt(x, pos=None):
    x = float(x)
    ax = abs(x)
    if ax < 1e3:
        return f"{x:.0f}"
    elif ax < 1e6:
        v = x / 1e3
        s = f"{v:.1f}".rstrip("0").rstrip(".")
        return f"{s}k"
    elif ax < 1e9:
        v = x / 1e6
        s = f"{v:.1f}".rstrip("0").rstrip(".")
        return f"{s}M"
    elif ax < 1e12:
        v = x / 1e9
        s = f"{v:.1f}".rstrip("0").rstrip(".")
        return f"{s}G"
    else:
        return f"{x:.2e}"

def load_csv(path: str) -> pd.DataFrame:
    df = pd.read_csv(path, skipinitialspace=True)

    # Normalize headers (fix BOM + stray spaces + case)
    df.columns = (
        df.columns.astype(str)
        .str.replace("\ufeff", "", regex=False)
        .str.strip()
    )

    # Strip whitespace in string columns
    for c in STR_COLS:
        if c in df.columns:
            df[c] = df[c].astype(str).str.strip()

    # Convert numeric columns; blanks -> NaN
    for c in NUM_COLS:
        if c in df.columns:
            df[c] = pd.to_numeric(df[c], errors="coerce")

    # Fail fast with message
    required = ["iters", "instructions", "cycles", "context_switches"]
    missing = [c for c in required if c not in df.columns]
    if missing:
        raise ValueError(f"{path}: missing columns {missing}. Have: {df.columns.tolist()}")

    # Per-iteration metrics
    df["instr_per_iter"] = df["instructions"] / df["iters"]
    df["cycles_per_iter"] = df["cycles"] / df["iters"]
    df["ctxsw_per_iter"] = df["context_switches"] / df["iters"]

    return df

def summarize(df: pd.DataFrame) -> pd.DataFrame:
    group_cols = ["test", "impl", "mode", "measure", "iters", "warmup"]

    summary = (
        df.groupby(group_cols)
          .agg(
              runs=("instr_per_iter", lambda s: s.notna().sum()),

              instr_mean=("instr_per_iter", "mean"),
              instr_median=("instr_per_iter", "median"),
              instr_q1=("instr_per_iter", q1),
              instr_q3=("instr_per_iter", q3),

              cycles_mean=("cycles_per_iter", "mean"),
              cycles_median=("cycles_per_iter", "median"),
              cycles_q1=("cycles_per_iter", q1),
              cycles_q3=("cycles_per_iter", q3),

              ctxsw_mean=("ctxsw_per_iter", "mean"),
              ctxsw_median=("ctxsw_per_iter", "median"),
              ctxsw_q1=("ctxsw_per_iter", q1),
              ctxsw_q3=("ctxsw_per_iter", q3),
          )
          .reset_index()
    )

    summary["instr_iqr"] = summary["instr_q3"] - summary["instr_q1"]
    summary["cycles_iqr"] = summary["cycles_q3"] - summary["cycles_q1"]
    summary["ctxsw_iqr"] = summary["ctxsw_q3"] - summary["ctxsw_q1"]
    return summary


# Plot instructions per inference graph
def plot_inst_per_infr(summary: pd.DataFrame, out_svg: str, ymin=0, ymax=4_000_000) -> None:
    plot_df = summary[summary["measure"] == "steady"].copy()

    # Normalize mode labels for display
    mode_map = {"single": "Single mode", "t10k": "T10K mode"}
    plot_df["mode_disp"] = (
        plot_df["mode"].astype(str).str.strip().str.lower().map(mode_map)
    )
    plot_df["mode_disp"] = plot_df["mode_disp"].fillna(plot_df["mode"])

    modes = ["Single mode", "T10K mode"]
    mode_to_x = {"Single mode": 0.0, "T10K mode": 0.4}  # smaller gap => closer points
    plot_df = plot_df[plot_df["mode_disp"].isin(modes)].copy()
    plot_df["x"] = plot_df["mode_disp"].map(mode_to_x)

    impl_colors = {"CPU": "tab:red", "CIM": "tab:blue"}
    impl_order = ["CPU", "CIM"]

    # Slight horizontal dodge so CIM/CPU are side-by-side within each mode 
    # Currently set to 0 since there is no overlap
    dodge = {"CPU": -0.00, "CIM": +0.00}

    plt.figure(figsize=(5.2, 4))  # narrower => points feel closer
    ax = plt.gca()

    for impl in impl_order:
        g = plot_df[plot_df["impl"] == impl].copy()
        if g.empty:
            continue

        x = g["x"].to_numpy() + g["impl"].map(dodge).to_numpy()
        y = g["instr_median"].to_numpy()
        yerr_low = (g["instr_median"] - g["instr_q1"]).to_numpy()
        yerr_high = (g["instr_q3"] - g["instr_median"]).to_numpy()

        ax.errorbar(
            x, y, yerr=[yerr_low, yerr_high],
            fmt="o", capsize=4,
            color=impl_colors.get(impl, None),
            label=impl,
        )

    tick_pos = [mode_to_x[m] for m in modes]
    ax.set_xticks(tick_pos)
    ax.set_xticklabels(modes)
    ax.set_xlim(min(tick_pos) - 0.25, max(tick_pos) + 0.25)
    ax.set_ylabel("Instructions per inference (median ± IQR)")
    ax.set_title("Instructions per inference (steady-state)")
    ax.yaxis.set_major_formatter(FuncFormatter(si_fmt))

    # Set minimum y-axis range
    ax.set_ylim(ymin, ymax)

    # Tighter x spacing / less empty space on sides
    ax.margins(x=0.02)

    # Boxed legend in top-right
    ax.legend(loc="upper right", frameon=True)

    plt.tight_layout()
    plt.savefig(out_svg, format="svg")
    plt.close()

# Plot amortized loading graph
def plot_amortized(summaryB: pd.DataFrame, out_svg: str, steady_baseline: pd.DataFrame) -> None:
    plot_df = summaryB.copy().sort_values(["impl", "mode", "measure", "iters"])

    plt.figure(figsize=(8, 4))
    ax = plt.gca()

    for (impl, meas, mode), g in plot_df.groupby(["impl", "measure", "mode"]):
        g = g.sort_values("iters")
        x = g["iters"].to_numpy()
        y = g["instr_median"].to_numpy()
        y1 = g["instr_q1"].to_numpy()
        y3 = g["instr_q3"].to_numpy()

        label = f"{impl}-{mode}-{meas}"
        ax.plot(x, y, marker="o", linewidth=1.5, label=label)
        ax.fill_between(x, y1, y3, alpha=0.2)

        # Draw asymptote for CIM total: steady-state median from inst_per_infr test
        if impl == "CIM" and meas == "total":
            row = steady_baseline[(steady_baseline["impl"] == impl) & (steady_baseline["mode"] == mode)]
            if not row.empty:
                asym = float(row["instr_median"].iloc[0])
                ax.axhline(asym, color="black", linestyle="--", linewidth=2, alpha=0.8)
                ax.text(
                    0.99, 0.05, f"Steady-state asymptote ≈ {si_fmt(asym)} instr/inf",
                    transform=ax.transAxes, ha="right", va="bottom", fontsize=9
                )

    ax.set_xscale("log")
    ax.set_xlabel("Number of inferences (log scale)")
    ax.xaxis.set_major_locator(LogLocator(base=10))
    ax.xaxis.set_major_formatter(FuncFormatter(log_plain_number_fmt))

    ax.set_ylabel("Instructions per inference (median ± IQR)")
    ax.set_title("Amortization of (One-Time) Weight & Bias Loading")
    ax.yaxis.set_major_formatter(FuncFormatter(si_fmt))
    ax.set_ylim(bottom=0)

    ax.legend()
    plt.tight_layout()
    plt.savefig(out_svg, format="svg")
    plt.close()

# Plot break even graph
def plot_break_even(summaryC: pd.DataFrame, out_svg: str) -> None:
    # Expect t10k, measure=total
    plot_df = summaryC.copy()
    plot_df = plot_df[(plot_df["mode"] == "t10k") & (plot_df["measure"] == "total")].copy()

    # Pivot to compute break-even on medians
    piv = plot_df.pivot_table(index="iters", columns="impl", values="instr_median", aggfunc="first").sort_index()

    n_star = None
    if "CIM" in piv.columns and "CPU" in piv.columns:
        wins = piv["CIM"] < piv["CPU"]
        if wins.any():
            n_star = int(wins[wins].index.min())
            cim_val = float(piv.loc[n_star, "CIM"])
            cpu_val = float(piv.loc[n_star, "CPU"])
            print(f"Break-even N* = {n_star} (CIM {cim_val:.2f} < CPU {cpu_val:.2f} instr/inf)")
        else:
            print("Break-even: CIM never below CPU over swept N values.")
    else:
        print("Break-even: missing CIM and/or CPU series in data.")

    plt.figure(figsize=(8, 4))
    ax = plt.gca()

    impl_colors = {"CPU": "tab:red", "CIM": "tab:blue"}
    impl_order = ["CPU", "CIM"]

    for impl in impl_order:
        g = plot_df[plot_df["impl"] == impl].sort_values("iters")
        if g.empty:
            continue
        x = g["iters"].to_numpy()
        y = g["instr_median"].to_numpy()
        y1 = g["instr_q1"].to_numpy()
        y3 = g["instr_q3"].to_numpy()

        ax.plot(x, y, marker="o", linewidth=1.5, color=impl_colors.get(impl), label=f"{impl} total")
        ax.fill_between(x, y1, y3, alpha=0.2, color=impl_colors.get(impl))

    # Mark break-even point with a vertical line
    if n_star is not None:
        ax.axvline(n_star, color="black", linestyle="--", linewidth=1, alpha=0.6)
        ax.text(
            n_star, 0.98, f"N*={n_star}",
            transform=ax.get_xaxis_transform(),
            ha="left", va="top", fontsize=9
        )

    ax.set_xlabel("Number of inferences")
    ax.set_xlim(0, plot_df["iters"].max())

    ax.set_ylabel("Instructions per inference (median ± IQR)")
    ax.set_title("CIM vs CPU Break-even Point")
    ax.yaxis.set_major_formatter(FuncFormatter(si_fmt))
    ax.set_ylim(bottom=0)

    ax.legend()
    plt.tight_layout()
    plt.savefig(out_svg, format="svg")
    plt.close()

def main():
    # ---- inst_per_infr (A) ----
    dfA = load_csv("inst_per_infr.csv")
    sumA = summarize(dfA)
    sumA.to_csv("inst_per_infr_summary.csv", index=False)
    print(f"Wrote inst_per_infr_summary.csv")
    plot_inst_per_infr(sumA, "inst_per_infr.svg", ymin=0, ymax=4_000_000)
    plot_inst_per_infr(sumA, "inst_per_infr_zoom.svg", ymin=2_600_000, ymax=3_900_000)
    print(f"Wrote inst_per_infr.svg")
    print(f"Wrote inst_per_infr_zoom.svg")
    
    # Copy baseline for amortized measurement
    steady_baseline = sumA[(sumA["measure"] == "steady")].copy()

    # ---- amortized (B) ----
    dfB = load_csv("amortized.csv")
    sumB = summarize(dfB)
    sumB.to_csv("amortized_summary.csv.csv", index=False)
    print(f"Wrote amortized_summary.csv.csv")
    plot_amortized(sumB, "amortized.svg", steady_baseline=steady_baseline)
    print(f"Wrote amortized.svg")

    # ---- break-even (C) ----
    dfC = load_csv("break_even.csv")
    sumC = summarize(dfC)
    sumC.to_csv("break_even_summary.csv", index=False)
    print("Wrote break_even_summary.csv")
    plot_break_even(sumC, "break_even.svg")
    print("Wrote break_even.svg")

if __name__ == "__main__":
    main()