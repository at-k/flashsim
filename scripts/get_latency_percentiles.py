import os
import sys
import numpy
import matplotlib.pyplot as plt

def main():
    percentiles = [50, 90, 99, 99.9]
    filename = sys.argv[1]
    try:
        req_percentile = float(sys.argv[2])
        percentiles = [req_percentile]
    except:
        pass
    latencies = []
    file = open(filename, "r")
    for line in file:
            try:
                    latencies.append(float(line.split('\t')[-1][:-1].strip()))
            except:
                    pass

    for p in percentiles:
        print p, numpy.percentile(latencies, p)


if __name__ == "__main__":
	main()
