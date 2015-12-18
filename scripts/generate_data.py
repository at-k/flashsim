import os
import numpy
import matplotlib.pyplot as plt

def main():
    for q_depth in [2, 4, 8, 16]:
			for i in range(0, 5):
				os.system('./test_queued 1 90 ' + str(q_depth))
				os.system('mv read_2_1_90_' + str(q_depth) + '.out read_2_1_90_' + str(q_depth) + '.out.' + str(i))
if __name__ == "__main__":
    main()
