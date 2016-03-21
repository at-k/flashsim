import os
import sys
import numpy
import matplotlib.pyplot as plt

def main():
	fname = sys.argv[1]
	f = open(fname, "r")
	read_delay = False
	erase_wait_latencies = []
	line_num = []
	write_hit_count = 0
	write_hit_array = []
	overall_write_hit_array = []
	count = 0
	for line in f:

		count += 1
		if not read_delay and line.startswith("Type"):
			read_delay = True
			write_hit_count += 1
			continue
		if read_delay and line.startswith("Type"):
			write_hit_count += 1
		if read_delay and line.startswith("Total plane"):
			latency = float(line.split(' ')[-1].strip())
			erase_wait_latencies.append(latency)
			write_hit_array.append(write_hit_count)
			if latency > 300:
				print latency, count
			overall_write_hit_array.append(write_hit_count)
			write_hit_count = 0
			read_delay = False
			line_num.append(count)


	print len(erase_wait_latencies)
	if len(erase_wait_latencies) > 0:

		#sorted_erase_wait_latencies = sorted(erase_wait_latencies)
		#for i in range(0, len(sorted_erase_wait_latencies)):
		#	print sorted_erase_wait_latencies[i]

		#yvals = numpy.arange(len(sorted_erase_wait_latencies))/float(len(sorted_erase_wait_latencies))

		#plt.plot(sorted_erase_wait_latencies, yvals)
		#plt.savefig('write_hit_latencies.png')
		#plt.close()

		print min(write_hit_array), max(write_hit_array)
		print min(overall_write_hit_array), max(overall_write_hit_array)
		for i in range(min(write_hit_array), max(write_hit_array) + 1):
			#if i > 36:
				#	break
				#print i, float(write_hit_array.count(i))/float(len(write_hit_array))
				print i, write_hit_array.count(i)
		print sum(write_hit_array)

		#print min(erase_wait_latencies), max(erase_wait_latencies)
		#print line_num[erase_wait_latencies.index+(min(erase_wait_latencies)]




if __name__ == "__main__":
	main()
