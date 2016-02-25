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

unsigned int costly_counter = 0;
unsigned int urgent_counter = 0;
unsigned int skip_count = 0;

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
	READ_PREFERENCE = true;
	urgent_cleaning = false;
	open_events = std::vector<std::vector<struct ftl_event> >(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE);
	background_events = std::vector<std::vector<struct ftl_event> >(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE);
	bg_cleaning_blocks = std::vector<std::vector<struct ssd_block> >(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE);
	urgent_bg_events = std::vector<std::vector<struct urgent_bg_events_pointer> >(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE);
	urgent_queues = std::vector<std::vector<struct urgent_ftl_event *> >(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE);
	queue_lengths = (unsigned int *)malloc(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * sizeof(unsigned int));
	gc_required = false;
	for(unsigned int i=0;i<SSD_SIZE * PACKAGE_SIZE * DIE_SIZE;i++)
	{
		open_events[i].reserve(PLANE_SIZE * BLOCK_SIZE);
		background_events[i].reserve(BLOCK_SIZE);
		bg_cleaning_blocks[i].reserve(PLANE_SIZE);
		urgent_bg_events[i].reserve(BLOCK_SIZE);
		urgent_queues[i].reserve(BLOCK_SIZE);
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
		ret_address.page = (*min_queue_iter).last_page_written + 1;
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

bool FtlImpl_Page::increment_log_write_address(Event &event, Address asked_for, bool already_allocated, bool bg_write)
{
	Address null_address;
	null_address.valid = NONE;
	if(log_write_address.valid == NONE)
	{
		return allocate_new_block(null_address);
	}

	bool already_open = false;
	Address next_write_address;
	if(asked_for.valid == PAGE)
	{
		next_write_address = asked_for;
		already_open = already_allocated;
	}
	else
	{
		next_write_address = find_write_location(event, log_write_address, &already_open);
	}
	if(already_open)
	{
		log_write_address = next_write_address;
		return true;
	}
	else
	{
		next_write_address.page = 0;
		next_write_address.valid = BLOCK;
		return allocate_new_block(next_write_address);
	}
}

bool FtlImpl_Page::allocate_new_block(Address requested_address)
{
	if(free_block_list.size() == 0)
	{
		printf("here returning false from allocate\n");
		return false;
	}
	if(free_block_list.size() <= clean_threshold)
	{
		gc_required = true;
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

bool compare_ftl_event_start_times(const struct ftl_event a, const struct ftl_event b) 
{
	return a.start_time < b.start_time;
}


double FtlImpl_Page::process_open_events_table(double start_time)
{
	double ret_time = std::numeric_limits<double>::max();
	for(unsigned int plane_num = 0;plane_num < SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;plane_num++)
	{
		double time = process_open_events_table(plane_num, start_time);
		ret_time = time < ret_time ? time : ret_time;
	}
	return ret_time;
}

double FtlImpl_Page::process_open_events_table(unsigned int plane_num, double start_time)
{
	std::vector<struct ftl_event>::iterator iter;
	double ret_time = std::numeric_limits<double>::max();
	for(iter=open_events[plane_num].begin();iter!=open_events[plane_num].end();)
	{
		if((*iter).end_time <= start_time)
		{
			if(iter->op_complete_pointer)
				*(iter->op_complete_pointer) = true;
			if(iter->end_time_pointer)
				*(iter->end_time_pointer) = iter->end_time;
			open_events[plane_num].erase(iter);
		}
		else
		{
			ret_time = iter->end_time;
			break;
		}
	}
	return ret_time;
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
	printf("Costly Counter %d\n", costly_counter);
	printf("Urgent Counter %d\n", urgent_counter);
	printf("Skip Count %d\n", skip_count);
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
	for(unsigned int l=0;l<SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;l++)
	{
		bg_cleaning_blocks[l].clear();
		urgent_bg_events[l].clear();
		open_events[l].clear();
		background_events[l].clear();
		std::vector<struct urgent_ftl_event *>::iterator i;
		for(i=urgent_queues[l].begin();i!=urgent_queues[l].end();i++)
		{
			free(*i);
		}
		urgent_queues[l].clear();
	}
	bg_cleaning_blocks.clear();
	urgent_bg_events.clear();
	open_events.clear();
	background_events.clear();
	urgent_queues.clear();
	free(queue_lengths);
}

enum status FtlImpl_Page::read(Event &event, bool &op_complete, double &end_time, bool actual_time)
{
	if(actual_time)
	{
		process_open_events_table(event.get_start_time());
		process_background_tasks(event);
		process_urgent_queues(event);
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
	unsigned int plane_num = read_address.package*PACKAGE_SIZE*DIE_SIZE + read_address.die*DIE_SIZE + read_address.plane;
	struct ftl_event fg_read;
	fg_read.type = READ;
	fg_read.start_time = event.get_start_time();
	fg_read.logical_address = event.get_logical_address();
	fg_read.physical_address = read_address;
	fg_read.process = FOREGROUND;
	fg_read.op_complete_pointer = &op_complete;
	fg_read.end_time_pointer = &end_time;
	if(urgent_queues[plane_num].size() == 0 || READ_PREFERENCE)
	{
		fg_read.end_time = read_(event, actual_time);	
		open_events[plane_num].push_back(fg_read);
		if(READ_PREFERENCE && urgent_queues[plane_num].size() > 0)
		{
			std::vector<struct urgent_ftl_event *>::iterator first_iterator = urgent_queues[plane_num].begin();
			struct urgent_ftl_event *first_event = *(first_iterator);
			if(first_event->event.start_time < fg_read.end_time)
				first_event->event.start_time = fg_read.end_time;
		}
	}
	else
	{
		fg_read.end_time = fg_read.start_time;
		struct urgent_ftl_event *stalled_fg_read = (struct urgent_ftl_event *)malloc(sizeof(struct urgent_ftl_event));
		stalled_fg_read->event = fg_read;
		stalled_fg_read->child = NULL;
		stalled_fg_read->parent_completed = true;
		stalled_fg_read->predecessor_completed = false;
		urgent_queues[plane_num].push_back(stalled_fg_read);
	}
	return SUCCESS;
}


double FtlImpl_Page::read_(Event &event, bool actual_time)
{
	controller.stats.numRead++;
	controller.issue(event, actual_time);
	return event.get_total_time();
}

enum status FtlImpl_Page::noop(Event &event, bool &op_complete, double &end_time, bool actual_time)
{
	double e_time = std::numeric_limits<double>::max();
	if(actual_time)
	{
		double o_time = process_open_events_table(event.get_start_time());
		e_time = o_time < e_time ? o_time : e_time;
		double b_time = process_background_tasks(event);
		e_time = b_time < e_time ? b_time : e_time;
		double u_time = process_urgent_queues(event);
		e_time = u_time < e_time ? u_time : e_time;
	}
	event.incr_time_taken(e_time - event.get_start_time());
	end_time = e_time;
	op_complete = true;
	return SUCCESS;
}

enum status FtlImpl_Page::write(Event &event, bool &op_complete, double &end_time, bool actual_time)
{
	if(actual_time)
	{
		process_open_events_table(event.get_start_time());
		process_background_tasks(event);
		process_urgent_queues(event);
	}
	Address cur_address;
	cur_address.valid = NONE;
	if(!increment_log_write_address(event, cur_address, false, !actual_time))
	{
		printf("returning known FAILURE\n");
		return FAILURE; 
	} 
	event.set_address(log_write_address);
	struct ftl_event fg_write;
	fg_write.type = WRITE;
	fg_write.start_time = event.get_start_time();
	fg_write.logical_address = event.get_logical_address();
	fg_write.process = FOREGROUND;
	fg_write.op_complete_pointer = &op_complete;
	fg_write.end_time_pointer = &end_time;
	Address write_address = event.get_address();
	fg_write.physical_address = write_address;
	unsigned int plane_num = write_address.package*PACKAGE_SIZE*DIE_SIZE + write_address.die*DIE_SIZE + write_address.plane;
	if(urgent_queues[plane_num].size() == 0)
	{
		fg_write.end_time = write_(event, actual_time);
		open_events[plane_num].push_back(fg_write);
	}
	else
	{
		fg_write.end_time = fg_write.start_time;
		struct urgent_ftl_event *stalled_fg_write = (struct urgent_ftl_event *)malloc(sizeof(struct urgent_ftl_event));
		stalled_fg_write->event = fg_write;
		stalled_fg_write->child = NULL;
		stalled_fg_write->parent_completed = true;
		stalled_fg_write->predecessor_completed = false;
		urgent_queues[plane_num].push_back(stalled_fg_write);	
	}
	return SUCCESS;
}

double FtlImpl_Page::write_(Event &event, bool actual_time)
{
	unsigned int logical_page_num = event.get_logical_address();
	Address write_address = event.get_address();
	std::list<struct ssd_block>::iterator iter, log_write_iter = allocated_block_list.end();
	Address currently_mapped_address = logical_page_list[logical_page_num].physical_address;
	Address log_write_block_address = write_address;
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
	event.set_address(write_address);
	controller.stats.numWrite++;
	controller.issue(event, actual_time);
	logical_page_list[logical_page_num].physical_address = write_address;
	(*log_write_iter).last_write_time = latest_write_time++;    
	(*log_write_iter).valid_page_count += 1;
	(*log_write_iter).page_mapping[write_address.page] = logical_page_num;  
	(*log_write_iter).last_page_written = write_address.page;
	if(actual_time && gc_required)
	{
		garbage_collect(event);
		if(free_block_list.size() == 0)
		{
			set_urgent_queues(event);
		}
	}
	return event.get_total_time();
}

enum status FtlImpl_Page::trim(Event &event)
{
	return SUCCESS;
}


enum status FtlImpl_Page::garbage_collect(Event &event)
{
	enum status ret_status;
	switch(GC_SCHEME)
	{
		case(0):
			ret_status = garbage_collect_default(event);
			break;
		case(1):
			ret_status = garbage_collect_cached(event);
			break;
		default:
			ret_status = garbage_collect_default(event);
			break;
	}
	return ret_status;
}


enum status FtlImpl_Page::garbage_collect_default(Event &event)
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
	
	/*
	cleaning_possible = false;
	for(unsigned int i=0;i<SSD_SIZE*PACKAGE_SIZE*die_size;i++)
	{
		if(bg_cleaning_blocks[i].size() == 0)
		{
			cleaning_possible = true;
			break;
		}
	}

	if(!cleaning_possible)
	{
		return FAILURE;
	}
	*/

	cleaning_possible = false;

	for(iter=filled_block_list.begin();iter!=filled_block_list.end();iter++)
	{
		float utilization = (float)iter->valid_page_count/(float)BLOCK_SIZE;
		if(iter->valid_page_count == BLOCK_SIZE)
		{
			continue;
		}
		double age = get_average_age(*iter);
		cur_benefit = (1.0 - utilization)*(float)age / (1.0 + utilization);
		Address possible_address = iter->physical_address;
		unsigned int possible_plane_num = possible_address.package*PACKAGE_SIZE*DIE_SIZE + possible_address.die*DIE_SIZE + possible_address.plane;
		if(iter->lifetime_left == 0)// || bg_cleaning_blocks[possible_plane_num].size() > 0)
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
	
	unsigned int total_bg_cleaning_blocks = 0;

	for(unsigned int i=0;i<SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;i++)
	{
		total_bg_cleaning_blocks += bg_cleaning_blocks[i].size();
	}

	if(total_bg_cleaning_blocks >= 5)//SSD_SIZE*PACKAGE_SIZE*DIE_SIZE)
		return FAILURE;
	
	struct ssd_block block_to_clean = *max_benefit_block_reference;
	Address cur_page_address = block_to_clean.physical_address;
	unsigned int plane_num = cur_page_address.package*PACKAGE_SIZE*DIE_SIZE + cur_page_address.die*DIE_SIZE + cur_page_address.plane;
	bool clean_pages_found = false;
	struct urgent_bg_events_pointer urgent_bg_events_location;
	urgent_bg_events_location.rw_start_index = background_events[plane_num].size();
	double time = event.get_start_time();
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
			bg_read.start_time = time;
			bg_read.end_time = bg_read.start_time;
			bg_read.process = BACKGROUND;
			bg_read.op_complete_pointer = NULL;
			bg_read.end_time_pointer = NULL;
			background_events[plane_num].push_back(bg_read);
			struct ftl_event bg_write;
			bg_write.type = WRITE;
			bg_write.physical_address = cur_page_address;
			bg_write.logical_address = block_to_clean.page_mapping[i];
			bg_write.start_time = time;
			bg_write.end_time = bg_write.start_time;
			bg_write.process = BACKGROUND;
			bg_write.op_complete_pointer = NULL;
			bg_write.end_time_pointer = NULL;
			background_events[plane_num].push_back(bg_write);
			clean_pages_found = true;
		}
	}
	urgent_bg_events_location.rw_end_index = background_events[plane_num].size();
	urgent_bg_events_location.erase_index = background_events[plane_num].size();
	struct ftl_event bg_erase;
	bg_erase.type = ERASE;
	bg_erase.physical_address = block_to_clean.physical_address;
	bg_erase.logical_address = translate_pba_lba(block_to_clean.physical_address);
	bg_erase.start_time = time;
	bg_erase.end_time = bg_erase.start_time;
	bg_erase.process = BACKGROUND;
	bg_erase.op_complete_pointer = NULL;
	bg_erase.end_time_pointer = NULL;

	background_events[plane_num].push_back(bg_erase);
	filled_block_list.erase(max_benefit_block_reference);
	bg_cleaning_blocks[plane_num].push_back(block_to_clean);
	urgent_bg_events[plane_num].push_back(urgent_bg_events_location);
	return SUCCESS;
}


enum status FtlImpl_Page::garbage_collect_cached(Event &event)
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
			printf("This is the problem\n");
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

	struct ssd_block target_block = *max_benefit_block_reference;
	Address target_block_address = target_block.physical_address;
	unsigned int target_plane = target_block_address.package*PACKAGE_SIZE*DIE_SIZE + target_block_address.die*DIE_SIZE + target_block_address.plane;
	std::list<struct ssd_block>::iterator target_block_iterator;

	//TODO set the bg events correctly
	bool clean_pages_found = false;
	for(iter = allocated_block_list.begin();iter!=allocated_block_list.end();iter++)
	{
		Address iter_address = iter->physical_address;
		if( iter->physical_address.package == target_block_address.package && 
			iter->physical_address.die == target_block_address.die &&
			iter->physical_address.plane == target_block_address.plane
			)
		{
			Address cur_page_address = iter->physical_address;
			cur_page_address.valid = PAGE;
			for(unsigned int i=0;i<BLOCK_SIZE;i++)
			{
				cur_page_address.page = i;
				if(cur_page_address == logical_page_list[(iter->page_mapping)[i]].physical_address)
				{
					struct ftl_event bg_read;
					bg_read.type = READ;
					bg_read.physical_address = cur_page_address;
					bg_read.logical_address = (iter->page_mapping)[i];
					bg_read.start_time = event.get_start_time();
					bg_read.end_time = 0;
					background_events[target_plane].push_back(bg_read);
					clean_pages_found = true;
				}
			}
		}
	}

	struct urgent_bg_events_pointer urgent_bg_events_location;
	for(iter = filled_block_list.begin();iter!=filled_block_list.end();iter++)
	{
		bool schedule_writes = false;
		Address iter_address = iter->physical_address;
		if( iter->physical_address.package == target_block_address.package && 
			iter->physical_address.die == target_block_address.die &&
			iter->physical_address.plane == target_block_address.plane
			)
		{
			if(iter->physical_address.block == target_block_address.block)
			{
				target_block_iterator = iter;
				schedule_writes = true;
				urgent_bg_events_location.rw_start_index = background_events[target_plane].size();
			}
			Address cur_page_address = iter->physical_address;
			cur_page_address.valid = PAGE;
			for(unsigned int i=0;i<BLOCK_SIZE;i++)
			{
				cur_page_address.page = i;
				if(cur_page_address == logical_page_list[(iter->page_mapping)[i]].physical_address)
				{
					struct ftl_event bg_read;
					bg_read.type = READ;
					bg_read.physical_address = cur_page_address;
					bg_read.logical_address = (iter->page_mapping)[i];
					bg_read.start_time = event.get_start_time();
					bg_read.end_time = 0;
					background_events[target_plane].push_back(bg_read);
					if(schedule_writes)
					{
						struct ftl_event bg_write;
						bg_write.type = WRITE;
						bg_write.physical_address = cur_page_address;
						bg_write.logical_address = (iter->page_mapping)[i];
						bg_write.start_time = 0;
						bg_write.end_time = 0;
						background_events[target_plane].push_back(bg_write);
					}
					clean_pages_found = true;
				}
			}
			if(schedule_writes)
				urgent_bg_events_location.rw_end_index = background_events[target_plane].size();
		}
	}
	urgent_bg_events_location.erase_index = background_events[target_plane].size();
	struct ftl_event bg_erase;
	bg_erase.type = ERASE;
	bg_erase.physical_address = target_block_address;
	bg_erase.logical_address = translate_pba_lba(target_block_address);
	if(clean_pages_found)
		bg_erase.start_time = 0;
	else
		bg_erase.start_time = event.get_start_time();
	bg_erase.end_time = 0;

	background_events[target_plane].push_back(bg_erase);
	filled_block_list.erase(max_benefit_block_reference);
	bg_cleaning_blocks[target_plane].push_back(target_block);
	urgent_bg_events[target_plane].push_back(urgent_bg_events_location);

	return SUCCESS;

}


double FtlImpl_Page::process_background_tasks(Event &event)
{
	double ret_time = std::numeric_limits<double>::max();
	double cur_simulated_time = event.get_start_time();
	if(free_block_list.size() == 0)
	{
		urgent_cleaning = true;
	}
	for(unsigned int plane_num=0;plane_num<SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;plane_num++)
	{
		while(background_events[plane_num].size() > 0 && background_events[plane_num].front().start_time < cur_simulated_time)
		{
			struct ftl_event first_event = background_events[plane_num].front();
			costly_counter++;
			Address candidate_address = first_event.physical_address;
			bool write_address_already_open;
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
				//TODO write NOOP
				if(!(logical_page_list[first_event.logical_address].physical_address == first_event.physical_address))
				{
					background_events[plane_num].erase(background_events[plane_num].begin());
					if(background_events[plane_num].size() > 0)
						background_events[plane_num].front().start_time = first_event.start_time;
					continue;
				}
				Event probable_bg_write(WRITE, first_event.logical_address, 1, first_event.start_time);
				candidate_address = find_write_location(probable_bg_write, log_write_address, &write_address_already_open);
			}
			std::vector<struct ftl_event>::iterator iter;
			unsigned int candidate_plane = candidate_address.package*PACKAGE_SIZE*DIE_SIZE + candidate_address.die*DIE_SIZE + candidate_address.plane;
			iter=open_events[candidate_plane].begin();
			Address conflict_address = (*iter).physical_address;
			if( (*iter).start_time <= first_event.start_time &&
				(*iter).end_time > first_event.start_time 
				)
			{
				perform_first_task = false;
				if(background_events[plane_num].front().start_time < (*iter).end_time)
					background_events[plane_num].front().start_time = (*iter).end_time;	
				break;
			}
			
			if(perform_first_task && !(urgent_cleaning && first_event.type == WRITE ))
			{
				bool is_erase = false;
				double task_time = 0;
				if(first_event.type == READ)
				{
					Event bg_read(first_event.type, first_event.logical_address, 1, first_event.start_time);
					task_time = read_(bg_read, false);
					open_events[candidate_plane].push_back(first_event);
				}
				else if(first_event.type == WRITE)
				{
					Event bg_write(first_event.type, first_event.logical_address, 1, first_event.start_time);
					increment_log_write_address(bg_write, candidate_address, write_address_already_open, true); 
					first_event.physical_address = log_write_address;
					task_time = write_(bg_write, false);
					open_events[candidate_plane].push_back(first_event);
					if(free_block_list.size() == 0)
					{
						urgent_cleaning = true;
					}
				}
				else if(first_event.type == ERASE)
				{
					Event bg_erase(first_event.type, first_event.logical_address, 1, first_event.start_time);
					bg_erase.set_address(first_event.physical_address);
					controller.issue(bg_erase, false);
					task_time = bg_erase.get_time_taken();
					open_events[candidate_plane].push_back(first_event);
					struct ssd_block block_to_clean = bg_cleaning_blocks[plane_num].front();
					block_to_clean.valid_page_count = 0;
					block_to_clean.last_page_written = 0;
					block_to_clean.lifetime_left -= 1;
					free_block_list.push_back(block_to_clean);
					if(free_block_list.size() > clean_threshold)
					{
						gc_required = false;
					}
					bg_cleaning_blocks[plane_num].erase(bg_cleaning_blocks[plane_num].begin());
					urgent_bg_events[plane_num].erase(urgent_bg_events[plane_num].begin());
					is_erase = true;
					controller.stats.numErase++;
				}
			
				background_events[plane_num].erase(background_events[plane_num].begin());
				if(background_events[plane_num].size() > 0)
				{
					if(background_events[plane_num].front().type == READ && first_event.type == WRITE && !(first_event.physical_address == background_events[plane_num].front().physical_address))
						background_events[plane_num].front().start_time = first_event.start_time;
					else
						background_events[plane_num].front().start_time = first_event.start_time + task_time;
				}
				if(urgent_cleaning && is_erase)
				{
					//TODO low watermark
					urgent_cleaning = false;
				}
				
				std::vector<struct urgent_bg_events_pointer>::iterator urgent_pointer;
				for(urgent_pointer=urgent_bg_events[plane_num].begin();urgent_pointer!=urgent_bg_events[plane_num].end();urgent_pointer++)
				{
					if(urgent_pointer->rw_start_index > 0)
						urgent_pointer->rw_start_index--;
					if(urgent_pointer->rw_end_index > 0)
						urgent_pointer->rw_end_index--;
					if(urgent_pointer->erase_index > 0)
						urgent_pointer->erase_index--;
				}
				
			}
		}
		if(background_events[plane_num].size() > 0 && background_events[plane_num].front().start_time < ret_time)
			ret_time = background_events[plane_num].front().start_time;
	}
	if(urgent_cleaning)
		set_urgent_queues(event);
	return ret_time;
}

void FtlImpl_Page::set_urgent_queues(Event &event)
{

	unsigned int urgent_cleaning_plane;
	unsigned int min_ops_required = UINT_MAX;
	for(unsigned int plane_num=0;plane_num<SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;plane_num++)
	{
		unsigned int num_ops = urgent_bg_events[plane_num].front().rw_end_index - urgent_bg_events[plane_num].front().rw_start_index;
		if(num_ops < min_ops_required)
		{
			min_ops_required = num_ops;
			urgent_cleaning_plane = plane_num;
		}
	}	
	unsigned int plane_num = urgent_cleaning_plane;

	double time = event.get_total_time();


	double first_event_start_time = background_events[plane_num].front().start_time;
	
	struct urgent_bg_events_pointer urgent_bg_events_location = urgent_bg_events[plane_num].front();
	urgent_bg_events[plane_num].erase(urgent_bg_events[plane_num].begin());
	std::vector<struct ftl_event>::iterator begin_pointer = background_events[plane_num].begin();
	std::vector<struct ftl_event>::iterator rw_start_pointer = background_events[plane_num].begin();
	std::advance(rw_start_pointer, urgent_bg_events_location.rw_start_index);
	std::vector<struct ftl_event>::iterator rw_end_pointer = background_events[plane_num].begin();
	std::advance(rw_end_pointer, urgent_bg_events_location.rw_end_index);
	std::vector<struct ftl_event>::iterator erase_pointer = background_events[plane_num].begin();
	std::advance(erase_pointer, urgent_bg_events_location.erase_index);

	background_events[plane_num].erase(rw_end_pointer, erase_pointer);
	background_events[plane_num].erase(begin_pointer, rw_start_pointer);
	unsigned int num_elems_erased = (urgent_bg_events_location.erase_index - urgent_bg_events_location.rw_end_index) + urgent_bg_events_location.rw_start_index;
	std::vector<struct urgent_bg_events_pointer>::iterator urgent_pointer;
	for(urgent_pointer=urgent_bg_events[plane_num].begin();urgent_pointer!=urgent_bg_events[plane_num].end();urgent_pointer++)
	{
		if(urgent_pointer->rw_start_index > num_elems_erased)
			urgent_pointer->rw_start_index -= num_elems_erased;
		else
			urgent_pointer->rw_start_index = 0;
		if(urgent_pointer->rw_end_index > num_elems_erased)
			urgent_pointer->rw_end_index -= num_elems_erased;
		else
			urgent_pointer->rw_end_index = 0;
		if(urgent_pointer->erase_index > num_elems_erased)
			urgent_pointer->erase_index -= num_elems_erased;
		else
			urgent_pointer->erase_index = 0;
	}
	
	background_events[plane_num].front().start_time = first_event_start_time;
	bool first = true;
	unsigned int last_plane_num = plane_num;
	while(background_events[plane_num].size() > 0 && urgent_cleaning)
	{
		struct ftl_event first_event = background_events[plane_num].front();
		first_event.start_time = time;

		urgent_counter++;
		if(first_event.type != ERASE && 
			!(logical_page_list[first_event.logical_address].physical_address == first_event.physical_address) )
		{
			//TODO write NOOP
			background_events[plane_num].erase(background_events[plane_num].begin());
			if(background_events[plane_num].size() > 0)
				background_events[plane_num].front().start_time = first_event.start_time;
			continue;
		}
		bool is_erase = false;
		if(first_event.type == READ)
		{
			if(open_events[plane_num].size() > 0 && open_events[plane_num].back().end_time > time)
				first_event.start_time = open_events[plane_num].back().end_time;
			first_event.end_time = first_event.start_time;
			struct urgent_ftl_event *urgent_bg_read = (struct urgent_ftl_event *)malloc(sizeof(struct urgent_ftl_event));
			urgent_bg_read->event = first_event;
			urgent_bg_read->parent_completed = true;
			urgent_bg_read->predecessor_completed = urgent_queues[plane_num].size() == 0 ? true : false;
			urgent_bg_read->child = NULL;
			urgent_queues[plane_num].push_back(urgent_bg_read);
			last_plane_num = plane_num;
		}
		else if(first_event.type == WRITE)
		{
			Address candidate_address;
			bool write_address_already_open;
			Event probable_bg_write(WRITE, first_event.logical_address, 1, first_event.start_time);
			candidate_address = find_write_location(probable_bg_write, log_write_address, &write_address_already_open);
			increment_log_write_address(probable_bg_write, candidate_address, write_address_already_open, true);
			first_event.physical_address = log_write_address;
			unsigned int log_write_plane = log_write_address.package*PACKAGE_SIZE*DIE_SIZE + log_write_address.die*DIE_SIZE + log_write_address.plane;
			if(open_events[plane_num].size() > 0 && open_events[plane_num].back().end_time > time)
				first_event.start_time = open_events[plane_num].back().end_time;
			first_event.end_time = first_event.start_time;
			struct urgent_ftl_event *urgent_bg_write = (struct urgent_ftl_event *)malloc(sizeof(struct urgent_ftl_event));
			urgent_bg_write->event = first_event;
			urgent_bg_write->predecessor_completed = urgent_queues[log_write_plane].size() == 0 ? true : false;
			urgent_bg_write->parent_completed = first ? true : false;
			if(!first)
			{
				std::vector<struct urgent_ftl_event *>::iterator last_iterator = urgent_queues[last_plane_num].end();
				--last_iterator;
				struct urgent_ftl_event *last_event = *(last_iterator);
				last_event->child = urgent_bg_write;
			}
			urgent_bg_write->child = NULL;
			urgent_queues[log_write_plane].push_back(urgent_bg_write);
			last_plane_num = log_write_plane;
		}
		else if(first_event.type == ERASE)
		{
			if(open_events[plane_num].size() > 0 && open_events[plane_num].back().end_time > time)
				first_event.start_time = open_events[plane_num].back().end_time;
			first_event.end_time = first_event.start_time;
			struct urgent_ftl_event *urgent_bg_erase = (struct urgent_ftl_event *)malloc(sizeof(struct urgent_ftl_event));
			urgent_bg_erase->event = first_event;
			urgent_bg_erase->predecessor_completed = urgent_queues[plane_num].size() == 0 ? true : false;
			urgent_bg_erase->parent_completed = first ? true : false;
			if(!first)
			{
				std::vector<struct urgent_ftl_event *>::iterator last_iterator = urgent_queues[last_plane_num].end();
				--last_iterator;
				struct urgent_ftl_event *last_event = *(last_iterator);
				last_event->child = urgent_bg_erase;
			}
			urgent_bg_erase->child = NULL;
			urgent_queues[plane_num].push_back(urgent_bg_erase);
			struct ssd_block block_to_clean = bg_cleaning_blocks[plane_num].front();
			block_to_clean.valid_page_count = 0;
			block_to_clean.last_page_written = 0;
			block_to_clean.lifetime_left -= 1;
			free_block_list.push_back(block_to_clean);
			if(free_block_list.size() > clean_threshold)
			{
				gc_required = false;
			}
			bg_cleaning_blocks[plane_num].erase(bg_cleaning_blocks[plane_num].begin());
			is_erase = true;
			last_plane_num = plane_num;
		}
	
		background_events[plane_num].erase(background_events[plane_num].begin());
		if(urgent_cleaning && is_erase)
		{
			//TODO low watermark
			urgent_cleaning = false;
			break;
		}
		for(urgent_pointer=urgent_bg_events[plane_num].begin();urgent_pointer!=urgent_bg_events[plane_num].end();urgent_pointer++)
		{
			if(urgent_pointer->rw_start_index > 0)
				urgent_pointer->rw_start_index--;
			if(urgent_pointer->rw_end_index > 0)
				urgent_pointer->rw_end_index--;
			if(urgent_pointer->erase_index > 0)
				urgent_pointer->erase_index--;
		}
		first = false;
	}
}


double FtlImpl_Page::process_urgent_queues(Event &event)
{
	double time = event.get_start_time();
	double ret_time = std::numeric_limits<double>::max();
	for(unsigned int plane_num = 0;plane_num < SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;plane_num++)
	{
		//TODO check strict inequality
		std::vector<struct urgent_ftl_event *>::iterator first_iterator = urgent_queues[plane_num].begin();
		struct urgent_ftl_event *first_pointer = *(first_iterator);
		while(	first_pointer && 
				first_pointer->parent_completed && 
				first_pointer->event.start_time <= time &&
				first_pointer->predecessor_completed)
		{
			struct ftl_event first_event = first_pointer->event;
			Event e(first_event.type, first_event.logical_address, 1, first_event.start_time);		
			e.set_address(first_event.physical_address);
			double next_event_time;
			if(first_event.type == ERASE)
			{
				controller.issue(e, false);
				next_event_time = e.get_total_time();
			}
			else if(first_event.type == READ)
			{
				next_event_time = read_(e, false);			
			}
			else if(first_event.type == WRITE)
			{
				next_event_time = write_(e, false);
			}

			if(first_pointer->child)
			{
				first_pointer->child->parent_completed = true;
				if(first_pointer->child->event.start_time < next_event_time)
					first_pointer->child->event.start_time = next_event_time;
			}
			urgent_queues[plane_num].erase(urgent_queues[plane_num].begin());
			if(urgent_queues[plane_num].size() > 0)
			{
				first_pointer = *(urgent_queues[plane_num].begin());
				first_pointer->event.start_time = next_event_time;
				first_pointer->predecessor_completed = true;
			}
			else
			{
				first_pointer = NULL;
			}
		}
		if(first_pointer && first_pointer->event.start_time < ret_time)
			ret_time = first_pointer->event.start_time;
	}
	return ret_time;
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
		else if ((*iter).start_time > time)
		{
			break;
		}
	}
}


