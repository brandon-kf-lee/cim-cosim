import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os

# Create the results directory if it does not exist
os.makedirs("results", exist_ok=True)

# Load and clean the data
csv_file = "instruction_profiling.csv"

if not os.path.exists(csv_file):
    print(f"Error: {csv_file} not found!")
    exit(1)

# Read CSV, skipping spaces after commas
df = pd.read_csv(csv_file, skipinitialspace=True)

# Clean column names and string data just in case of trailing spaces
df.columns = df.columns.str.strip()
df['Target'] = df['Target'].str.strip()
df['Network'] = df['Network'].str.strip()

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

# ==========================================
# CONSOLE OUTPUT: Print Medians
# ==========================================
print("\n" + "="*45)
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

# Prepare data for plotting
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

    print(cpu_val)
    print(cim_val)

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
output_filename = "results/CPU_instructions_per_inference"
plt.savefig(f"{output_filename}.pdf", format='pdf', bbox_inches='tight')
plt.savefig(f"{output_filename}.png", format='png', bbox_inches='tight')

print(f"Graph successfully generated: saved as '{output_filename}'")