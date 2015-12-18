import os
import sys
import numpy
import matplotlib.pyplot as plt

def main():
	filename = sys.argv[1]
	latencies = []
	file = open(filename, "r")
	for line in file:
		try:
			latencies.append(float(line.split('\t')[-1][:-1].strip()))
		except:
			pass

	print numpy.percentile(latencies, 50)
	print numpy.percentile(latencies, 87)
	print numpy.percentile(latencies, 88)
	print numpy.percentile(latencies, 89)
	print numpy.percentile(latencies, 99.9)
	print numpy.percentile(latencies, 100)

	#x = []
	#y = []
	#for p in range(0, 100):
	#	y.append(p)
	#	x.append(numpy.percentile(latencies, p))

	#plt.plot(x,y)
	#plt.ylim((80, 100))
	#plt.show()


if __name__ == "__main__":
	main()
