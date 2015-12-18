import sys
import os
import numpy
import matplotlib.pyplot as plt

def main():

    filename_array = []


    for util in [90]:
        n_threads = 2
        while n_threads <= 2048:
            rw_filename = 'read_2_1_' + str(util) + '_' + str(n_threads) + '.out'
            rw_file = open(rw_filename, 'r')
           
            rw_data_file = open('moving_latencies_' + rw_filename + '.lat', 'w')

            rw_latencies = []
            for line in rw_file:
                rw_latencies.append(float(line.strip()))
            rw_file.close()

            rw_partial_list = []
            for latency in rw_latencies:
                rw_partial_list.append(latency)
                for p in [50, 99, 99.9]:
                    rw_data_file.write(str.format("{0:.2f}", numpy.percentile(rw_partial_list, p)/1000) + '\t')
                rw_data_file.write("\n")
        
            rw_data_file.close()
            n_threads = 2*n_threads
   
if __name__ == "__main__":
    main()
