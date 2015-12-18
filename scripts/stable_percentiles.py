import os
import sys
import numpy

def main():
    filename = sys.argv[1]
    file = open(filename, 'r')
    drop = int(sys.argv[2])
    latencies = []

    for line in file:
        latencies.append(float(line.strip()))
    file.close()

    stable_latencies = latencies[drop:]

    print numpy.percentile(latencies, 99), '\t', numpy.percentile(latencies, 99.9)
    print numpy.percentile(stable_latencies, 99), '\t', numpy.percentile(stable_latencies, 99.9)

if __name__ == "__main__":
    main()
