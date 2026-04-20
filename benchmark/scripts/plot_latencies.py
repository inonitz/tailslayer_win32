import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as plt_tick
import pathlib
import sys


def plot_csv_benchmarks(csv_files):
    """
    Reads multiple CSV files and plots their latency survival functions.
    Expects each CSV to have a column of latency values (nanoseconds).
    """
    if not csv_files:
        print("No CSV files provided.")
        return

    fig, ax = plt.subplots(figsize=(10, 6), dpi=150)
    
    # Using a professional color cycle
    prop_cycle = plt.rcParams['axes.prop_cycle']
    colors = prop_cycle.by_key()['color']

    for i, file_path in enumerate(csv_files):
        path = pathlib.Path(file_path)
        
        try:
            # Read a single-column CSV File with a header at index 0
            # Take the first columns' values only, i.e only latencies
            sorted_latencies = ( pd.read_csv(path, header=0) ).iloc[:, 0].values
            sorted_latencies = np.sort(sorted_latencies)
            
            # 2. Calculate the Survival Function (1 - CDF)
            # Probability that latency is greater than X
            n = len(sorted_latencies)
            if n == 0:
                raise ValueError("No values have been read from {}".format(path))
            p = 1.0 - np.arange(1, n + 1) / n



            # Clip for log scale stability (down to 1 in a million)
            p = np.clip(p, a_min=1e-7, a_max=1.0)
            
            # 3. Plotting
            label = path.stem.replace('_', ' ').title() # Use filename as label
            ax.plot(sorted_latencies, p, label=label, color=colors[i % len(colors)], linewidth=1.5)
            
            print(f"Processed {path.name}: {n} samples.")
            
        except Exception as e:
            print(f"Error processing {path.name}: {e}")


    ax.set_yscale('log')
    ax.set_xscale('log')
    ax.set_ylim(1e-6, 1.5)
    
    # if len(csv_files) > 0:
    #     ax.set_xlim(left=0, right=np.percentile(latencies, 99.99) * 1.2)

    # Use MaxNLocator instead of MultipleLocator to prevent the 11M ticks error
    # ax.xaxis.set_major_locator(plt_tick.MaxNLocator(nbins=10)) 
    # ax.xaxis.set_minor_locator(plt_tick.AutoMinorLocator())
    ax.yaxis.set_major_locator(plt_tick.LogLocator(base=10.0, numticks=10))
    # ax.yaxis.set_minor_locator(plt_tick.AutoMinorLocator())

    # Styling
    ax.set_title('Channel-Hedged RAM Latency', fontsize=14, fontweight='bold')
    ax.set_xlabel('Latency (ns)', fontsize=12)
    ax.set_ylabel('Probability > X', fontsize=12)
    ax.grid(True, which='major', linestyle='-', alpha=0.5)
    ax.grid(True, which='minor', linestyle=':', alpha=0.2)
    ax.legend(loc='upper right')

    plt.tight_layout()
    plt.show()

if __name__ == "__main__":
    # You can pass filenames as arguments or list them here:
    # Usage: python script.py data_ch1.csv data_ch2.csv hedged_results.csv
    if len(sys.argv) > 1:
        files = sys.argv[1:]
    else:
        # Default fallback: search for all CSVs in the current directory
        files = list(pathlib.Path('.').glob('*.csv'))
        
    plot_csv_benchmarks(files)