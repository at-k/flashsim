
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
	unsigned int req_per_thread = 1000;
	
	unsigned int total_read_count = 1000000, cur_read_count = 0;

	int read_loc = 0;
	int write_loc = 0;

	load_config();
	print_config(NULL);
	printf("\n");

	Ssd *ssd = new Ssd();
	srand(time(NULL));

	unsigned int write = atoi(argv[1]);
	unsigned int util_percent = atoi(argv[2]);
	q_depth = atoi(argv[3]);

	char ftl_implementation[10] = {'0' + FTL_IMPLEMENTATION};


	printf("addressable blocks %d\n", NUMBER_OF_ADDRESSABLE_BLOCKS);
	unsigned int lastLBA = NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE;


	strcat(read_file_name, "closed_read_");
	strcat(read_file_name, ftl_implementation);
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
	strcat(write_file_name, argv[1]);
	strcat(write_file_name, "_");
	strcat(write_file_name, argv[2]);
	strcat(write_file_name, "_");
	strcat(write_file_name, argv[3]);
	strcat(write_file_name, ".out");

	read_file = fopen(read_file_name, "w");
	write_file = fopen(write_file_name, "w");

	unsigned int occupied = util_percent*lastLBA/100;
	unsigned int i=0;
	for (i = 0; i < occupied; i++)
	{
		double result = ssd -> event_arrive(WRITE, i%lastLBA, 1, (double) i*1000);
		if(result == -1)
			return -1;
		addresses.insert(i);
	}
	initial_delay = i*1000;

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

	std::vector<double> response_times;
	unsigned int count[q_depth];
	for (unsigned int i=0;i<q_depth;i++)
	{
		count[i] = 0;
	}	
	unsigned int location = 0;
	unsigned int write_count = 0;
	bool loop = true;
	for(unsigned int i=0;i<q_depth;i++)
	{
		double result;
		if(write_data && i >= q_depth/2)
		{
			location = rand()%lastLBA;
			//location = (write_loc+1)%lastLBA;	
			result = ssd->event_arrive(WRITE, location, 1, (double) initial_delay);
			if(result == -1)
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
			
			result = ssd->event_arrive(READ, location, 1, (double) initial_delay);
			if(result == -1)
			{
				fprintf(read_file, "==========\nCould not do a read, incomplete experiment\n");
				goto exit;
			}
			cur_read_count++;
			fprintf(read_file, "%.5lf\t%.5lf\n", initial_delay, result);
		}
		response_times.push_back(initial_delay + result);
		count[i]++;
	}
	/*
	bool loop = false;
	for(unsigned int i=0;i<q_depth;i++)
	{
		if(count[i] < req_per_thread)
		{
			loop = true;
			break;
		}
	}
	*/

	while(loop)
	{
		std::vector<double>::iterator iter, min_val_reference = response_times.end();
		double min_val = -1;
		for(iter=response_times.begin();iter!=response_times.end();iter++)
		{
			//if((count[iter-response_times.begin()] < req_per_thread) && ((*iter) < min_val || min_val == -1))
			if((*iter) < min_val || min_val == -1)
			{
				min_val = *iter;
				min_val_reference = iter;
			}
		}
		if(min_val_reference == response_times.end())
		{
			printf("problem\n");
		}
		response_times.erase(min_val_reference);
		double next_request_time = min_val;
		double result;
		unsigned int position = min_val_reference - response_times.begin();
		if(write_data && position >= q_depth/2)
		{
			location = rand()%lastLBA;
			//location = (write_loc+1)%lastLBA;
			addresses.insert(location);
			result = ssd->event_arrive(WRITE, location, 1, (double) next_request_time);
			if(result == -1)
			{
				fprintf(read_file, "==========\nCould not do a write, incomplete experiment\n");
				goto exit;
			}
			count[position]++;
			fprintf(write_file, "%.5lf\t%.5lf\n", next_request_time, result);
			write_count++;
		}	
		else
		{
			
			location = rand()%lastLBA;
			while(addresses.find(location) == addresses.end())
			{
				location = rand()%lastLBA;
			}
			cur_read_count++;	
			result = ssd->event_arrive(READ, location, 1, (double) next_request_time);
			if(result == -1)	
			{
				fprintf(read_file, "==========\nCould not do a read, incomplete experiment\n");
				goto exit;
			}
			fprintf(read_file, "%.5lf\t%.5lf\n", next_request_time, result);
			count[position]++;
		}
		response_times.insert(min_val_reference, next_request_time + result);
		
		if(cur_read_count >= total_read_count)
		{
			loop = false;
			break;
		}
			
		/*	
		loop = false;
		for(unsigned int i=0;i<q_depth;i++)
		{
			if(count[i] < req_per_thread)
			{
				loop = true;
				break;
			}
		}
		*/
		
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
