import os
import sys
import numpy
import matplotlib.pyplot as plt

def main():
    filename = sys.argv[1]
    latencies = []
    max_latency = -1
    filename_parts = filename.split('.')
    file = open(filename, "r")
    for line in file:
        try:
            cur_latency = float(line.strip())
            #This is done so as to combine all latencies higher than 2000 into a single slot
            #Otherwise, each latency value has a very small frequency and inferring the total 
            #frequency of latencies higher than 2000 from the plot becomes infeasible
            if cur_latency > 2000:
                    cur_latency = 2001
            latencies.append(cur_latency)
            if(latencies[-1] > max_latency):
                    max_latency = latencies[-1]
            if(lantecies[-1] < 0):
                    print 'ERROR'
        except:
            pass

    print numpy.percentile(latencies, 50)
    print numpy.percentile(latencies, 80)
    print numpy.percentile(latencies, 99)
    print numpy.percentile(latencies, 99.9)

    #x = []
    #y = []
    #for p in range(0, 100):
    #	y.append(p)
    #	x.append(numpy.percentile(latencies, p))

    #plt.plot(x,y)
    ##plt.ylim((80, 100))
    #plt.show()


    (n, bins, patches) = plt.hist(latencies, bins=100, normed=True)
    plt.xlabel('read latency (microseconds)')
    plt.ylabel('PDF')
    plt.savefig(filename_parts[0] + '.png')
    ##plt.xlim((0,5000))
    #print n, numpy.sum(n)
    #print bins


if __name__ == "__main__":
	main()
