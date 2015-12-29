/* Copyright 2011 Matias Bjørling */

/* page_ftl.cpp  */

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

/* Implements a very simple page-level FTL without merge */

#include <new>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "../ssd.h"

using namespace ssd;

FtlImpl_Page::FtlImpl_Page(Controller &controller):FtlParent(controller)
{
	RAW_SSD_BLOCKS = SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE; 
	ADDRESSABLE_SSD_PAGES = NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE; 
	logical_page_list = (struct logical_page *)malloc(ADDRESSABLE_SSD_PAGES * sizeof (struct logical_page));
	for (unsigned int i=0;i<ADDRESSABLE_SSD_PAGES;i++)
	{
		logical_page_list[i].physical_address.valid = NONE;
	}
	log_write_address.valid = NONE;

	unsigned int next_block_lba = 0;
	for(unsigned int i=0;i<RAW_SSD_BLOCKS;i++)
	{
		struct ssd_block new_ssd_block;
		new_ssd_block.physical_address = translate_lba_pba(next_block_lba);
		new_ssd_block.physical_address.valid = BLOCK;
		new_ssd_block.last_write_time = 0;
		new_ssd_block.valid_page_count = 0;
		new_ssd_block.lifetime_left = BLOCK_ERASES;
		new_ssd_block.page_mapping = (unsigned int *)malloc(BLOCK_SIZE * sizeof(unsigned int));
		for(unsigned int i=0;i<BLOCK_SIZE;i++)
			new_ssd_block.page_mapping[i] = 0;
		new_ssd_block.last_page_written = 0;
		free_block_list.push_back(new_ssd_block);
		next_block_lba = get_next_block_lba(next_block_lba);
		if(next_block_lba == 0)
			break;
	}
	clean_threshold = float(OVERPROVISIONING)/100 * SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE;
	age_variance_limit = 1;	
	open_events.reserve(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE);
	//queue_lengths = NULL;
	queue_lengths = (unsigned int *)malloc(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * sizeof(unsigned int));
	for(unsigned int i=0;i<SSD_SIZE * PACKAGE_SIZE * DIE_SIZE;i++)
	{
		queue_lengths[i] = 0;
	}
	background_events.reserve(BLOCK_SIZE);
	bg_cleaning_blocks.reserve(PLANE_SIZE);
}

unsigned int FtlImpl_Page::get_next_block_lba(unsigned int lba)
{
	Address cur_address = translate_lba_pba(lba);
	Address next_address = get_next_block_pba(cur_address);
	if(next_address.valid == NONE)
		return 0;
	return translate_pba_lba(next_address);
}

Address FtlImpl_Page::get_next_block_pba(Address pba)
{
	Address next_address = pba;
	bool carry_over = false;
	next_address.package = (next_address.package + 1)%SSD_SIZE;
	if(next_address.package == 0)
	{
		carry_over = true;
		next_address.die = (next_address.die + 1)%PACKAGE_SIZE;
	}
	else
	{
		carry_over = false;
	}
	if(carry_over && next_address.die == 0)
	{
		carry_over = true;
		next_address.plane = (next_address.plane + 1)%DIE_SIZE;
	}
	else
	{
		carry_over = false;
	}
	if(carry_over && next_address.plane == 0)
	{
		carry_over = true;
		next_address.block = (next_address.block + 1)%PLANE_SIZE;
	}
	else
	{
		carry_over = false;
	}
	if(carry_over && next_address.block == 0)
	{
		next_address.valid = NONE;
	}
	return next_address;
}

double FtlImpl_Page::get_average_age(struct ssd_block block)
{
	/*ToDo: This isn't the best way to do this
	 *because when cleaning is done, the pages are copied, and hence not written sequentially
	 *This works on the assumption that pages in a block are always written sequentially with newest writes
	 *The ideal way to do this would be to use a per page timestamp, copy that while cleaning and wear levelling
	 *Tradeoff: More memory, better age calculation
	 * */
	double average_age = 0;
	Address supposed_address = block.physical_address;
	for(unsigned int i=0;i<BLOCK_SIZE;i++)
	{
		Address actual_address = logical_page_list[block.page_mapping[i]].physical_address;
		supposed_address.page = i;
		supposed_address.valid = PAGE;
		if(actual_address == supposed_address)
		{
			unsigned int write_time = block.last_write_time - (BLOCK_SIZE - 1 - i);
			long int age = latest_write_time - write_time;
			if(age < 0)
				age = latest_write_time + (UINT_MAX - write_time) + 1;
			average_age += (double)age;
		}
	}
	if(block.valid_page_count > 0)
		average_age = average_age/(double)block.valid_page_count;
	else
		average_age = latest_write_time;
	return average_age;
}

Address FtlImpl_Page::translate_lba_pba(unsigned int lba)
{
	/*
	 *Translates logical address to a physical address. Simple mathematical transformation
	 */
	Address pba;
	pba.page = lba%BLOCK_SIZE;
	lba = lba/BLOCK_SIZE;
	pba.block = lba%PLANE_SIZE;
	lba = lba/PLANE_SIZE;
	pba.plane = lba%DIE_SIZE;
	lba = lba/DIE_SIZE;
	pba.die = lba%PACKAGE_SIZE;
	lba = lba/PACKAGE_SIZE;
	pba.package = lba%SSD_SIZE;
	pba.valid = PAGE;
	return pba;
}


unsigned int FtlImpl_Page::translate_pba_lba(Address pba)
{
	/*
	 *Transforms physical address to logical address. Simple mathematical transformation
	 */
	unsigned int lba = 0;
	lba += pba.package*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE*BLOCK_SIZE;
	lba += pba.die*DIE_SIZE*PLANE_SIZE*BLOCK_SIZE;
	lba += pba.plane*PLANE_SIZE*BLOCK_SIZE;
	lba += pba.block*BLOCK_SIZE;
	lba += pba.page;
	return lba;
}


unsigned int FtlImpl_Page::get_page_number_in_block(unsigned int lba)
{
	/*
	 *Returns the page number of the address within the block
	 */
	unsigned int page_number_in_block = lba%BLOCK_SIZE;
	return page_number_in_block;
}

unsigned int FtlImpl_Page::get_block_starting_lba(unsigned int lba)
{
	/*
	 * Returns the logical address corresponding to the 0th block in the block in which the given logical address is present
	 */
	unsigned int block_starting_lba = lba - (lba%BLOCK_SIZE);
	return block_starting_lba;
}

unsigned int FtlImpl_Page::get_logical_block_num(unsigned int lba)
{
	unsigned int logical_block_num;
	logical_block_num = lba/BLOCK_SIZE;
	return logical_block_num;
}

Address FtlImpl_Page::find_write_location(Address cur, bool *already_open)
{
	Address ret_address;
	ret_address.valid = NONE;

	std::list<struct ssd_block>::iterator iter, min_queue_iter;
	unsigned int min_queue_len = 0;
	min_queue_iter = allocated_block_list.end();
	bool found_block = false;
	for(iter=allocated_block_list.begin();iter!=allocated_block_list.end();iter++)
	{
		if((*iter).last_page_written == BLOCK_SIZE - 1)
			continue;
		Address candidate_address = (*iter).physical_address;
		if (candidate_address.package == cur.package &&
			candidate_address.die == cur.die &&
			candidate_address.plane == cur.plane)
		{
			//
		}
		else
		{
			unsigned int plane_num = candidate_address.package*PACKAGE_SIZE*DIE_SIZE + candidate_address.die*DIE_SIZE + candidate_address.plane;
			
			unsigned int queue_count = queue_lengths[plane_num];
			/*
			std::vector<struct ftl_event>::iterator event_iter;
			unsigned int queue_count = 0;
			for(event_iter=open_events.begin();event_iter!=open_events.end();event_iter++)
			{
				Address conflict_address = (*event_iter).physical_address;
				if (candidate_address.package == conflict_address.package &&
					candidate_address.die == conflict_address.die &&
					candidate_address.plane == conflict_address.plane 
					)
				{
							queue_count += 1;
				}
			}
			*/
			if(queue_count < min_queue_len || !found_block)
			{
				min_queue_iter = iter;
				min_queue_len = queue_count;
				found_block = true;
			}
		}
	}
	if(min_queue_iter != allocated_block_list.end())
	{
		ret_address = (*min_queue_iter).physical_address;
		ret_address.page = (*min_queue_iter).last_page_written;
		ret_address.valid = PAGE;
		*already_open = true;
	}
	unsigned int free_list_size = free_block_list.size();
	if(free_list_size > clean_threshold)
	{
		std::list<struct ssd_block>::iterator free_iter, min_iter = free_block_list.end();
		for(free_iter=free_block_list.begin();free_iter!=free_block_list.end();free_iter++)
		{
			Address candidate_address = (*free_iter).physical_address;
			if (candidate_address.package == cur.package &&
				candidate_address.die == cur.die &&
				candidate_address.plane == cur.plane)
			{
				//
			}
			else
			{
				unsigned int plane_num = candidate_address.package*PACKAGE_SIZE*DIE_SIZE + candidate_address.die*DIE_SIZE + candidate_address.plane;
				
				unsigned int queue_count = queue_lengths[plane_num];
				/*
				std::vector<struct ftl_event>::iterator event_iter;
				unsigned int queue_count = 0;
				for(event_iter=open_events.begin();event_iter!=open_events.end();event_iter++)
				{
					Address conflict_address = (*event_iter).physical_address;
					if (candidate_address.package == conflict_address.package &&
						candidate_address.die == conflict_address.die &&
						candidate_address.plane == conflict_address.plane 
						)
					{
								queue_count += 1;
					}
				}
				*/
				if(queue_count < min_queue_len || !found_block)
				{
					min_iter = free_iter;
					min_queue_len = queue_count;
					found_block = true;
				}
			}
		}
		if(min_iter != free_block_list.end())
		{
			ret_address = (*min_iter).physical_address;
			ret_address.page = 0;
			ret_address.valid = PAGE;
			*already_open = false;
		}
	}
	fflush(stdout);
	return ret_address;
}

bool FtlImpl_Page::increment_log_write_address(Event &event)
{
	Address null_address;
	null_address.valid = NONE;
	if(log_write_address.valid == NONE)
		return allocate_new_block(null_address, event);

	bool already_open = false;
	Address next_write_address = find_write_location(log_write_address, &already_open);
	if(next_write_address.valid == NONE)
	{
		//TODO improve performance here by having an allocted flag in the struct ssd_block
		Address log_write_block_address = log_write_address;
		log_write_block_address.page = 0;
		log_write_block_address.valid = BLOCK;
		bool still_allocated = false;
		std::list<struct ssd_block>::iterator iter;
		for(iter=allocated_block_list.begin();iter!=allocated_block_list.end();iter++)
		{	
			if((*iter).physical_address == log_write_block_address)
				still_allocated = true;
		}
		if(still_allocated && log_write_address.page < BLOCK_SIZE - 1)
		{
			log_write_address.page += 1;
			return true;
		}

		return allocate_new_block(null_address, event);
	}
	else
	{
		if(already_open)
		{
			log_write_address = next_write_address;
			if(log_write_address.page < BLOCK_SIZE - 1)
			{
				log_write_address.page += 1;
				return true;
			}
			return allocate_new_block(null_address, event);
		}
		else
		{
			next_write_address.page = 0;
			next_write_address.valid = BLOCK;
			return allocate_new_block(next_write_address, event);
		}
	}
}

bool FtlImpl_Page::allocate_new_block(Address requested_address, Event &event)
{
	unsigned int free_list_size = free_block_list.size();
	while(free_list_size == 0)
	{
		if(bg_cleaning_blocks.size() > 0)
		{
			process_background_tasks(event, true);
			//printf("[ANB] There is space but its busy\n");
		}
		else
		{
			printf("[ANB] No space found\n");
			return false;
		}
	}  
	bool ret_val = false;
	if(requested_address.valid == NONE)
	{
		struct ssd_block new_ssd_block = free_block_list.front();
		allocated_block_list.push_back(new_ssd_block);
		free_block_list.pop_front();
		log_write_address = allocated_block_list.back().physical_address;
		log_write_address.page = 0;
		log_write_address.valid = PAGE;
		ret_val = true;
	}
	else
	{
		std::list<struct ssd_block>::iterator iter, req_iter = free_block_list.end();
		for(iter=free_block_list.begin();iter!=free_block_list.end();iter++)
		{
			if((*iter).physical_address == requested_address)
			{
				req_iter = iter;
				break;
			}
		}
		struct ssd_block new_ssd_block = (*req_iter);
		allocated_block_list.push_back(new_ssd_block);
		free_block_list.erase(req_iter);
		log_write_address = allocated_block_list.back().physical_address;
		log_write_address.page = 0;
		log_write_address.valid = PAGE;
		ret_val = true;
	}
	return ret_val;
}

void FtlImpl_Page::add_event(Event event)
{
	struct ftl_event new_event;
	new_event.type = event.get_event_type();
	new_event.logical_address = event.get_logical_address();
	new_event.physical_address = event.get_address();
	new_event.start_time = event.get_start_time();
	new_event.end_time = event.get_total_time();
	open_events.push_back(new_event);
	unsigned int plane_num = new_event.physical_address.package*PACKAGE_SIZE*DIE_SIZE + new_event.physical_address.die*DIE_SIZE + new_event.physical_address.plane;
	queue_lengths[plane_num]++;
}

void FtlImpl_Page::process_open_events_table(Event event)
{
	std::vector<struct ftl_event>::iterator iter;
	double start_time = event.get_start_time();
	for(iter=open_events.begin();iter!=open_events.end();)
	{
		if((*iter).end_time < start_time)
		{
			unsigned int plane_num = (iter->physical_address).package*PACKAGE_SIZE*DIE_SIZE + (iter->physical_address).die*DIE_SIZE + (iter->physical_address).plane;
			queue_lengths[plane_num]--;
			open_events.erase(iter);
		}
		else
			iter++;
	}
}

void FtlImpl_Page::add_background_event(struct ftl_event event)
{
	background_events.push_back(event);
}

void FtlImpl_Page::get_min_max_erases()
{
	std::list<struct ssd_block>::iterator iter, start, end;
	start = allocated_block_list.begin();
	end = allocated_block_list.end();
	unsigned int min_erases = UINT_MAX;
	unsigned int max_erases = 0;
	for(iter=start;iter!=end;iter++)
	{
		unsigned int erases = BLOCK_ERASES - (*iter).lifetime_left;
		if(erases < min_erases)
			min_erases = erases;
		if(erases > max_erases)
			max_erases = erases;	
	}
	start = free_block_list.begin();
	end = free_block_list.end();
	for(iter=start;iter!=end;iter++)
	{
		unsigned int erases = BLOCK_ERASES - (*iter).lifetime_left;
		if(erases < min_erases)
			min_erases = erases;
		if(erases > max_erases)
			max_erases = erases;	
	}
	controller.stats.minErase = min_erases;
	controller.stats.maxErase = max_erases;
	printf("returning");
	fflush(stdout);
}
FtlImpl_Page::~FtlImpl_Page(void)
{
}

enum status FtlImpl_Page::read(Event &event)
{
	process_open_events_table(event);
	unsigned int logical_page_num = event.get_logical_address();
	if(logical_page_num >= ADDRESSABLE_SSD_PAGES)
	{
		printf("returning false because the page is out of range\n");
		return FAILURE;
	}
	if(logical_page_list[logical_page_num].physical_address.valid == NONE)
	{
		printf("returning false because page has not been written yet\n");
		return FAILURE;
	}
	process_background_tasks(event, false);
	Address read_address = logical_page_list[logical_page_num].physical_address;
	event.set_address(read_address);
	controller.stats.numFTLRead++;
	enum status ret_status = controller.issue(event, true);
	add_event(event);
	return ret_status;

}

enum status FtlImpl_Page::write(Event &event)
{
	process_open_events_table(event);
	unsigned int logical_page_num = event.get_logical_address();
	if(!increment_log_write_address(event))
	{
		printf("returning known FAILURE\n");
		return FAILURE; 
	}  

	std::list<struct ssd_block>::iterator iter, log_write_iter = allocated_block_list.end();
	Address currently_mapped_address = logical_page_list[logical_page_num].physical_address;
	Address log_write_block_address = log_write_address;
	log_write_block_address.page = 0;
	log_write_block_address.valid = BLOCK;
	Address currently_mapped_block_address = currently_mapped_address;
	currently_mapped_block_address.page = 0;
	currently_mapped_block_address.valid = BLOCK;
	bool need_invalidation = (currently_mapped_address.valid == PAGE);
	for(iter=allocated_block_list.begin();iter!=allocated_block_list.end();iter++)
	{
		if(need_invalidation && iter->physical_address == currently_mapped_block_address)
			iter->valid_page_count -= 1;
		if(iter->physical_address == log_write_block_address)
			log_write_iter = iter;
	}
	process_background_tasks(event, false);
	event.set_address(log_write_address);
	controller.stats.numWrite++;
	enum status ret_status = controller.issue(event, true);
	add_event(event);
	logical_page_list[logical_page_num].physical_address = log_write_address;
	(*log_write_iter).last_write_time = latest_write_time++;    
	(*log_write_iter).valid_page_count += 1;
	(*log_write_iter).page_mapping[log_write_address.page] = logical_page_num;  
	(*log_write_iter).last_page_written = log_write_address.page;
	if(free_block_list.size() < clean_threshold)
	{
		garbage_collect(event);
	}
	return ret_status;
}


enum status FtlImpl_Page::trim(Event &event)
{
	return SUCCESS;
}

enum status FtlImpl_Page::garbage_collect(Event &event)
{
	while(free_block_list.size() == 0)
	{
		if(bg_cleaning_blocks.size() > 0)
		{
			//printf("There is space but its busy\n");
			process_background_tasks(event, true);
		}
		else
		{
			printf("garbage collector returned FAILURE \n");
			return FAILURE; 
		}
	}  		

	std::list<struct ssd_block>::iterator iter, max_benefit_block_reference = allocated_block_list.end();
	float max_benefit = 0, cur_benefit;
	bool cleaning_possible = false;
	double average_lifetime_left = 0, min_lifetime_left = -1, max_lifetime_left = -1;
	for(iter=allocated_block_list.begin();iter!=allocated_block_list.end();iter++)
	{
		average_lifetime_left += (double)iter->lifetime_left;
		if(iter->lifetime_left < min_lifetime_left || min_lifetime_left == -1)
			min_lifetime_left = iter->lifetime_left;
		if(iter->lifetime_left > max_lifetime_left || max_lifetime_left == -1)
			max_lifetime_left = iter->lifetime_left;
	}
	for(iter=free_block_list.begin();iter!=free_block_list.end();iter++)
	{
		average_lifetime_left += (double)iter->lifetime_left;
		if(iter->lifetime_left < min_lifetime_left || min_lifetime_left == -1)
			min_lifetime_left = iter->lifetime_left;
		if(iter->lifetime_left > max_lifetime_left || max_lifetime_left == -1)
			max_lifetime_left = iter->lifetime_left;
	}
	
	average_lifetime_left = average_lifetime_left/(double)(RAW_SSD_BLOCKS);
	for(iter=allocated_block_list.begin();iter!=allocated_block_list.end();iter++)
	{
		if(iter == --allocated_block_list.end())
		{
			continue;
		}
		float utilization = (float)iter->valid_page_count/(float)BLOCK_SIZE;
		double age = get_average_age(*iter);
		cur_benefit = (1.0 - utilization)*(float)age / (1.0 + utilization);
		if(iter->lifetime_left == 0)
		{
			continue;  
		}
		//float probab_to_skip = 0;
		//srand(time(NULL));
		if(iter->lifetime_left < age_variance_limit*average_lifetime_left)
		{
			//probab_to_skip = 1.0 - (float)iter->lifetime_left/(age_variance_limit*average_lifetime_left);
			//TODO: add these to a list and then process that list in case cleaning_possible is false
			continue;
		}
		if(cur_benefit > 0 && (max_benefit == 0 || cur_benefit > max_benefit)) //&& rand()/RAND_MAX >= probab_to_skip)
		{
			max_benefit = cur_benefit;
			max_benefit_block_reference = iter; 
			cleaning_possible = true;
		}    
	}
	if(!cleaning_possible)
	{
		printf("cleaning is not possible\n");
		return FAILURE;
		//return wear_level(event);
	} 
	struct ssd_block block_to_clean = *max_benefit_block_reference;
	struct ssd_block cleaning_block = free_block_list.front();
	unsigned int page_pointer = 0;
	Address cur_page_address = block_to_clean.physical_address;
	bool clean_pages_found = false;
	for(unsigned int i=0;i<BLOCK_SIZE;i++)
	{
		cur_page_address.page = i;
		cur_page_address.valid = PAGE; 
		if(cur_page_address == logical_page_list[block_to_clean.page_mapping[i]].physical_address)
		{
			struct ftl_event bg_read;
			bg_read.type = READ;
			bg_read.physical_address = cur_page_address;
			bg_read.logical_address = block_to_clean.page_mapping[i];
			bg_read.start_time = event.get_total_time() - event.get_time_taken();
			bg_read.end_time = 0;
			//add_background_event(bg_read);
			background_events.push_back(bg_read);
			struct ftl_event bg_write;
			bg_write.type = WRITE;
			bg_write.physical_address = cleaning_block.physical_address;
			bg_write.physical_address.page = page_pointer;
			bg_write.physical_address.valid = PAGE;
			bg_write.logical_address = block_to_clean.page_mapping[i];
			bg_write.start_time = 0;
			bg_write.end_time = 0;
			//add_background_event(bg_write);
			background_events.push_back(bg_write);
			page_pointer += 1;
			clean_pages_found = true;
		}
	}
	struct ftl_event bg_erase;
	bg_erase.type = ERASE;
	bg_erase.physical_address = block_to_clean.physical_address;
	bg_erase.logical_address = translate_pba_lba(block_to_clean.physical_address);
	if(clean_pages_found)
		bg_erase.start_time = 0;
	else
		bg_erase.start_time = event.get_total_time() - event.get_time_taken();
	bg_erase.end_time = 0;

	//add_background_event(bg_erase);
	background_events.push_back(bg_erase);
	
	struct background_cleaning_blocks new_cleaning_blocks;
	new_cleaning_blocks.block_to_clean = block_to_clean;
	new_cleaning_blocks.cleaning_block = cleaning_block;
	
	allocated_block_list.erase(max_benefit_block_reference);
	free_block_list.pop_front();
	bg_cleaning_blocks.push_back(new_cleaning_blocks);
	return SUCCESS;
}

void FtlImpl_Page::process_background_tasks(Event &event, bool urgent)
{
	double cur_simulated_time = event.get_start_time();
	if(background_events.size() == 0)
		return;
	if(!urgent && background_events.front().start_time > cur_simulated_time)
		return;
	//printf("Comparing %f and %f\n", background_events.front().start_time, cur_simulated_time);
	while(background_events.size() > 0 && (urgent || background_events.front().start_time <= cur_simulated_time))
	{
		struct ftl_event first_event = background_events.front();
		Address candidate_address = first_event.physical_address;
		bool perform_first_task = true;
		unsigned int plane_num = candidate_address.package*PACKAGE_SIZE*DIE_SIZE + candidate_address.die*DIE_SIZE + candidate_address.plane;
		unsigned int queue_count = queue_lengths[plane_num];
		if(queue_count != 0)
		{
			double event_total_time = event.get_total_time(); 
			std::vector<struct ftl_event>::iterator iter;
			for(iter=open_events.begin();iter!=open_events.end();iter++)
			{
			
				Address conflict_address = (*iter).physical_address;
				if( (*iter).start_time < first_event.start_time &&
					candidate_address.package == conflict_address.package &&
					candidate_address.die == conflict_address.die &&
					candidate_address.plane == conflict_address.plane	
					)
				{
					//printf("Found conflicting task at ");
					//conflict_address.print();
					//printf("\n");
					//fflush(stdout);
					perform_first_task = false;
					if(background_events.front().start_time < (*iter).end_time)
						background_events.front().start_time = (*iter).end_time;	
					if(!urgent)
						break;
					else if(event_total_time < (*iter).end_time)
					{
						double diff = (*iter).end_time - event_total_time;
						event_total_time += diff;
					}
				}
		
			}
			if(urgent)
			{
				event.incr_time_taken(event_total_time - event.get_total_time());
			}
		}
		if(urgent || perform_first_task)
		{
			bool is_erase = false;
			Event bg_task(first_event.type, first_event.logical_address, 1, first_event.start_time);
			bg_task.set_address(first_event.physical_address);
			controller.issue(bg_task, !urgent);
			double task_time = bg_task.get_time_taken();
			if(urgent)
				event.incr_time_taken(task_time);
			if(cur_simulated_time < first_event.start_time + task_time)
				add_event(bg_task);
			if(first_event.type == READ)
				controller.stats.numRead++;				
			if(first_event.type == WRITE)
			{
				logical_page_list[first_event.logical_address].physical_address = first_event.physical_address;
				bg_cleaning_blocks.front().cleaning_block.page_mapping[first_event.physical_address.page] = first_event.logical_address;
				bg_cleaning_blocks.front().cleaning_block.last_page_written = first_event.physical_address.page;
				controller.stats.numWrite++;
			}
			if(first_event.type == ERASE)
			{
				struct background_cleaning_blocks freed_blocks = bg_cleaning_blocks.front();
				struct ssd_block block_to_clean = freed_blocks.block_to_clean;
				struct ssd_block cleaning_block = freed_blocks.cleaning_block;	
				cleaning_block.valid_page_count = block_to_clean.valid_page_count;
				block_to_clean.valid_page_count = 0;
				block_to_clean.lifetime_left -= 1;
				cleaning_block.last_write_time = block_to_clean.last_write_time;
				free_block_list.push_back(block_to_clean);
				allocated_block_list.push_back(cleaning_block);
				bg_cleaning_blocks.erase(bg_cleaning_blocks.begin());
				is_erase = true;
				controller.stats.numErase++;
			}
			background_events.erase(background_events.begin());
			if(background_events.size() > 0)
			{
				background_events.front().start_time = first_event.start_time + task_time;
				//printf("Comparing %f and %f\n", background_events.front().start_time, cur_simulated_time);
				//printf("%d %d\n", background_events.size(), background_events.front().start_time < cur_simulated_time);
			}
			if(urgent && is_erase)
				break;
		}
		else
		{
			break;
		}
	}
}

/*
enum status FtlImpl_Page::wear_level(Event &event)
{
	std::list<struct ssd_block>::iterator iter, healthiest_block_reference, wear_block_reference;
	unsigned int max_lifetime_left = 0;
	double max_benefit = 0;
	bool cleaning_possible = false;
	for(iter=allocated_block_list.begin();iter!=allocated_block_list.end();iter++)
	{
		unsigned int cur_lifetime_left = iter->lifetime_left;
		if(cur_lifetime_left >= max_lifetime_left)
		{
			max_lifetime_left = cur_lifetime_left;
			healthiest_block_reference = iter;
		}
		float utilization = (float)iter->valid_page_count/(float)BLOCK_SIZE;
		double age = get_average_age(*iter);
		if(iter->lifetime_left == 0)
			continue;
		double cur_benefit = (1.0 - utilization)*(float)age / (1.0 + utilization);
		if(cur_benefit > 0 && (max_benefit == 0 || cur_benefit > max_benefit))
		{
			max_benefit = cur_benefit;
			wear_block_reference = iter;
			cleaning_possible = true; 
		}    
	}

	if(free_block_list.size() == 0 || !cleaning_possible)
		return FAILURE;


	struct ssd_block *cleaning_block = &(free_block_list.front());
	unsigned int page_pointer = 0;
	cleaning_block->valid_page_count += wear_block_reference->valid_page_count;
	Address cur_page_address = wear_block_reference->physical_address;
	for(unsigned int i=0;i<BLOCK_SIZE;i++)
	{
		cur_page_address.page = i;
		cur_page_address.valid = PAGE; 
		if(cur_page_address == logical_page_list[wear_block_reference->page_mapping[i]].physical_address)
		{
			Event read_to_copy(READ, wear_block_reference->page_mapping[i], 1, event.get_total_time());
			read_to_copy.set_address(cur_page_address);
			controller.issue(read_to_copy);
			add_event(read_to_copy);
			event.incr_time_taken(read_to_copy.get_time_taken());
			Event write_to_copy(WRITE, wear_block_reference->page_mapping[i], 1, event.get_total_time());
			Address write_to_address = cleaning_block->physical_address;
			write_to_address.page = page_pointer;
			write_to_address.valid = PAGE;
			write_to_copy.set_address(write_to_address);
			controller.issue(write_to_copy);
			add_event(write_to_copy);
			event.incr_time_taken(write_to_copy.get_time_taken());
			logical_page_list[wear_block_reference->page_mapping[i]].physical_address = write_to_address;
			cleaning_block->page_mapping[page_pointer] = wear_block_reference->page_mapping[i];
			page_pointer += 1;

		}
	}
	wear_block_reference->valid_page_count = 0;
	Event erase_event(ERASE, translate_pba_lba(wear_block_reference->physical_address), 1, event.get_total_time());
	erase_event.set_address(wear_block_reference->physical_address);
	controller.issue(erase_event);
	add_event(erase_event);
	event.incr_time_taken(erase_event.get_time_taken());
	wear_block_reference->lifetime_left -= 1;
	cleaning_block->last_write_time = wear_block_reference->last_write_time;
	log_write_address = cleaning_block->physical_address;
	log_write_address.page = page_pointer;
	log_write_address.valid = PAGE;

	struct ssd_block block_to_copy = *healthiest_block_reference;
	page_pointer = 0;
	wear_block_reference->valid_page_count += block_to_copy.valid_page_count;
	cur_page_address = block_to_copy.physical_address;
	for(unsigned int i=0;i<BLOCK_SIZE;i++)
	{
		cur_page_address.page = i;
		cur_page_address.valid = PAGE;
		if(cur_page_address == logical_page_list[block_to_copy.page_mapping[i]].physical_address)
		{
			Event read_to_copy(READ, block_to_copy.page_mapping[i], 1, event.get_total_time());
			read_to_copy.set_address(cur_page_address);
			controller.issue(read_to_copy);
			add_event(read_to_copy);
			event.incr_time_taken(read_to_copy.get_time_taken());
			Event write_to_copy(WRITE, block_to_copy.page_mapping[i], 1, event.get_total_time());
			Address write_to_address = wear_block_reference->physical_address;
			write_to_address.page = page_pointer;
			write_to_address.valid = PAGE;
			write_to_copy.set_address(write_to_address);
			controller.issue(write_to_copy);
			add_event(write_to_copy);
			event.incr_time_taken(write_to_copy.get_time_taken());
			logical_page_list[block_to_copy.page_mapping[i]].physical_address = write_to_address;
			wear_block_reference->page_mapping[page_pointer] = block_to_copy.page_mapping[i];
			page_pointer += 1;
		}
	}
	block_to_copy.valid_page_count = 0;
	Event copy_event(ERASE, translate_pba_lba(block_to_copy.physical_address), 1, event.get_total_time());
	copy_event.set_address(block_to_copy.physical_address);
	controller.issue(copy_event);
	add_event(copy_event);
	event.incr_time_taken(copy_event.get_time_taken());
	block_to_copy.lifetime_left -= 1;
	wear_block_reference->last_write_time = block_to_copy.last_write_time;
	allocated_block_list.erase(healthiest_block_reference);
	free_block_list.push_back(block_to_copy);  
	allocated_block_list.push_back(*cleaning_block);
	free_block_list.pop_front();
	return SUCCESS;
}
*/
