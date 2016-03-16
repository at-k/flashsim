import os
import sys
import numpy
import matplotlib.pyplot as plt

def main():
    percentiles = [50, 90, 99, 99.9]
    filename = sys.argv[1]
    out_filename = sys.argv[2]
    out_file = open(out_filename, "w")    

    latencies = []
    file = open(filename, "r")
    count = 0;
    start_time = -1
    end_time = -1
    for line in file:
        if line.startswith("="):
            break
        try:
            latency = float(line.split('\t')[1][:-1].strip())
            req_time = float(line.split('\t')[0].strip())
            latencies.append(latency)
            time = req_time + latency
            if req_time < start_time or start_time == -1:
                start_time = req_time
            if time > end_time or end_time == -1:
                end_time = time
            count += 1
        except:
            pass


    sorted_latencies = numpy.sort(latencies)
    yvals = numpy.arange(len(sorted_latencies))/float(len(sorted_latencies))

    for i in range(0, len(sorted_latencies)):
        out_file.write(str(sorted_latencies[i]) + '\t' + str.format("{0:.9f}", yvals[i]) + '\n')

    #plt.plot(sorted_latencies, yvals)
    #plt.show()

    print end_time, start_time
    print 'total duration ', (end_time - start_time)/1000000, ' seconds'

if __name__ == "__main__":
	main()
