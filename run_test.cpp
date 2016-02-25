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
	bool result;
	srand(time(NULL));
	
	FILE *log_file = fopen("./test_output.log", "w");

	int LAST_LBA = NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE;

	int i = 0;
	int NUM_PAGES = LAST_LBA + BLOCK_SIZE;

	bool temp_flag = false;
	double temp_total_time = 0;
	bool t_flag = false;
	double t_total_time = 0;
	bool f = false;
	double d = 0;

	result = ssd->event_arrive(WRITE, 0, 1, 0, temp_flag, temp_total_time);
	result = ssd->event_arrive(WRITE, 0, 1, 10, t_flag, t_total_time);
	printf("WRITE %d %f\n", temp_flag, temp_total_time);
	printf("WRITE %d %f\n", t_flag, t_total_time);
	result = ssd->event_arrive(NOOP, 0, 1, 0, f, d);
	printf("NOOP %d %f\n", f, d);
	printf("WRITE %d %f\n", temp_flag, temp_total_time);
	printf("WRITE %d %f\n", t_flag, t_total_time);
	result = ssd->event_arrive(NOOP, 0, 1, d/2, f, d);
	printf("NOOP %d %f\n", f, d);
	printf("WRITE %d %f\n", temp_flag, temp_total_time);
	printf("WRITE %d %f\n", t_flag, t_total_time);
	result = ssd->event_arrive(NOOP, 0, 1, d, f, d);
	if(d==std::numeric_limits<double>::max())
		printf("MAX\n");
	printf("NOOP %d %f\n", f, d);
	printf("WRITE %d %f\n", temp_flag, temp_total_time);
	printf("WRITE %d %f\n", t_flag, t_total_time);
	result = ssd->event_arrive(NOOP, 0, 1, d, f, d);
	if(d==std::numeric_limits<double>::max())
		printf("MAX\n");
	printf("NOOP %d %f\n", f, d);
	printf("WRITE %d %f\n", temp_flag, temp_total_time);
	printf("WRITE %d %f\n", t_flag, t_total_time);

	/*
	for(i=0;i<NUM_PAGES;i++)
	{
		int write_address = i%LAST_LBA;
		result = ssd->event_arrive(WRITE, write_address, 1, (double)(i*350), temp_flag, temp_total_time);
		if(result == false)
		{
			printf("breaking at write %d\n", i);
			break;
		}
	}
	*/

	delete ssd;
	return 0;
}
