import sys
import matplotlib.pyplot as plt

def main():
    num_files = int(sys.argv[1])
    for i in range(0, num_files):
        fname = sys.argv[2+i]
        f = open(fname, "r")
        xvals = []
        yvals = []
        for line in f:
            line_parts = line.split('\t')
            xvals.append(float(line_parts[0].strip()))
            yvals.append(float(line_parts[1].strip()))

        plt.plot(xvals, yvals, label=fname)
    
    #plt.xlim((0, 20000))
    plt.ylim((0, 1))
    plt.legend(loc = "lower right")
    #plt.show()
    plt.savefig(sys.argv[2+num_files]+'.png')
           


if __name__ == "__main__":
    main()
