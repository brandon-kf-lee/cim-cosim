import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os
import glob
import re
import sys

# ==========================================
# CONFIGURATION
# ==========================================
OUTPUT_DIR = "results"             # Results directory

if (len(sys.argv) < 2):
    print("Error: missing number of inference iterations per run")
    sys.exit(1)
ITERATIONS_PER_RUN = int(sys.argv[1])   # Number of inferences per run

os.makedirs(OUTPUT_DIR, exist_ok=True)

# ==========================================
# LOG PARSER
# ==========================================
print(f"Scanning for log files in '{OUTPUT_DIR}'...")
log_files = glob.glob(f"{OUTPUT_DIR}/**/*.log", recursive=True)

if not log_files:
    print(f"Error: No .log files found in {OUTPUT_DIR}! Run the bash orchestrator first.")
    exit(1)

parsed_data = []

for filepath in log_files:
    filename = os.path.basename(filepath).lower()
    folder = os.path.basename(os.path.dirname(filepath)).lower()
    
    # Determine Hardware Target
    if "cim" in filename:
        target = "CIM"
    elif "cpu" in filename:
        target = "CPU"
    else:
        print("Error: unknown target, skipping.")
        continue # Skip unrecognized logs
        
    # Determine Network Topology
    if "mnist" in folder or "mnist" in filename:
        network = "MNIST"
    elif "synthetic" in folder or "synthetic" in filename:
        network = "Synthetic"
    else:
        print("Error: unknown network, skipping.")
        continue # Skip unrecognized networks

    # Read the log file line by line
    with open(filepath, 'r') as f:
        setup_val = None
        
        for line in f:
            # Extract setup instructions
            setup_match = re.search(r'setup instructions=\s*(\d+)', line)
            if setup_match:
                setup_val = int(setup_match.group(1))
                
            # Extract inference instructions
            infr_match = re.search(r'inference instructions=\s*(\d+)', line)
            if infr_match:
                infr_val = int(infr_match.group(1))
                
                # We expect setup to print right before inference in the C script
                if setup_val is not None:
                    parsed_data.append({
                        'Network': network,
                        'Target': target,
                        'Iterations': ITERATIONS_PER_RUN,
                        'Setup_Inst': setup_val,
                        'Inference_Inst': infr_val
                    })
                    setup_val = None # Reset for the next run in the same log

# Convert to DataFrame
df = pd.DataFrame(parsed_data)

if df.empty:
    print("Error: Could not find any valid instruction counts in the logs.")
    exit(1)

# Save the parsed raw data as a clean CSV 
csv_file = os.path.join(OUTPUT_DIR, "instruction_benchmarking.csv")
df.to_csv(csv_file, index=False)
print(f"Successfully parsed {len(df)} runs. Saved raw data to '{csv_file}'\n")


# ==========================================
# DATA AGGREGATION & CONSOLE OUTPUT
# ==========================================
# Calculate the per-inference instructions
df['Per_Inference_Inst'] = df['Inference_Inst'] / df['Iterations']

# Aggregate the data using median to filter out OS & simulation noise
inference_agg_df = df.groupby(['Network', 'Target'])['Per_Inference_Inst'].median().unstack()
setup_agg_df = df.groupby(['Network', 'Target'])['Setup_Inst'].median().unstack()

# Ensure networks are ordered logically (MNIST first, then Synthetic)
networks_present = inference_agg_df.index.tolist()
networks = []
if 'MNIST' in networks_present: networks.append('MNIST')
if 'Synthetic' in networks_present: networks.append('Synthetic')

print("="*45)
print(" MEDIAN INSTRUCTION COUNTS SUMMARY")
print("="*45)
for n in networks:
    print(f"NETWORK: {n}")
    print(f"  Setup Region (Total Instructions):")
    print(f"    CPU-Only   : {setup_agg_df.loc[n, 'CPU']:,.0f}")
    print(f"    CIM Offload: {setup_agg_df.loc[n, 'CIM']:,.0f}")
    print(f"  Inference Region (Per Inference):")
    print(f"    CPU-Only   : {inference_agg_df.loc[n, 'CPU']:,.0f}")
    print(f"    CIM Offload: {inference_agg_df.loc[n, 'CIM']:,.0f}")
    print("-" * 45)
print("\n")


# ==========================================
# GRAPH GENERATION
# ==========================================
cpu_medians = [inference_agg_df.loc[n, 'CPU'] for n in networks]
cim_medians = [inference_agg_df.loc[n, 'CIM'] for n in networks]

# Setup the plot
fig, ax = plt.subplots(figsize=(7, 4.5))

x = np.arange(len(networks))  # Label locations
width = 0.35                  # Bar width

# Plot bars with edge colors and hatch patterns for B&W print compatibility
rects1 = ax.bar(x - width/2, cpu_medians, width, label='CPU-Only', 
                color='#1f77b4', edgecolor='black')
rects2 = ax.bar(x + width/2, cim_medians, width, label='CIM Offload', 
                color='#aec7e8', edgecolor='black', hatch='//')

# Formatting the Y-Axis (Logarithmic)
ax.set_yscale('log')
ax.set_ylim(bottom=1) 
ax.set_ylabel('Instructions per Inference (Log Scale)', fontsize=11, fontweight='bold')
ax.set_title('CPU Instructions per Inference', fontsize=12, fontweight='bold')

# Formatting the X-Axis
# Create descriptive labels based on the network name
x_labels = []
for n in networks:
    if n == 'MNIST': x_labels.append('MNIST\n[784x10]')
    elif n == 'Synthetic': x_labels.append('Synthetic NN\n[1024x1024]')
    else: x_labels.append(n)

ax.set_xticks(x)
ax.set_xticklabels(x_labels, fontsize=11)
ax.set_xlabel('Neural Network Workload [input x output]', fontsize=11, fontweight='bold')
ax.legend(fontsize=10, loc='upper left')

# Add percentage reduction labels directly above the CIM bars
for i in range(len(networks)):
    cpu_val = cpu_medians[i]
    cim_val = cim_medians[i]

    # Calculate reduction percentage
    reduction = ((cpu_val - cim_val) / cpu_val) * 100
    
    # Place text slightly above the CIM bar in log-space
    text_y_pos = cim_val * 1.3 
    
    ax.text(x[i] + width/2, text_y_pos, f"-{reduction:.1f}%", 
            ha='center', va='bottom', fontsize=10, fontweight='bold', color='darkred')

# Add a subtle grid for readability
ax.grid(axis='y', linestyle='--', alpha=0.7, which='both')
ax.set_axisbelow(True) # Put grid behind bars

# Tight layout formatting
plt.tight_layout()

# Save the plot as PDF & PNG
output_filename = os.path.join(OUTPUT_DIR, "CPU_instructions_per_inference")
plt.savefig(f"{output_filename}.pdf", format='pdf', bbox_inches='tight')
plt.savefig(f"{output_filename}.png", format='png', bbox_inches='tight')

print(f"Graph successfully generated: saved as '{output_filename}'")