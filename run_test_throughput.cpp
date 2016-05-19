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
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <ctime>
#include <set>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <vector>
#include <limits.h>

using namespace ssd;

enum op_type{OP_READ, OP_WRITE};


int main(int argc, char **argv)
{
	load_config();
	print_config(NULL);
	printf("\n");

	unsigned int total_writes = 10000000;
	Ssd *ssd = new Ssd();
	srand(time(NULL));

	unsigned int lastLBA = NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE;
	bool *op_complete = (bool *) malloc(total_writes * sizeof(bool));
	if(!op_complete)
		printf("op complete failed\n");
	double *start_time = (double *)malloc(total_writes * sizeof(double));
	if(!start_time)
		printf("start time failed\n");
	double *end_time = (double *)malloc(total_writes * sizeof(double));
	if(!end_time)
		printf("end time failed\n");
	
	double time = 0;
	for(unsigned int i=0;i<total_writes;i++)
	{
		op_complete[i] = false;
		start_time[i] = 0;
		end_time[i] = 0;
	}

	unsigned int location;
	for(unsigned int i=0;i<total_writes;i++)
	{
			
			location = rand()%lastLBA;
			printf("[App] writing %d\n", location);
			bool result = ssd->event_arrive(WRITE, location, 1, start_time[i], op_complete[i], end_time[i]);
			if(!result)
			{
				printf("write %d failed\n", i);
				break;
			}
			
	}

	double prev_noop_time = time;
	bool noop_complete = false;
	while(prev_noop_time < std::numeric_limits<double>::max())
	{
		ssd->event_arrive(NOOP, 0, 1, prev_noop_time, noop_complete, prev_noop_time);
		printf("next noop at %f\n", prev_noop_time);
	}
	

	FILE *write_file = fopen("write_throughput.out", "w");

	for(unsigned int i=0;i<total_writes;i++)
	{
		if(!op_complete[i])		
		{
			printf("Event %d not complete\n", i);
			continue;
		}
		fprintf(write_file, "%.5lf\t%.5lf\t%.5lf\n", start_time[i], end_time[i] - start_time[i], end_time[i]);
	}

	ssd->print_ftl_statistics(stdout);
	fclose(write_file);
	delete ssd;
	return 0;
}
