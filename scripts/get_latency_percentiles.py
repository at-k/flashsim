import os
import sys
import numpy
import matplotlib.pyplot as plt

def main():
    percentiles = [50, 90, 99, 99.9, 100]
    filename = sys.argv[1]
    num_lines = int(sys.argv[2])
        
    try:
        req_percentile = float(sys.argv[3])
        percentiles = [req_percentile]
    except:
        pass
    latencies = []
    file = open(filename, "r")
    count = 0;
    start_time = -1
    end_time = -1
    for line in file:
        if line.startswith("="):
            break
        if num_lines != -1 and count >= num_lines:
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

    for p in percentiles:
        print p, numpy.percentile(latencies, p)

    print end_time, start_time
    print 'total duration ', (end_time - start_time)/1000000, ' seconds'

if __name__ == "__main__":
	main()
