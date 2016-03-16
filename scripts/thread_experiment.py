import os
import numpy

def main():
    thread_vals = [32] #, 64, 16, 128, 8, 4, 2]
    for thread_val in thread_vals:
        for rep in range(0, 5):
            org_config_file = open('ssd.conf.org', 'r')
            new_config_file = open('ssd.conf', 'w')
            for line in org_config_file:
            	if line.startswith('GC_SCHEME'):
            		new_config_file.write('GC_SCHEME 0\n')
            	else:
            		new_config_file.write(line)
            org_config_file.close()
            new_config_file.flush()
            new_config_file.close()

            os.system('./test_random 1 100 ' + str(thread_val))
            os.system('mv closed_read_0_0_1_100_' + str(thread_val) + '.out closed_read_0_0_1_100_' + str(thread_val) + '.out.' + str(rep))
            os.system('mv closed_write_0_0_1_100_' + str(thread_val) + '.out closed_write_0_0_1_100_' + str(thread_val) + '.out.' + str(rep))


            org_config_file = open('ssd.conf.org', 'r')
            new_config_file = open('ssd.conf', 'w')
            for line in org_config_file:
            	if line.startswith('GC_SCHEME'):
            		new_config_file.write('GC_SCHEME 1\n')
            	else:
            		new_config_file.write(line)
            org_config_file.close()
            new_config_file.flush()
            new_config_file.close()

            os.system('./test_random 1 100 ' + str(thread_val))
            os.system('mv closed_read_0_1_1_100_' + str(thread_val) + '.out closed_read_0_1_1_100_' + str(thread_val) + '.out.' + str(rep))
            os.system('mv closed_write_0_1_1_100_' + str(thread_val) + '.out closed_write_0_1_1_100_' + str(thread_val) + '.out.' + str(rep))

if __name__ == '__main__':
	main()
