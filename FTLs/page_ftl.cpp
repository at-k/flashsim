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
	background_events.reserve(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE);
	bg_cleaning_blocks.reserve(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE);
	queue_lengths = (unsigned int *)malloc(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * sizeof(unsigned int));
	for(unsigned int i=0;i<SSD_SIZE * PACKAGE_SIZE * DIE_SIZE;i++)
	{
		open_events[i].reserve(PLANE_SIZE * BLOCK_SIZE);
		background_events[i].reserve(BLOCK_SIZE);
		bg_cleaning_blocks[i].reserve(PLANE_SIZE);
		queue_lengths[i] = 0;
	}
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


Address FtlImpl_Page::find_write_location(Event &event, Address cur, bool *already_open)
{
	Address ret_address;
	Address possible_ret_address;
	ret_address.valid = NONE;

	std::list<struct ssd_block>::iterator iter, min_queue_iter;
	unsigned int min_queue_len = 0;
	min_queue_iter = allocated_block_list.end();
	bool found_block = false;
	bool same_plane_ret_address = false;

	double time = event.get_total_time();
	bool queue_len_computed[SSD_SIZE*PACKAGE_SIZE*DIE_SIZE];
	for(unsigned int i=0;i<SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;i++)
	{
		queue_len_computed[i] = false;
	}
	for(iter=allocated_block_list.begin();iter!=allocated_block_list.end();)
	{
		if((*iter).last_page_written == BLOCK_SIZE - 1)
		{
			struct ssd_block filled_block;
			filled_block.physical_address = iter->physical_address;
			filled_block.last_write_time = iter->last_write_time;
			filled_block.valid_page_count = iter->valid_page_count;
			filled_block.lifetime_left = iter->lifetime_left;
			filled_block.last_page_written = iter->last_page_written;
			filled_block.page_mapping = (unsigned int *)malloc(BLOCK_SIZE * sizeof(unsigned int));
			for(unsigned int k=0;k<BLOCK_SIZE;k++)
			{
				filled_block.page_mapping[k] = (iter->page_mapping)[k];
			}
			filled_block_list.push_back(filled_block);
			iter = allocated_block_list.erase(iter);
			continue;
		}
		Address candidate_address = (*iter).physical_address;
		unsigned int plane_num = candidate_address.package*PACKAGE_SIZE*DIE_SIZE + candidate_address.die*DIE_SIZE + candidate_address.plane;
		if(!queue_len_computed[plane_num])
		{
			populate_queue_len(time, plane_num);
			queue_len_computed[plane_num] = true;
		}
		unsigned int queue_count = queue_lengths[plane_num];
		if(queue_count < min_queue_len || !found_block)
		{
			min_queue_iter = iter;
			min_queue_len = queue_count;
			found_block = true;
			if(	candidate_address.package == cur.package &&
				candidate_address.die == cur.die &&
				candidate_address.plane == cur.plane)
				same_plane_ret_address = true;
			if(min_queue_len == 0)
				break;
		}
		if(queue_count == min_queue_len &&
			same_plane_ret_address &&
			(candidate_address.package != cur.package || 
			 candidate_address.die != cur.die || 
			 candidate_address.plane != cur.plane)
				)
		{
			min_queue_iter = iter;
			min_queue_len = queue_count;
			found_block = true;
			same_plane_ret_address = false;
			if(min_queue_len == 0)
				break;
		}
		iter++;
	}
	if(min_queue_iter != allocated_block_list.end())
	{
		ret_address = (*min_queue_iter).physical_address;
		ret_address.page = (*min_queue_iter).last_page_written;
		ret_address.valid = PAGE;
		*already_open = true;
	}
	if(!(found_block && min_queue_len == 0))
	{
		std::list<struct ssd_block>::iterator free_iter, min_iter = free_block_list.end();
		for(free_iter=free_block_list.begin();free_iter!=free_block_list.end();free_iter++)
		{
			Address candidate_address = (*free_iter).physical_address;
			unsigned int plane_num = candidate_address.package*PACKAGE_SIZE*DIE_SIZE + candidate_address.die*DIE_SIZE + candidate_address.plane;
			if(!queue_len_computed[plane_num])
			{
				populate_queue_len(time, plane_num);	
				queue_len_computed[plane_num] = true;
			}
			unsigned int queue_count = queue_lengths[plane_num];
			if(queue_count < min_queue_len || !found_block)
			{
				min_iter = free_iter;
				min_queue_len = queue_count;
				found_block = true;
				if(	candidate_address.package == cur.package &&
					candidate_address.die == cur.die &&
					candidate_address.plane == cur.plane)
					same_plane_ret_address = true;
				if(min_queue_len == 0)
					break;
			}
			if(queue_count == min_queue_len &&
				same_plane_ret_address &&
				(candidate_address.package != cur.package || 
				 candidate_address.die != cur.die || 
				 candidate_address.plane != cur.plane)
					)
			{
				min_queue_iter = iter;
				min_queue_len = queue_count;
				found_block = true;
				same_plane_ret_address = false;
				if(min_queue_len == 0)
					break;
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
	return ret_address;
}

bool FtlImpl_Page::increment_log_write_address(Event &event, bool *gc_required, bool bg_write)
{
	Address null_address;
	null_address.valid = NONE;
	if(log_write_address.valid == NONE)
	{
		return allocate_new_block(null_address, event, gc_required, bg_write);
	}

	bool already_open = false;
	Address next_write_address = find_write_location(event, log_write_address, &already_open);
	if(next_write_address.valid == NONE)
	{
		printf("This case happened, something seems wrong\n");
		Address log_write_block_address = log_write_address;
		log_write_block_address.page = 0;
		log_write_block_address.valid = BLOCK;
		bool still_allocated = false;
		std::list<struct ssd_block>::iterator iter;
		for(iter=allocated_block_list.begin();iter!=allocated_block_list.end();iter++)
		{	
			if((*iter).physical_address == log_write_block_address)
			{
				still_allocated = true;
				log_write_address.page = (*iter).last_page_written;
				break;
			}
		}
		if(still_allocated && log_write_address.page < BLOCK_SIZE - 1)
		{
			printf("Have to write on the same block\n");
			log_write_address.page += 1;
			return true;
		}
		return allocate_new_block(null_address, event, gc_required, bg_write);
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
			return allocate_new_block(null_address, event, gc_required, bg_write);
		}
		else
		{
			next_write_address.page = 0;
			next_write_address.valid = BLOCK;
			return allocate_new_block(next_write_address, event, gc_required, bg_write);
		}
	}
}

bool FtlImpl_Page::allocate_new_block(Address requested_address, Event &event, bool *gc_required, bool bg_write)
{
	if(free_block_list.size() == 0)
	{
		printf("here returning false from allocate\n");
		return false;
	}
	if(free_block_list.size() <= clean_threshold)
	{
		*gc_required = true;
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
		if(req_iter == free_block_list.end())
			printf("This should not have happened\n");
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
	unsigned int plane_num = new_event.physical_address.package*PACKAGE_SIZE*DIE_SIZE + new_event.physical_address.die*DIE_SIZE + new_event.physical_address.plane;
	open_events[plane_num].push_back(new_event);
}

void FtlImpl_Page::process_open_events_table(Event event)
{
	std::vector<struct ftl_event>::iterator iter;
	Address event_address = event.get_address();
	unsigned int plane_num = event_address.package*PACKAGE_SIZE*DIE_SIZE + event_address.die*DIE_SIZE + event_address.plane;
	double start_time = event.get_start_time();
	for(iter=open_events[plane_num].begin();iter!=open_events[plane_num].end();)
	{
		if((*iter).end_time <= start_time)
		{
			open_events[plane_num].erase(iter);
		}
		else
		{
			iter++;
		}
	}
}


void FtlImpl_Page::add_background_event(struct ftl_event event)
{
	//background_events.push_back(event);
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
	start = filled_block_list.begin();
	end = filled_block_list.end();
	for(iter=start;iter!=end;iter++)
	{
		unsigned int erases = BLOCK_ERASES - (*iter).lifetime_left;
		if(erases < min_erases)
			min_erases = erases;
		if(erases > max_erases)
			max_erases = erases;	
	}
	for(unsigned int k=0;k<SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;k++)
	{
		for(std::vector<struct ssd_block>::iterator i=bg_cleaning_blocks[k].begin();i!=bg_cleaning_blocks[k].end();i++)
		{
			unsigned int erases = BLOCK_ERASES - (*i).lifetime_left;
			if(erases < min_erases)
				min_erases = erases;
			if(erases > max_erases)
				max_erases = erases;	
		}
	}
	controller.stats.minErase = min_erases;
	controller.stats.maxErase = max_erases;
}
FtlImpl_Page::~FtlImpl_Page(void)
{
	std::list<struct ssd_block>::iterator iter;
	for(iter=allocated_block_list.begin();iter!=allocated_block_list.end();)
		iter = allocated_block_list.erase(iter);
	for(iter=free_block_list.begin();iter!=free_block_list.end();)
		iter = free_block_list.erase(iter);
	for(iter=filled_block_list.begin();iter!=filled_block_list.end();)
		iter = filled_block_list.erase(iter);
	std::vector<struct ssd_block>::iterator i;
	for(unsigned int k=0;k<SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;k++)
	{
		for(i=bg_cleaning_blocks[k].begin();i!=bg_cleaning_blocks[k].end();)
			bg_cleaning_blocks[k].erase(i);
	}
}

enum status FtlImpl_Page::read(Event &event, bool actual_time)
{
	if(actual_time)
	{
		process_open_events_table(event);
		process_background_tasks(event, false);
	}
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
	Address read_address = logical_page_list[logical_page_num].physical_address;
		
	event.set_address(read_address);
	controller.stats.numRead++;
	enum status ret_status = controller.issue(event, actual_time);
	//if(actual_time)
	add_event(event);
	return ret_status;

}

enum status FtlImpl_Page::write(Event &event, bool actual_time)
{
	if(actual_time)
	{
		process_open_events_table(event);
		process_background_tasks(event, false);
	}
	if(event.get_noop() == true)
		return SUCCESS; 
	bool gc_required = false;
	unsigned int logical_page_num = event.get_logical_address();
	if(!increment_log_write_address(event, &gc_required, !actual_time))
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
	bool identified = false;
	
	if(need_invalidation)
	{
		for(iter=filled_block_list.begin();iter!=filled_block_list.end();iter++)
		{
			if(iter->physical_address == currently_mapped_block_address)
			{
				iter->valid_page_count -= 1;
				need_invalidation = false;
				break;
			}
		}
	}

	for(iter=allocated_block_list.begin();iter!=allocated_block_list.end();iter++)
	{
		if(iter->physical_address == log_write_block_address)
		{
			log_write_iter = iter;
			identified = true;
			if(!need_invalidation)
				break;
		}
		if(need_invalidation && iter->physical_address == currently_mapped_block_address)
		{
			iter->valid_page_count -= 1;
			need_invalidation = false;
			if(identified)
				break;
		}
	}
	
	event.set_address(log_write_address);
	controller.stats.numWrite++;
	enum status ret_status = controller.issue(event, actual_time);
	add_event(event);
	logical_page_list[logical_page_num].physical_address = log_write_address;
	(*log_write_iter).last_write_time = latest_write_time++;    
	(*log_write_iter).valid_page_count += 1;
	(*log_write_iter).page_mapping[log_write_address.page] = logical_page_num;  
	(*log_write_iter).last_page_written = log_write_address.page;
	if(actual_time && gc_required)
	{
		garbage_collect(event);
		if(free_block_list.size() == 0)
			process_background_tasks(event, true);
	}
	return ret_status;
}


enum status FtlImpl_Page::trim(Event &event)
{
	return SUCCESS;
}

enum status FtlImpl_Page::garbage_collect(Event &event)
{
	std::list<struct ssd_block>::iterator iter;
	std::list<struct ssd_block>::iterator max_benefit_block_reference = filled_block_list.end();
	float max_benefit = 0, cur_benefit;
	bool cleaning_possible = false;
	/*
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
	for(iter=filled_block_list.begin();iter!=filled_block_list.end();iter++)
	{
		average_lifetime_left += (double)iter->lifetime_left;
		if(iter->lifetime_left < min_lifetime_left || min_lifetime_left == -1)
			min_lifetime_left = iter->lifetime_left;
		if(iter->lifetime_left > max_lifetime_left || max_lifetime_left == -1)
			max_lifetime_left = iter->lifetime_left;
	}
	average_lifetime_left = average_lifetime_left/(double)(RAW_SSD_BLOCKS);
	*/
	
	for(iter=allocated_block_list.begin();iter!=allocated_block_list.end();)
	{
		if(iter->last_page_written == BLOCK_SIZE - 1)
		{
			struct ssd_block filled_block;
			filled_block.physical_address = iter->physical_address;
			filled_block.last_write_time = iter->last_write_time;
			filled_block.valid_page_count = iter->valid_page_count;
			filled_block.lifetime_left = iter->lifetime_left;
			filled_block.last_page_written = iter->last_page_written;
			filled_block.page_mapping = (unsigned int *)malloc(BLOCK_SIZE * sizeof(unsigned int));
			for(unsigned int k=0;k<BLOCK_SIZE;k++)
			{
				filled_block.page_mapping[k] = (iter->page_mapping)[k];
			}
			filled_block_list.push_back(filled_block);
			iter = allocated_block_list.erase(iter);
			continue;
		}
		else
		{
			iter++;
		}
	}
	
	for(iter=filled_block_list.begin();iter!=filled_block_list.end();iter++)
	{
		float utilization = (float)iter->valid_page_count/(float)BLOCK_SIZE;
		if(iter->valid_page_count == BLOCK_SIZE)
		{
			continue;
		}
		double age = get_average_age(*iter);
		cur_benefit = (1.0 - utilization)*(float)age / (1.0 + utilization);
		if(iter->lifetime_left == 0)
		{
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
	} 
	struct ssd_block block_to_clean = *max_benefit_block_reference;
	Address cur_page_address = block_to_clean.physical_address;
	unsigned int plane_num = cur_page_address.package*PACKAGE_SIZE*DIE_SIZE + cur_page_address.die*DIE_SIZE + cur_page_address.plane;
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
			background_events[plane_num].push_back(bg_read);
			struct ftl_event bg_write;
			bg_write.type = WRITE;
			bg_write.physical_address = cur_page_address;
			bg_write.logical_address = block_to_clean.page_mapping[i];
			bg_write.start_time = 0;
			bg_write.end_time = 0;
			background_events[plane_num].push_back(bg_write);
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

	background_events[plane_num].push_back(bg_erase);
	
	filled_block_list.erase(max_benefit_block_reference);
	bg_cleaning_blocks[plane_num].push_back(block_to_clean);
	return SUCCESS;
}

void FtlImpl_Page::process_background_tasks(Event &event, bool urgent)
{
	double cur_simulated_time = event.get_start_time();
	if(free_block_list.size() == 0)
	{
		urgent = true;
	}
	bool open_events_processed[SSD_SIZE*PACKAGE_SIZE*DIE_SIZE];
	for(unsigned int k=0;k<SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;k++)
	{
		open_events_processed[k] = false;
	}
	double earliest_possible_start = -1;
	unsigned int earliest_possible_start_plane;
	for(unsigned int plane_num=0;plane_num<SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;plane_num++)
	{
		if(background_events[plane_num].size() == 0)
		{
			continue;
		}
		if(!urgent && background_events[plane_num].front().start_time > cur_simulated_time)
			continue;
		while(background_events[plane_num].size() > 0 && (urgent || background_events[plane_num].front().start_time <= cur_simulated_time))
		{
			struct ftl_event first_event = background_events[plane_num].front();
			Address candidate_address = first_event.physical_address;
			bool perform_first_task = true;
			if(first_event.type == READ)
			{
				if(!(logical_page_list[first_event.logical_address].physical_address == first_event.physical_address))
				{
					background_events[plane_num].erase(background_events[plane_num].begin());
					if(background_events[plane_num].size() > 0)
						background_events[plane_num].front().start_time = first_event.start_time;
					continue;
				}
			}
			if(first_event.type == WRITE)
			{
				if(!(logical_page_list[first_event.logical_address].physical_address == first_event.physical_address))
				{
					background_events[plane_num].erase(background_events[plane_num].begin());
					if(background_events[plane_num].size() > 0)
						background_events[plane_num].front().start_time = first_event.start_time;
					continue;
				}
				Event probable_bg_write(WRITE, first_event.logical_address, 1, first_event.start_time);
				bool already_open;
				candidate_address = find_write_location(probable_bg_write, log_write_address, &already_open);
			}
			std::vector<struct ftl_event>::iterator iter;
			unsigned int candidate_plane = candidate_address.package*PACKAGE_SIZE*DIE_SIZE + candidate_address.die*DIE_SIZE + candidate_address.plane;
			if(!open_events_processed[candidate_plane])
			{
				Event dummy_event(READ, 0, 1, cur_simulated_time);
				dummy_event.set_address(candidate_address);
				process_open_events_table(dummy_event);
				open_events_processed[candidate_plane] = true;
			}
			for(iter=open_events[candidate_plane].begin();iter!=open_events[candidate_plane].end();iter++)
			{
				Address conflict_address = (*iter).physical_address;
				if( (*iter).start_time <= first_event.start_time &&
					(*iter).end_time > first_event.start_time 
					)
				{
					perform_first_task = false;
					if(background_events[plane_num].front().start_time < (*iter).end_time)
						background_events[plane_num].front().start_time = (*iter).end_time;	
					if(!urgent)
						break;
				}
			
			}
			if(perform_first_task)
			{
				bool is_erase = false;
				double task_time = 0;
				if(first_event.type == READ)
				{
					controller.stats.numRead++;
					Event bg_read(first_event.type, first_event.logical_address, 1, first_event.start_time);
					read(bg_read, false);
					task_time = bg_read.get_time_taken();
				}
				else if(first_event.type == WRITE)
				{
					controller.stats.numWrite++;
					Event bg_write(first_event.type, first_event.logical_address, 1, first_event.start_time);
					write(bg_write, false);
					task_time = bg_write.get_time_taken();
				}
				else if(first_event.type == ERASE)
				{
					Event bg_erase(first_event.type, first_event.logical_address, 1, first_event.start_time);
					bg_erase.set_address(first_event.physical_address);
					controller.issue(bg_erase, false);
					add_event(bg_erase);
					task_time = bg_erase.get_time_taken();
					struct ssd_block block_to_clean = bg_cleaning_blocks[plane_num].front();
					block_to_clean.valid_page_count = 0;
					block_to_clean.last_page_written = 0;
					block_to_clean.lifetime_left -= 1;
					free_block_list.push_back(block_to_clean);
					bg_cleaning_blocks[plane_num].erase(bg_cleaning_blocks[plane_num].begin());
					is_erase = true;
					controller.stats.numErase++;
				}
			
				background_events[plane_num].erase(background_events[plane_num].begin());
				if(background_events[plane_num].size() > 0)
				{
					if(background_events[plane_num].front().type == READ && first_event.type == WRITE)
						background_events[plane_num].front().start_time = first_event.start_time;
					else
						background_events[plane_num].front().start_time = first_event.start_time + task_time;
				}
				if(urgent && is_erase)
				{
					urgent = false;
				}
			}
			else
			{
				if(urgent)
				{
					if( earliest_possible_start == -1 || 
						earliest_possible_start > background_events[plane_num].front().start_time)
					{
						earliest_possible_start = background_events[plane_num].front().start_time;
						earliest_possible_start_plane = plane_num;
					}
				}
				break;
			}
		}
	}
	if(urgent && earliest_possible_start != -1)
	{
		unsigned int plane_num = earliest_possible_start_plane;
		while(background_events[plane_num].size() > 0 && urgent)
		{
			struct ftl_event first_event = background_events[plane_num].front();
			bool is_erase = false;
			double task_time = 0;
			if(first_event.type == READ)
			{
				controller.stats.numRead++;
				Event bg_read(first_event.type, first_event.logical_address, 1, first_event.start_time);
				read(bg_read, false);
				task_time = bg_read.get_time_taken();
			}
			else if(first_event.type == WRITE)
			{
				controller.stats.numWrite++;
				Event bg_write(first_event.type, first_event.logical_address, 1, first_event.start_time);
				write(bg_write, false);
				task_time = bg_write.get_time_taken();
			}
			else if(first_event.type == ERASE)
			{
				Event bg_erase(first_event.type, first_event.logical_address, 1, first_event.start_time);
				bg_erase.set_address(first_event.physical_address);
				controller.issue(bg_erase, false);
				add_event(bg_erase);
				task_time = bg_erase.get_time_taken();
				struct ssd_block block_to_clean = bg_cleaning_blocks[plane_num].front();
				block_to_clean.valid_page_count = 0;
				block_to_clean.last_page_written = 0;
				block_to_clean.lifetime_left -= 1;
				free_block_list.push_back(block_to_clean);
				bg_cleaning_blocks[plane_num].erase(bg_cleaning_blocks[plane_num].begin());
				is_erase = true;
				controller.stats.numErase++;
			}
		
			background_events[plane_num].erase(background_events[plane_num].begin());
			if(background_events[plane_num].size() > 0)
			{
				if(background_events[plane_num].front().type == READ && first_event.type == WRITE)
					background_events[plane_num].front().start_time = first_event.start_time;
				else
					background_events[plane_num].front().start_time = first_event.start_time + task_time;
			}
			if(urgent && is_erase)
			{
				urgent = false;
				break;
			}
		}
	}
	if(urgent && earliest_possible_start == -1)
		printf("Something is not quite right\n");
}

void FtlImpl_Page::populate_queue_len(double time, unsigned int plane_num)
{
	queue_lengths[plane_num] = 0;
	std::vector<struct ftl_event>::iterator iter;
	for(iter=open_events[plane_num].begin();iter!=open_events[plane_num].end();iter++)
	{
		if( (*iter).start_time <= time && (*iter).end_time > time)
		{
			queue_lengths[plane_num]++;
		}
	}
}


