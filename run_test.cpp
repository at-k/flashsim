/* Copyright 2009, 2010 Brendan Tauras */

/* run_test.cpp is part of FlashSim. */

/* FlashSim is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version. */

/* FlashSim is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. */

/* You should have received a copy of the GNU General Public License
 * along with FlashSim.  If not, see <http://www.gnu.org/licenses/>. */

/****************************************************************************/

/* Basic test driver
 * Brendan Tauras 2009-11-02
 *
 * driver to create and run a very basic test of writes then reads */

#include "ssd.h"
#include <time.h>
#include <stdlib.h>

#define SIZE 130

using namespace ssd;

int main()
{
	load_config();
	print_config(NULL);
	Ssd *ssd = new Ssd();

	double result;
	srand(time(NULL));
	
	FILE *log_file = fopen("./test_output.log", "w");

	int LAST_LBA = NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE;

	int NUM_PAGES = 0.9 * LAST_LBA;
	int i;
	for (i = 0; i < NUM_PAGES; i++)
	{
		/* event_arrive(event_type, logical_address, size, start_time) */
		result = ssd -> event_arrive(WRITE, i%LAST_LBA, 1, (double) (350 * i));
	}

	printf("Experiment Starting\n");
	double initial_delay = i * 350;
	double experiment_start_time = initial_delay;
	double experiment_end_time = -1;
	for (i = 0; i < 100000 + 6000; i++)
	{
		unsigned int add = rand()%LAST_LBA;
		double read_time = initial_delay + (i * 100);
		result = ssd -> event_arrive(READ, add, 1, read_time);
		if(read_time + result > experiment_end_time)
		{
			experiment_end_time = read_time + result;
		}
		fprintf(log_file, "%.5lf\n", result);
		if(i%5 == 0 && i < 100000)
		{
			add = rand()%LAST_LBA;
			double write_time = initial_delay + (i*100);
			result = ssd -> event_arrive(WRITE, add, 1, write_time);
			if(write_time + result > experiment_end_time)
				experiment_end_time = write_time + result;
		}
	}

	//fprintf(log_file, "%.5f %.5f\n", experiment_start_time, experiment_end_time);

	delete ssd;
	return 0;
}
