import subprocess
import sys
import argparse


def run_benchmarks(
        bench_exe_path : str, 
        bench_args     : dict[str, str], 
        bit_begin      : int, 
        bit_end        : int,
        channel_begin  : int,
        channel_end    : int
    ):
    base_prefix = bench_args['--raw-prefix']
    

    bit_channel_permutations = []
    for bitoff in range(bit_begin, bit_end):
        for ch in range(channel_begin, channel_end):
            bit_channel_permutations.append([bitoff, ch])


    for i in range(bit_begin, bit_end):
        # Concatenate all K,V Pairs to a single string
        bench_args['--channel-bit'] = str(i)
        bench_args['--raw-prefix']  = base_prefix + "{}".format(i)

        str_args = bench_exe_path
        for k, v in bench_args.items():
            str_args += " {} {} ".format(k, v)


        try:
            print("  Running Benchmark\n  {}\n".format(str_args))
            status = subprocess.run(str_args, capture_output=True)
            print("  Finished\n")
            status.check_returncode()
        except Exception as e:
            print(f"Error Benchmarking with arguments:\n  {bench_args["--raw-prefix"]}\n  {e}\n")

        # if graphBool:
        #     try:
        #         csvFileList = [ bench_args['--raw-prefix'] + '_hedged_quiet_ch.csv' ]
        #         for ch in range(0, int(bench_args['--channels'])):
        #             csvFileList.append(
        #                 bench_args['--raw-prefix'] + '_hedged_quiet_ch{}.csv'.format(ch)
        #             )

        #         status = subprocess.run(['python', 'plot_latencies.py', csvFileList])

        #     except Exception as e:
        #         print(f"Error processing Benchmark for {bench_args["--raw-prefix"]}: {e}")



if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-choffb", '--channel-offset-begin',help="The Beginning Range for the Memory Offset",                   type=int)
    parser.add_argument("-choffe", '--channel-offset-end',  help="The Ending    Range for the Memory Offset",                   type=int)
    parser.add_argument("-chb",    '--channel-begin',       help="The Range of Memory Channels to Benchmark.",                  type=int)
    parser.add_argument("-che",    '--channel-end',         help="channel-begin < channel-end <= Count(RAM Modules Installed)", type=int)


    bench_exe_path = "../../build/bin/tailslayer_benchmark"
    bench_args = {}
    channel_bit_off_begin = 6
    channel_bit_off_end   = 27
    channel_count_begin = 1
    channel_count_end   = 2
    args = []
    try:
        args = parser.parse_args()
    except Exception as e:
        print(f"Error while processing command line arguments: {e}")


    # if args.both:
    #     run_bench = True
    #     run_graph = True
    # elif args.benchmark:
    #     run_bench = True
    # elif args.graph:
    #     run_graph = True

    # Maximum Offsets have been chosen arbitarily
    if args.channel_offset_begin:
        channel_bit_off_begin = min(max(args.channel_offset_begin, 0), 30)
    if args.channel_offset_end:
        channel_bit_off_end = min(max(args.channel_offset_end, 0), 62)

    if args.channel_begin:
        channel_count_begin = min(max(args.channel_begin, 1), 128)
    if args.channel_end:
        channel_count_end = min(max(args.channel_end, 2), 128)

    bench_args = {
        "--arm"           : "dual_quiet",
        "--samples"       : "5000000",
        "--stress-threads": "4",
        "--channels"      : str(channel_count_begin),
        "--channel-bit"   : str(channel_bit_off_begin),
        "--raw-prefix"    : "dual_quiet_bit"
    }


    run_benchmarks(
        bench_exe_path, 
        bench_args, 
        channel_bit_off_begin, 
        channel_bit_off_end,
        channel_count_begin,
        channel_count_end
    )