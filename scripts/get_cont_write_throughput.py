import os
import sys
import numpy
import matplotlib.pyplot as plt

def main():
	filename = sys.argv[1]

	latencies = []
	tput_array = []
	file = open(filename, "r")
	count = 0;
	start_time = -1
	end_time = -1
	for line in file:
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

	for t in tput_array:
		print t


if __name__ == "__main__":
	main()
