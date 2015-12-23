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
#include <set>

#define SIZE 130

using namespace ssd;

int main()
{
	load_config();
	print_config(NULL);
	Ssd *ssd = new Ssd();

	std::set<int> addresses;
	double result;
	srand(time(NULL));
	
	FILE *log_file = fopen("./test_output.log", "w");

	int LAST_LBA = NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE;

	int i = 0;
	int NUM_PAGES = LAST_LBA;

	
	printf("NUM PAGES %d\n", NUM_PAGES);
	for(i=0;i<2*NUM_PAGES;i++)
	{
		int write_address = i%LAST_LBA;
		if(i==NUM_PAGES)
		{
			printf("One round done\n");
		}
		if(i > NUM_PAGES)
		{
			write_address = 7;
		}
		printf("writing %d\n", write_address);
		result = ssd->event_arrive(WRITE, write_address, 1, (double)(350 * i));
		if(result == -1)
		{
			printf("wrote %d\n", i);
			break;
		}
		//result = ssd->event_arrive(WRITE, 0, 1, 0);
	}

	return 0;

	/*
	for (i = 0; i < NUM_PAGES; i++)
	{
		result = ssd -> event_arrive(WRITE, i%LAST_LBA, 1, (double) (350 * i));
		addresses.insert(i);
	}
	printf("Experiment Starting\n");
	double initial_delay = i * 350;
	for (i = 0; i < 10000; i++)
	{
		double cur_time = initial_delay + (i*100);
		unsigned int add = 0;
		if(i%10 == 0 && i%20 != 0 && i != 0)
		//if(i%4 == 0 && i%5 != 0 && i != 0)
		{
			do
			{
				add = rand()%LAST_LBA;
			}
			while(addresses.find(add) == addresses.end());
			result = ssd -> event_arrive(READ, add, 1, cur_time);
			fprintf(log_file, "%.5lf\n", result);
		}
		if(i%20 == 0)
		{
			add = rand()%LAST_LBA;
			result = ssd -> event_arrive(WRITE, add, 1, cur_time);
		}
	}
*/
	delete ssd;
	return 0;
}
