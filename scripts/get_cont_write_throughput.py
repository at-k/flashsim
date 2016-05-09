import os
import sys
import numpy
import matplotlib.pyplot as plt

def main():
    filename = sys.argv[1]
    num_lines = -1
    try:
        num_lines = sys.argv[2]
    except:
        pass
    latencies = []
    tput_array = []
    file = open(filename, "r")
    count = 0;
    start_time = -1
    end_time = -1
    line_num = 0
    for line in file:
        if not num_lines == -1 and line_num > num_lines:
                break
        req_time = float(line.split('\t')[0].strip())
        time = float(line.split('\t')[2].strip())
        if req_time < start_time or start_time == -1:
            start_time = req_time
        if time > end_time or end_time == -1:
            end_time = time
        count += 1
        duration = (end_time - start_time)/1000000
        tput = count/duration
        tput_array.append(tput)
        line_num += 1

    for t in tput_array:
        print t


if __name__ == "__main__":
	main()
