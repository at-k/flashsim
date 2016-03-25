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
	char read_file_name[100] = "";
	char write_file_name[100] = "";
	std::set<unsigned int> addresses;
	FILE *write_file;
	double initial_delay;
	//unsigned int req_per_thread = 1000;
	
	unsigned int total_read_count = 100000, cur_read_count = 0;


	load_config();
	print_config(NULL);
	printf("\n");

	Ssd *ssd = new Ssd();
	srand(time(NULL));

	unsigned int util_percent = atoi(argv[1]);
	unsigned int num_rounds = 100;
	unsigned int burst_write_gap = 100;
	unsigned int non_burst_write_gap = 10000;
	unsigned int burst_writes_per_round = 1000;
	unsigned int non_burst_writes_per_round = 10;

	char ftl_implementation[10] = {'0' + FTL_IMPLEMENTATION};
	char gc_scheme[10] = {'0' + GC_SCHEME};


	printf("addressable blocks %d\n", NUMBER_OF_ADDRESSABLE_BLOCKS);
	unsigned int lastLBA = NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE;



	strcat(write_file_name, "_burst_write_");
	strcat(write_file_name, ftl_implementation);
	strcat(write_file_name, "_");
	strcat(write_file_name, gc_scheme);
	strcat(write_file_name, ".out");


	write_file = fopen(write_file_name, "w");

	bool noop_complete = false;
	double next_noop_time = 0;
	double prev_noop_time = 0;

	bool write_complete = false;
	double write_end_time = 0;

	unsigned int occupied = util_percent*lastLBA/100;
	unsigned int i=0;
	unsigned int location = 0;
	/*
	for (i = 0; i < occupied; i++)
	{
		write_complete = false;
		location = rand()%lastLBA;
		//location = i%lastLBA;
		bool result = ssd -> event_arrive(WRITE, location, 1, write_end_time, write_complete, write_end_time);
		if(result == false)
		{
			printf("returning failure\n");
			return -1;
		}
		//printf("Write %d %f\n", write_complete, write_end_time);
		prev_noop_time = write_end_time;
		int k = 0;
		while(!write_complete)
		{
			ssd->event_arrive(NOOP, 1, 1, prev_noop_time, noop_complete, next_noop_time);
			prev_noop_time = next_noop_time;
			k++;
		}
		addresses.insert(location);
	}
	*/
	initial_delay = write_end_time;
	printf("Completed\n");
	fflush(stdout);

	printf("starting experiment\n");
	fflush(stdout);

	unsigned int total_writes = num_rounds * (burst_writes_per_round + non_burst_writes_per_round);

	printf("total_writes %d\n", total_writes);
	bool *op_complete = (bool *) malloc(total_writes * sizeof(bool));
	if(!op_complete)
		printf("op complete failed\n");
	double *start_time = (double *)malloc(total_writes * sizeof(double));
	if(!start_time)
		printf("start time failed\n");
	double *end_time = (double *)malloc(total_writes * sizeof(double));
	if(!end_time)
		printf("end time failed\n");
	
	double time = initial_delay;
	for(unsigned int i=0;i<total_writes;i++)
	{
		op_complete[i] = false;
		start_time[i] = time;
		end_time[i] = time;
	}

	unsigned int cur_write_num = 0;
	//location = 0;
	for(unsigned int i=0;i<num_rounds;i++)
	{
		for(unsigned int j=0;j<burst_writes_per_round;j++)
		{
			
			location = rand()%lastLBA;
			time = time + burst_write_gap;
			start_time[cur_write_num] = time;
			bool result = ssd->event_arrive(WRITE, location, 1, start_time[cur_write_num], op_complete[cur_write_num], end_time[cur_write_num]);
			if(!result)
			{
				printf("write %d failed\n", cur_write_num);
				break;
			}
			
			cur_write_num++;
		}
		for(unsigned int j=0;j<non_burst_writes_per_round;j++)
		{
			location = rand()%lastLBA;
			time = time + non_burst_write_gap;
			start_time[cur_write_num] = time;
			bool result = ssd->event_arrive(WRITE, location, 1, start_time[cur_write_num], op_complete[cur_write_num], end_time[cur_write_num]);
			if(!result)
			{
				printf("write %d failed\n", cur_write_num);
				break;
			}
			cur_write_num++;
		}
	}


	
	prev_noop_time = time;
	while(prev_noop_time < std::numeric_limits<double>::max())
	{
		ssd->event_arrive(NOOP, 0, 1, prev_noop_time, noop_complete, prev_noop_time);
		printf("[App] next noop at %f\n", prev_noop_time);
	}
	


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
