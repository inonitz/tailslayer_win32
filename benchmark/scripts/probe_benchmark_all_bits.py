import subprocess
import sys



def run_benchmarks(bench_exe_path, args, bit_begin, bit_end, benchmarkBool, graphBool):


    for i in range(bit_begin, bit_end):    
        args['--channel-bit'] = str(i)
        args['--raw-prefix']  = "dual_quiet_bit{}".format(i)
        str_args = bench_exe_path
        for k, v in args.items():
            str_args += " {} {} ".format(k, v)



        if benchmarkBool:
            print("benchmark running with args\n  -> {}\n".format(str_args))
            status = subprocess.run(str_args, capture_output=True)
            status.check_returncode()

        if graphBool:
            try:
                status = subprocess.run(['python', 'plot_latencies.py',
                    args['--raw-prefix'] + '_hedged_quiet_ch0.csv',
                    args['--raw-prefix'] + '_hedged_quiet_ch1.csv',
                    args['--raw-prefix'] + '_hedged_quiet_ch2.csv',
                    args['--raw-prefix'] + '_hedged_quiet_ch3.csv',
                    args['--raw-prefix'] + '_hedged_quiet.csv'
                ])

            except Exception as e:
                print(f"Error processing Benchmark for {args["--raw-prefix"]}: {e}")



if __name__ == "__main__":
    # You can pass filenames as arguments or list them here:
    # Usage: python script.py data_ch1.csv data_ch2.csv hedged_results.csv
    run_bench = False
    run_graph = False

    if len(sys.argv) > 1:
        run_bench = (sys.argv[1] == "bench") or (sys.argv[1] == "both") 
        run_graph = (sys.argv[1] == "graph") or (sys.argv[1] == "both") 

    bench_exe_path = "../../build/bin/tailslayer_benchmark"
    bench_args = {
        "--arm"           : "dual_quiet",
        "--samples"       : "5000000",
        "--stress-threads": "2",
        "--channels"      : "4",
        "--channel-bit"   : "8",
        "--raw-prefix"    : "dual_quiet_bit"
    }
    channel_bit_off_begin = 6
    channel_bit_off_end   = 27


    run_benchmarks(
        bench_exe_path, 
        bench_args, 
        channel_bit_off_begin, 
        channel_bit_off_end,
        run_bench,
        run_graph
    )