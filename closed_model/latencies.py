import sys
import os
import numpy
import matplotlib.pyplot as plt

def main():

    filename_array = []


    for util in [90]:
        data_file = open('data_file_' + str(util) + '.dat', 'w')
        for n_threads in [2, 4, 8, 16, 32, 64]:
            ro_file = open('read_2_0_' + str(util) + '_' + str(n_threads) + '.out', 'r')
            rw_file = open('read_2_1_' + str(util) + '_' + str(n_threads) + '.out', 'r')
           
            ro_latencies = []
            for line in ro_file:
                ro_latencies.append(float(line.strip()))

            rw_latencies = []
            for line in rw_file:
                rw_latencies.append(float(line.strip()))

            data_file.write(str(2*n_threads) + '\t')
            for p in [50, 90, 99, 99.9]:
                data_file.write(str.format("{0:.2f}", numpy.percentile(ro_latencies, p)/1000) + '\t' + str.format("{0:.2f}", numpy.percentile(rw_latencies, p)/1000) + '\t')
            
            data_file.write("\n")

        

#    for util in [90]:
#        data_file = open('data_file_open_' + str(util) + '.dat', 'w')
#        for time_gap in [1000000, 100000, 10000, 1000, 100, 1]:
#            ro_file = open('read_2_' + str(0) + '_' + str(time_gap) + '_' + str(util) + '.out', 'r')
#            rw_file = open('read_2_' + str(time_gap) + '_' + str(time_gap) + '_' + str(util) + '.out', 'r')
#           
#            ro_latencies = []
#            for line in ro_file:
#                ro_latencies.append(float(line.strip()))
#
#            rw_latencies = []
#            for line in rw_file:
#                rw_latencies.append(float(line.strip()))
#
#
#            data_file.write(str(1000000/time_gap) + '\t')
#            for p in [50, 90, 99, 99.9]:
#                data_file.write(str.format("{0:.2f}", numpy.percentile(ro_latencies, p)/1000) + '\t' + str.format("{0:.2f}", numpy.percentile(rw_latencies, p)/1000) + '\t')
#            
#            data_file.write("\n")
   
if __name__ == "__main__":
    main()