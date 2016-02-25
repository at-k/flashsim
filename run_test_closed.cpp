
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
	
	unsigned int total_read_count = 500000, cur_read_count = 0;


	load_config();
	print_config(NULL);
	printf("\n");

	Ssd *ssd = new Ssd();
	srand(time(NULL));

	unsigned int write = atoi(argv[1]);
	unsigned int util_percent = atoi(argv[2]);
	q_depth = atoi(argv[3]);

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
	strcat(read_file_name, "_.out");

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
	strcat(write_file_name, "_.out");


	read_file = fopen(read_file_name, "w");
	write_file = fopen(write_file_name, "w");

	bool noop_complete = false;
	double next_noop_time = 0;

	bool write_complete = false;
	double write_end_time = 0;

	unsigned int occupied = util_percent*lastLBA/100;
	unsigned int i=0;
	for (i = 0; i < occupied; i++)
	{
		bool result = ssd -> event_arrive(WRITE, i%lastLBA, 1, write_end_time, write_complete, write_end_time);
		if(result == false)
		{
			printf("returning failure\n");
			return -1;
		}
		do
		{
			ssd->event_arrive(NOOP, 0, 1, next_noop_time, noop_complete, next_noop_time);
		}
		while(!write_complete);
		addresses.insert(i);
	}
	initial_delay = write_end_time;

	if(write == 0)
	{
		write_data = false;
	}
	else
	{
		write_data = true;
		q_depth = 2*q_depth;
	}

	printf("starting experiment\n");

	unsigned int count[q_depth];
	bool op_complete[q_depth];
	double op_start_time[q_depth];
	double op_complete_time[q_depth];
	for (unsigned int i=0;i<q_depth;i++)
	{
		count[i] = 0;
		op_complete[i] = false;
		op_start_time[i] = initial_delay;
	}	
	next_noop_time = initial_delay;
	unsigned int location = 0;
	unsigned int write_count = 0;
	bool loop = true;
	for(unsigned int i=0;i<q_depth;i++)
	{
		bool result;
		if(write_data && i >= q_depth/2)
		{
			location = rand()%lastLBA;
			result = ssd->event_arrive(WRITE, location, 1, (double) op_start_time[i], op_complete[i], op_complete_time[i]);
			if(result == false)
			{
				fprintf(read_file, "==========\nCould not do a write, incomplete experiment\n");
				goto exit;
			}
			addresses.insert(location);
			write_count++;
			fprintf(write_file, "%.5lf\t%.5lf\n", initial_delay, result);
		}	
		else
		{
			
			location = rand()%lastLBA;
			while(addresses.find(location) == addresses.end())
			{
				location = rand()%lastLBA;
			}
			
			result = ssd->event_arrive(READ, location, 1, (double) op_start_time[i], op_complete[i], op_complete_time[i]);
			if(result == false)
			{
				fprintf(read_file, "==========\nCould not do a read, incomplete experiment\n");
				goto exit;
			}
			cur_read_count++;
			fprintf(read_file, "%.5lf\t%.5lf\n", initial_delay, result);
		}
		count[i]++;
	}
	

	while(loop)
	{
		bool event_completed = false;
		while(!event_completed)
		{
			ssd->event_arrive(NOOP, 0, 1, next_noop_time, noop_complete, next_noop_time);
			for(unsigned int i=0;i<q_depth;i++)
			{
				if(op_complete[i])
				{
					event_completed = true;
					break;
				}
			}
		}
		for(unsigned int i=0;i<q_depth;i++)
		{
			if(op_complete[i])
			{
				bool result = false;
				op_complete[i] = false;
				if(write_data && i >= q_depth/2)
				{
					count[i]++;
					fprintf(write_file, "%.5lf\t%.5lf\n", op_start_time[i], op_complete_time[i] - op_start_time[i]);
					write_count++;
					op_start_time[i] = op_complete_time[i];
					location = rand()%lastLBA;
					addresses.insert(location);
					result = ssd->event_arrive(WRITE, location, 1, (double) op_start_time[i], op_complete[i], op_complete_time[i]);
					if(result == false)
					{
						fprintf(read_file, "==========\nCould not do a write, incomplete experiment\n");
						goto exit;
					}
				}	
				else
				{
					fprintf(read_file, "%.5lf\t%.5lf\n", op_start_time[i], op_complete_time[i] - op_start_time[i]);
					count[i]++;
					op_start_time[i] = op_complete_time[i];
					location = rand()%lastLBA;
					while(addresses.find(location) == addresses.end())
					{
						location = rand()%lastLBA;
					}
					cur_read_count++;	
					result = ssd->event_arrive(READ, location, 1, (double) op_start_time[i], op_complete[i], op_complete_time[i]);
					if(result == false)	
					{
						fprintf(read_file, "==========\nCould not do a read, incomplete experiment\n");
						goto exit;
					}
				}
				
			}
		}
		
		if(cur_read_count >= total_read_count)
		{
			loop = false;
			break;
		}
		
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
