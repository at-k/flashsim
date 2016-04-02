import os
import sys

def main():
    filename = sys.argv[1]
    trace_file = open(filename, 'r')
    lba_min = -1
    lba_max = -1
    for line in trace_file:
        line_parts = line.split('\t')
        if not int(line_parts[3].strip()) == 0:
            print 'problem'
        lba = int(line_parts[2].strip())
        if lba > lba_max or lba_max == -1:
            lba_max = lba
        if lba < lba_min or lba_min == -1:
            lba_min = lba
    trace_file.close()
    print lba_min, lba_max

if __name__ == "__main__":
    main()
