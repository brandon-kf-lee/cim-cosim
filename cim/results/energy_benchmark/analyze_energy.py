import matplotlib.pyplot as plt
import numpy as np

# Create the results directory if it does not exist
os.makedirs("results", exist_ok=True)

# 1. Raw Data from Spreadsheet (in picoJoules)
# Split into CPU and CIM arrays to match the grouping logic of the instruction script
# Arrays are ordered: [MNIST, Synthetic]
# TODO: don't hardcode, grab from results directory
data_pj_cpu = {
    'CPU Pipeline':       [877644966.89,   107951053066.64],
    'Inst-Cache Acc.': [9636601977.97,  1180007451855.63],
    'Data-Cache Acc.': [2995627052.38,  603545750887.39],
    'DMA Traffic':        [0.0,            0.0],
    'CIM Compute':        [0.0,            0.0]
}

data_pj_cim = {
    'CPU Pipeline':       [100592447.58,   185482487.66],
    'Inst-Cache Acc.': [1176720092.37,  2038466771.75],
    'Data-Cache Acc.': [676516174.31,   752900800.68],
    'DMA Traffic':        [202770750.00,   1504588800.00],
    'CIM Compute':        [349843.82,      46790539.94]
}

categories = list(data_pj_cpu.keys())

# Convert all raw data from pJ to mJ (divide by 1,000,000,000)
for key in categories:
    data_pj_cpu[key] = [val / 1e9 for val in data_pj_cpu[key]]
    data_pj_cim[key] = [val / 1e9 for val in data_pj_cim[key]]

# 2. X-Axis Grouping Logic (Make bars touch)
networks = ['MNIST', 'Synthetic']
x = np.arange(len(networks))  # Label locations [0, 1]
width = 0.35                  # Bar width

x_cpu = x - width/2
x_cim = x + width/2

# Colors and hatches for B&W print compatibility
colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd']
hatches = ['', '\\\\', '//', 'xx', '++']

# 3. Setup the Broken Y-Axis Plot (Linear Scale)
fig, (ax_top, ax_bot) = plt.subplots(2, 1, sharex=True, figsize=(7, 6.5), gridspec_kw={'height_ratios': [1, 3]})
fig.subplots_adjust(hspace=0.08)  # Bring subplots close together

# 4. Plot the Stacked Bars on BOTH subplots
bottoms_cpu = np.zeros(len(networks))
bottoms_cim = np.zeros(len(networks))

for i, cat in enumerate(categories):
    val_cpu = np.array(data_pj_cpu[cat])
    val_cim = np.array(data_pj_cim[cat])
    
    # Plot CPU
    ax_top.bar(x_cpu, val_cpu, width, bottom=bottoms_cpu, color=colors[i], edgecolor='black', hatch=hatches[i])
    ax_bot.bar(x_cpu, val_cpu, width, bottom=bottoms_cpu, color=colors[i], edgecolor='black', hatch=hatches[i])
    
    # Plot CIM (Only add label to one of the axes so legend doesn't duplicate)
    ax_top.bar(x_cim, val_cim, width, bottom=bottoms_cim, color=colors[i], edgecolor='black', hatch=hatches[i])
    ax_bot.bar(x_cim, val_cim, width, bottom=bottoms_cim, label=cat, color=colors[i], edgecolor='black', hatch=hatches[i])
    
    bottoms_cpu += val_cpu
    bottoms_cim += val_cim

# 5. Configure the Y-Axis Limits (The "Break") in mJ
# Bottom plot: 0 to 14 mJ
ax_bot.set_ylim(0, 14)

# Top plot: 1850 mJ to 1950 mJ
ax_top.set_ylim(1850, 1950)

# Hide the spines between ax_top and ax_bot
ax_top.spines['bottom'].set_visible(False)
ax_bot.spines['top'].set_visible(False)
ax_top.xaxis.tick_top()
ax_top.tick_params(labeltop=False, bottom=False)  
ax_bot.xaxis.tick_bottom()

# 6. Draw the Diagonal "Break" Lines
# Because ax_top is 1/3 the height of ax_bot, the Y-slope must be multiplied by 3 to be physically parallel
d = 0.015  
dx = d
dy_bot = d
dy_top = d * 3

kwargs_top = dict(transform=ax_top.transAxes, color='k', clip_on=False, linewidth=2.5)
ax_top.plot((-dx, +dx), (-dy_top, +dy_top), **kwargs_top)        
ax_top.plot((1 - dx, 1 + dx), (-dy_top, +dy_top), **kwargs_top)  

kwargs_bot = dict(transform=ax_bot.transAxes, color='k', clip_on=False, linewidth=2.5)
ax_bot.plot((-dx, +dx), (1 - dy_bot, 1 + dy_bot), **kwargs_bot)  
ax_bot.plot((1 - dx, 1 + dx), (1 - dy_bot, 1 + dy_bot), **kwargs_bot)  

# --- Extra visible break markers inside the plot area ---
cap_halfwidth = width * 0.57   # tune (smaller = shorter cap)

# y position in *data units* at the break edge for each axis
y_cap_bot = ax_bot.get_ylim()[1]   # top of bottom axis (e.g., 14 mJ)
y_cap_top = ax_top.get_ylim()[0]   # bottom of top axis (e.g., 1850 mJ)

ax_bot.hlines(y_cap_bot, x_cpu[1] - cap_halfwidth, x_cpu[1] + cap_halfwidth,
                colors='k', linewidth=3, clip_on=False)
ax_top.hlines(y_cap_top, x_cpu[1] - cap_halfwidth, x_cpu[1] + cap_halfwidth,
                colors='k', linewidth=3, clip_on=False)


# 7. Formatting Labels and Grids
bkg_ax = fig.add_subplot(111, zorder=-1)
bkg_ax.spines['top'].set_color('none')
bkg_ax.spines['bottom'].set_color('none')
bkg_ax.spines['left'].set_color('none')
bkg_ax.spines['right'].set_color('none')
bkg_ax.tick_params(labelcolor='none', top=False, bottom=False, left=False, right=False)

bkg_ax.set_ylabel('Calculated Energy (mJ)', fontsize=11, fontweight='bold', labelpad=25)
ax_top.set_title('Estimated System Dynamic Energy', fontsize=12, fontweight='bold')

x_labels = ['MNIST [784x10]', 'Synthetic NN [1024x1024]']
ax_bot.set_xticks(x)
ax_bot.tick_params(axis='x', which='major', pad=25) 
ax_bot.set_xticklabels(x_labels, fontsize=11)
ax_bot.set_xlabel('Neural Network Workload [input x output]', fontsize=11, fontweight='bold', labelpad=15)

# Add "CPU-Only" and "CIM-Offload" Sub-labels
for i in range(len(networks)):
    ax_bot.text(x_cpu[i], -1.2, 'CPU-Only', ha='center', va='top', fontsize=9, fontweight='bold', clip_on=False)
    ax_bot.text(x_cim[i], -1.2, 'CIM-Offload', ha='center', va='top', fontsize=9, fontweight='bold', clip_on=False)

#  Force the Y-Axis to tick exactly every 2 units ***
ax_bot.set_yticks(np.arange(0, 15, 2))
ax_top.set_yticks(np.arange(1850, 1951, 50))

ax_top.grid(axis='y', linestyle='--', alpha=0.7, which='both')
ax_bot.grid(axis='y', linestyle='--', alpha=0.7, which='both')
ax_top.set_axisbelow(True)
ax_bot.set_axisbelow(True)

# 8. Add Absolute Energy Text Labels on Top of Bars
absolute_labels_cpu = ["13.5 mJ", "1.89 J"]
absolute_labels_cim = ["2.16 mJ", "4.53 mJ"]

# CPU Labels (MNIST on bottom plot, Synth on top plot)
ax_bot.text(x_cpu[0], bottoms_cpu[0] + 0.3, absolute_labels_cpu[0], ha='center', va='bottom', fontsize=10, fontweight='bold', color='darkred')
ax_top.text(x_cpu[1], bottoms_cpu[1] + 5, absolute_labels_cpu[1], ha='center', va='bottom', fontsize=10, fontweight='bold', color='darkred')

# CIM Labels (Both fit easily on the bottom plot)
ax_bot.text(x_cim[0], bottoms_cim[0] + 0.3, absolute_labels_cim[0], ha='center', va='bottom', fontsize=10, fontweight='bold', color='darkred')
ax_bot.text(x_cim[1], bottoms_cim[1] + 0.3, absolute_labels_cim[1], ha='center', va='bottom', fontsize=10, fontweight='bold', color='darkred')

# 9. Clean up Legend
handles, leg_labels = ax_bot.get_legend_handles_labels()
ax_bot.legend(handles[::-1], leg_labels[::-1], loc='upper center', bbox_to_anchor=(0.5, -0.35), ncol=3, fontsize=10, frameon=False)

# Save Graph 1
plt.tight_layout()
output_filename_1 = "results/total_energy"
plt.savefig(f"{output_filename_1}.pdf", format='pdf', bbox_inches='tight')
plt.savefig(f"{output_filename_1}.png", format='png', bbox_inches='tight')
print(f"Graph 1 generated: saved as '{output_filename_1}'")

# Synthetic NN Results

# 10. Extract Synthetic Data (Index 1) for CPU (Values are already in mJ from Step 1)
synth_cpu_vals = np.array([data_pj_cpu[cat][1] for cat in categories])

# Make a slightly narrower figure since we are only plotting one bar
fig2, ax2 = plt.subplots(figsize=(4.5, 6))
x_pos = [0]
bar_width = 0.5
bottom_cpu = 0

# Box style to make text legible over dark colors/hatches
text_box_props = dict(boxstyle="round,pad=0.2", facecolor="white", edgecolor="none", alpha=0.85)

for i, cat in enumerate(categories):
    val = synth_cpu_vals[i]
    
    # Only add to the legend if the energy is greater than 0 
    # (This automatically excludes DMA Traffic and CIM Compute for the CPU)
    bar_label = cat if val > 0 else None
    
    # Plot CPU Raw Block
    ax2.bar(0, val, bar_width, bottom=bottom_cpu, 
            color=colors[i], edgecolor='black', hatch=hatches[i], label=bar_label)
    
    # Add text label inside the bar if the value is large enough to see (> 50 mJ)
    if val > 50:
        y_pos = bottom_cpu + (val / 2)
        ax2.text(0, y_pos, f"{val:.1f} mJ", 
                 ha='center', va='center', fontweight='bold', fontsize=10, bbox=text_box_props)
    
    bottom_cpu += val

# Add the Total label at the very top of the bar
ax2.text(0, bottom_cpu + 30, f"Total: {bottom_cpu:.1f} mJ\n(1.89 J)", 
         ha='center', va='bottom', fontsize=11, fontweight='bold', color='darkred')

# Formatting the Split Graph
ax2.set_xticks(x_pos)
ax2.set_xticklabels(['Synthetic NN\n(CPU-Only)'], fontsize=11, fontweight='bold')
ax2.set_ylabel('Calculated Energy (mJ)', fontsize=11, fontweight='bold')
ax2.set_title('Energy Split', fontsize=12, fontweight='bold')

# Give the top of the graph a little breathing room for the total label
ax2.set_ylim(0, bottom_cpu + 200)

# Move Legend to the right side
handles2, leg_labels2 = ax2.get_legend_handles_labels()
ax2.legend(handles2[::-1], leg_labels2[::-1], loc='center left', bbox_to_anchor=(1.05, 0.5), 
           fontsize=10, frameon=False, title="Energy Source", title_fontproperties={'weight':'bold'})

plt.tight_layout()

# Save Graph 2
output_filename_2 = "results/synthetic_cpu_split"
plt.savefig(f"{output_filename_2}.pdf", format='pdf', bbox_inches='tight')
plt.savefig(f"{output_filename_2}.png", format='png', bbox_inches='tight')
print(f"Graph 2 generated: saved as '{output_filename_2}'")