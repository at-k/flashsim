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
	FILE *trace_file;
	double initial_delay;
	unsigned int q_depth;

	load_config();
	print_config(NULL);
	printf("\n");

	Ssd *ssd = new Ssd();
	unsigned int lastLBA = NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE;
	char ftl_implementation_[10] = {'0' + FTL_IMPLEMENTATION};
	std::string ftl_implementation(ftl_implementation_);
	char gc_scheme_[10] = {'0' + GC_SCHEME};
	std::string gc_scheme(gc_scheme_);

	std::string input_trace_filename = argv[1];
	std::string read_filename = input_trace_filename + ".read." + ftl_implementation + '.' + gc_scheme;
	std::string write_filename = input_trace_filename + ".write." + ftl_implementation + '.' + gc_scheme;

	read_file = fopen(read_filename.c_str(), "w");
	write_file = fopen(write_filename.c_str(), "w");
	trace_file = fopen(input_trace_filename.c_str(), "r");


	bool noop_complete = false;
	double next_noop_time = 0;
	double prev_noop_time = 0;

	bool write_complete = false;
	double write_end_time = 0;

	printf("%u\n", lastLBA);


	bool *completed = (bool *)malloc(lastLBA *sizeof(bool));
	double *time = (double *)malloc(lastLBA * sizeof(double));

	/*
	for (unsigned int i = 0; i < lastLBA; i++)
	{
		completed[i] = false;
		time[i] = 0;
		write_complete = false;
		//unsigned int location = rand()%lastLBA;
		unsigned int location = i%lastLBA;
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
	}
	//	printf("Write %d %f\n", write_complete, write_end_time);
	//
	
printf("Issued\n");
fflush(stdout);
		prev_noop_time = 0;
		int k = 0;
		while(prev_noop_time < std::numeric_limits<double>::max())
		{
			ssd->event_arrive(NOOP, 1, 1, prev_noop_time, noop_complete, prev_noop_time);
			//prev_noop_time = next_noop_time;
			k++;
			printf("%d\n", k);
		}
		
	*/
	
	initial_delay = 0;

	printf("replaying trace\n");
	std::vector<double> start_time;
	std::vector<double *> end_time;
	std::vector<bool *> op_status;
	std::vector<enum op_type> op;

	unsigned int write_count = 0;
	unsigned int read_count = 0;
	while(!feof(trace_file))
	{
		double time;
		unsigned int location;
		char op_type[10];
		bool *op_complete = (bool *)malloc(sizeof(bool));
		double *complete_time = (double *)malloc(sizeof(double));
		fscanf(trace_file, "%s\t%lf\t%d\n", op_type, &time, &location);
		if(location >= lastLBA)
			location = lastLBA - 1;
		//printf("[Application] %d %f\n", location, time);
		time = time + initial_delay;
		if(!strcmp(op_type, "Read"))
		{
			bool result = ssd->event_arrive(READ, location, 1, time, *op_complete, *complete_time);
			read_count++;
			op.push_back(OP_READ);
		}
		else
		{
			bool result = ssd->event_arrive(WRITE, location, 1, time, *op_complete, *complete_time);
			write_count++;
			op.push_back(OP_WRITE);
		}
		start_time.push_back(time);
		end_time.push_back(complete_time);
		op_status.push_back(op_complete);
	}


	printf("checking completion\n");
	for(unsigned int i=0;i<op.size();i++)
	{
		if(!(*op_status[i]))
		{
			printf("%d\n", i);
		}
		else
		{
			if(op[i] == OP_READ)
			{
				fprintf(read_file, "%.5lf\t%.5lf\t%.5lf\n", start_time[i], *end_time[i] - start_time[i], *end_time[i]);
			}
			else
			{
				fprintf(write_file, "%.5lf\t%.5lf\t%.5lf\n", start_time[i], *end_time[i] - start_time[i], *end_time[i]);
			}
		}
	}

	
	printf("Reads %u, Writes %u\n", read_count, write_count);

	ssd->print_ftl_statistics(stdout);
	fclose(read_file);
	fclose(write_file);
	delete ssd;
	return 0;
}
