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
	FILE *read_file;
	FILE *write_file;
	double initial_delay;
	unsigned int q_depth;
	bool write_data;
	//unsigned int req_per_thread = 1000;
	
	unsigned int total_read_count = 100000, cur_read_count = 0;


	load_config();
	print_config(NULL);
	printf("\n");

	Ssd *ssd = new Ssd();
	srand(time(NULL));

	unsigned int write = atoi(argv[1]);
	unsigned int util_percent = atoi(argv[2]);
	q_depth = atoi(argv[3]);
	//total_read_count = q_depth * 10000;

	char ftl_implementation[10] = {'0' + FTL_IMPLEMENTATION};
	char gc_scheme[10] = {'0' + GC_SCHEME};


	printf("addressable blocks %d\n", NUMBER_OF_ADDRESSABLE_BLOCKS);
	unsigned int lastLBA = NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE;


	strcat(read_file_name, "closed_read_");
	strcat(read_file_name, ftl_implementation);
	strcat(read_file_name, "_");
	strcat(read_file_name, gc_scheme);
	strcat(read_file_name, "_");
	strcat(read_file_name, argv[1]);
	strcat(read_file_name, "_");
	strcat(read_file_name, argv[2]);
	strcat(read_file_name, "_");
	strcat(read_file_name, argv[3]);
	strcat(read_file_name, ".out");

	strcat(write_file_name, "closed_write_");
	strcat(write_file_name, ftl_implementation);
	strcat(write_file_name, "_");
	strcat(write_file_name, gc_scheme);
	strcat(write_file_name, "_");
	strcat(write_file_name, argv[1]);
	strcat(write_file_name, "_");
	strcat(write_file_name, argv[2]);
	strcat(write_file_name, "_");
	strcat(write_file_name, argv[3]);
	strcat(write_file_name, ".out");


	read_file = fopen(read_file_name, "w");
	write_file = fopen(write_file_name, "w");

	bool noop_complete = false;
	double next_noop_time = 0;
	double prev_noop_time = 0;

	bool write_complete = false;
	double write_end_time = 0;

	unsigned int occupied = util_percent*lastLBA/100;
	unsigned int i=0;
	unsigned int location = 0;
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
		printf("Write %d %f\n", write_complete, write_end_time);
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
	initial_delay = write_end_time;
	printf("Completed\n");
	fflush(stdout);
	if(write == 0)
	{
		write_data = false;
	}
	else
	{
		write_data = true;
//		q_depth = 2*q_depth;
	}

	printf("starting experiment\n");
	fflush(stdout);
	unsigned int count[q_depth];
	bool op_complete[q_depth];
	double op_start_time[q_depth];
	double op_complete_time[q_depth];
	enum op_type op_rw_type[q_depth]; 
	unsigned int op_addresses[q_depth];
	for (unsigned int i=0;i<q_depth;i++)
	{
		count[i] = 0;
		op_complete[i] = false;
		op_start_time[i] = initial_delay;
		op_addresses[i] = 0;
		op_rw_type[i] = OP_READ;
	}	
	next_noop_time = initial_delay;
	unsigned int write_count = 0;
	bool loop = true;
	for(unsigned int i=0;i<q_depth;i++)
	{
		bool result;
		bool read_write = rand()%2;
		if(write_data && read_write == 1)
		{
			location = rand()%lastLBA;
			result = ssd->event_arrive(WRITE, location, 1, (double) op_start_time[i], op_complete[i], op_complete_time[i]);
			if(result == false)
			{
				fprintf(read_file, "==========\nCould not do a write, incomplete experiment\n");
				goto exit;
			}
			op_addresses[i] = location;
			op_rw_type[i] = OP_WRITE;
			//addresses.insert(location);
		}	
		else
		{
			
			location = rand()%lastLBA;
			while(addresses.find(location) == addresses.end())
			{
				location = rand()%lastLBA;
			}
			result = ssd->event_arrive(READ, location, 1, (double) op_start_time[i], op_complete[i], op_complete_time[i]);
			if(result == -1)
			{
				fprintf(read_file, "==========\nCould not do a read, incomplete experiment\n");
				goto exit;
			}
			op_addresses[i] = location;
			op_rw_type[i] = OP_READ;
		}
	}
	
	prev_noop_time = initial_delay;
	while(loop)
	{
		bool event_completed = false;
		unsigned int loop_c = 0;
		double earliest_event = -1;
		unsigned int earliest_event_index = 0;
		for(unsigned int i=0;i<q_depth;i++)
		{
			//printf("%d %p %d %f %f\n", i, &op_complete[i], op_complete[i], op_complete_time[i], earliest_event);
			if(op_complete[i])
			{
				event_completed = true;
				if(op_complete_time[i] < earliest_event || earliest_event == -1)
				{
					earliest_event = op_complete_time[i];
					earliest_event_index = i;
				}
			}
		}
		if(event_completed)
			prev_noop_time = earliest_event < prev_noop_time ? earliest_event : prev_noop_time;
		int infinite_loop_check = 0;
		while(!event_completed)
		{
			ssd->event_arrive(NOOP, 1, 1, prev_noop_time, noop_complete, next_noop_time);
			prev_noop_time = next_noop_time;
			for(unsigned int i=0;i<q_depth;i++)
			{
				if(op_complete[i])
				{
					event_completed = true;
					if(op_complete_time[i] < earliest_event || earliest_event == -1)
					{
						earliest_event = op_complete_time[i];
						earliest_event_index = i;
					}
				}
			}
			infinite_loop_check++;
			if(event_completed)
				prev_noop_time = earliest_event < prev_noop_time ? earliest_event : prev_noop_time;
			else
				prev_noop_time = next_noop_time;
			loop_c++;
			//printf("%d %f %f\n", loop_c, prev_noop_time, next_noop_time);
		}
				
		if(op_rw_type[earliest_event_index] == OP_READ)
		{
			fprintf(read_file, "%.5lf\t%.5lf\t%.5lf\t%d\n", op_start_time[earliest_event_index], 
					op_complete_time[earliest_event_index] - op_start_time[earliest_event_index], 
					op_complete_time[earliest_event_index], op_addresses[earliest_event_index]);
			cur_read_count++;
			//printf("[Application] latency for %d was %f\n", op_addresses[earliest_event_index], 
			//		op_complete_time[earliest_event_index] - op_start_time[earliest_event_index]);
		}
		else
		{
			fprintf(write_file, "%.5lf\t%.5lf\t%.5lf\t%d\n", op_start_time[earliest_event_index], 
					op_complete_time[earliest_event_index] - op_start_time[earliest_event_index], 
					op_complete_time[earliest_event_index], op_addresses[earliest_event_index]);
			write_count++;
			addresses.insert(op_addresses[earliest_event_index]);
		}
		prev_noop_time = op_complete_time[earliest_event_index];
		count[earliest_event_index]++;
		bool result = false;
		op_complete[earliest_event_index] = false;
		op_start_time[earliest_event_index] = op_complete_time[earliest_event_index];
		if(op_start_time[earliest_event_index] == std::numeric_limits<double>::max())
		{
			//printf("Got a max response at %d which was %d for %d\n", earliest_event_index, op_rw_type[earliest_event_index], op_addresses[earliest_event_index]);
			assert(false);
			break;
		}
		bool read_write = rand()%2;
		if(write_data && read_write == 1)
		{
			location = rand()%lastLBA;
			op_addresses[earliest_event_index] = location;
			//printf("[Application] sending a write for %d at time %f\n", op_addresses[earliest_event_index], 
			//		op_start_time[earliest_event_index]);
			result = ssd->event_arrive(WRITE, location, 1, (double) op_start_time[earliest_event_index], 
					op_complete[earliest_event_index], op_complete_time[earliest_event_index]);
			op_rw_type[earliest_event_index] = OP_WRITE;
			//addresses.insert(location);
		}	
		else
		{
			location = rand()%lastLBA;
			while(addresses.find(location) == addresses.end())
			{
				location = rand()%lastLBA;
			}
			op_addresses[earliest_event_index] = location;
			//printf("[Application] sending a read for %d at time %f\n", op_addresses[earliest_event_index], 
			//		op_start_time[earliest_event_index]);
			result = ssd->event_arrive(READ, location, 1, (double) op_start_time[earliest_event_index], 
					op_complete[earliest_event_index], op_complete_time[earliest_event_index]);
			op_rw_type[earliest_event_index] = OP_READ;
		}
		if(result == false)
		{
			fprintf(read_file, "==========\nCould not operate\n");
			goto exit;
		}
		
		if(cur_read_count >= total_read_count)
		{
			loop = false;
			break;
		}
		
	}
	while(prev_noop_time < std::numeric_limits<double>::max())
	{
		ssd->event_arrive(NOOP, 0, 1, prev_noop_time, noop_complete, prev_noop_time);
		printf("[App] next noop at %f\n", prev_noop_time);
	}
exit:
	fprintf(stdout, "========================\n");
	fprintf(stdout, "experiment ended with write_count as %d\n", write_count);
	ssd->print_ftl_statistics(stdout);
	fclose(read_file);
	fclose(write_file);
	delete ssd;
	return 0;
}
