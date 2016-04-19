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


unsigned int app_writes = 0;
unsigned int total_writes = 0;

using namespace ssd;

FtlImpl_Page::FtlImpl_Page(Controller &controller, Ssd &parent):FtlParent(controller, parent), 
	latest_write_time(0), 
	gc_required(false),
	RAW_SSD_BLOCKS(SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE),
	ADDRESSABLE_SSD_PAGES(NUMBER_OF_ADDRESSABLE_BLOCKS * BLOCK_SIZE),
	clean_threshold(((SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE) - NUMBER_OF_ADDRESSABLE_BLOCKS)/2),
	READ_PREFERENCE(true),
	//open_events(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE), 
	background_events(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE), 
	ftl_queues(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE),
	bg_cleaning_blocks(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE), 
	required_bg_events(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE),
	plane_free_times(SSD_SIZE*PACKAGE_SIZE*DIE_SIZE)
{
	logical_page_list = new logical_page[ADDRESSABLE_SSD_PAGES];
	for (unsigned int i=0;i<ADDRESSABLE_SSD_PAGES;i++)
	{
		logical_page_list[i].physical_address.valid = NONE;
		logical_page_list[i].write_time = -1;
	}
	unsigned int logical_page_list_index = 0;
	allocated_block_list.clear();
	free_block_list.clear();
	filled_block_list.clear();
	unsigned int next_block_lba = 0;
	unsigned int stripe_set_remaining[SSD_SIZE*PACKAGE_SIZE*DIE_SIZE];
	unsigned int last_lba_mapped[SSD_SIZE*PACKAGE_SIZE*DIE_SIZE];
	bool plane_encountered_before[SSD_SIZE*PACKAGE_SIZE*DIE_SIZE];
	unsigned int plane_map[SSD_SIZE*PACKAGE_SIZE*DIE_SIZE];
	for(unsigned int i=0;i<SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;i++)
	{
		stripe_set_remaining[i] = STRIPE_SIZE;
		last_lba_mapped[i] = 0;
		plane_encountered_before[i] = false;
		plane_map[i] = 0;
	}
	unsigned int _effective_plane = 0;
	for(unsigned int i=0;i<RAW_SSD_BLOCKS;i++)
	{
		struct ssd_block new_ssd_block;
		new_ssd_block.physical_address = translate_lba_pba(next_block_lba);
		new_ssd_block.physical_address.valid = BLOCK;
		new_ssd_block.valid_page_count = 0;
		new_ssd_block.lifetime_left = BLOCK_ERASES;
		new_ssd_block.page_copy_complete_time = -1;
		new_ssd_block.page_mapping = (unsigned int *)malloc(BLOCK_SIZE * sizeof(unsigned int));
		new_ssd_block.reserved_page_count = 0;
		if(!new_ssd_block.page_mapping)
			assert(false);
		for(unsigned int j=0;j<BLOCK_SIZE;j++)
		{
			new_ssd_block.page_mapping[j] = 0;
		}
		new_ssd_block.page_to_write = 0;
		new_ssd_block.scheduled_for_erasing = false;
		free_block_list.push_back(new_ssd_block);
		free(new_ssd_block.page_mapping);
		new_ssd_block.page_mapping = NULL;
		if(STRIPE_SIZE > 0)
		{
			unsigned int plane_num = new_ssd_block.physical_address.package*PACKAGE_SIZE*DIE_SIZE + new_ssd_block.physical_address.die*DIE_SIZE + new_ssd_block.physical_address.plane;
			if(!plane_encountered_before[plane_num])
			{
				plane_map[plane_num] = _effective_plane;
			}
			unsigned int effective_plane_num = plane_map[plane_num];
			unsigned int next_lba_to_map = 0;
			unsigned int num_planes = SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;
			if(!plane_encountered_before[plane_num])
			{
				next_lba_to_map = effective_plane_num * STRIPE_SIZE;
			}
			else
			{
				if(stripe_set_remaining[plane_num] == STRIPE_SIZE)
				{
					next_lba_to_map = last_lba_mapped[plane_num] - (STRIPE_SIZE - 1) + num_planes*STRIPE_SIZE;
					stripe_set_remaining[plane_num] = STRIPE_SIZE;
				}
				else
				{
					next_lba_to_map = last_lba_mapped[plane_num] + 1;
				}
			}
		
			Address cur_address = new_ssd_block.physical_address;
			cur_address.valid = PAGE;

			for(unsigned int j=0;j<BLOCK_SIZE;j++)
			{
				if(next_lba_to_map >= ADDRESSABLE_SSD_PAGES)
				{
					break;
				}
				cur_address.page = j;
				new_ssd_block.page_mapping[j] = next_lba_to_map;
				new_ssd_block.page_to_write = j+1;
				new_ssd_block.valid_page_count++;
				logical_page_list[next_lba_to_map].physical_address = cur_address;
				logical_page_list[next_lba_to_map].write_time = 0;
				last_lba_mapped[plane_num] = next_lba_to_map;
				stripe_set_remaining[plane_num]--;
				if(stripe_set_remaining[plane_num] != 0)
				{
					next_lba_to_map = last_lba_mapped[plane_num] + 1;
				}
				else
				{
					next_lba_to_map = last_lba_mapped[plane_num] - (STRIPE_SIZE - 1) + num_planes*STRIPE_SIZE;
					stripe_set_remaining[plane_num] = STRIPE_SIZE;
				}
			}
			if(new_ssd_block.valid_page_count > 0)
			{
				if(new_ssd_block.page_to_write != BLOCK_SIZE)
				{
					allocated_block_list.push_back(new_ssd_block);
				}
				else
				{
					filled_block_list.push_back(new_ssd_block);
				}
				std::list<struct ssd_block>::iterator last_free_block_pointer = free_block_list.end();
				last_free_block_pointer--;
				free_block_list.erase(last_free_block_pointer);
			}
			plane_encountered_before[plane_num] = true;
			_effective_plane++;
		}
		next_block_lba = get_next_block_lba(next_block_lba);
		if(next_block_lba == 0)
			break;
	}
	log_write_address.valid = NONE;
	low_watermark = MAX_BLOCKS_PER_GC;
	//low_watermark = 1;

	ftl_queue_last_bg_event_index = (unsigned int *)malloc(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * sizeof(unsigned int));
	ftl_queue_has_bg_event = (bool *)malloc(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * sizeof(bool));
	cleaning_queued = (unsigned int *)malloc(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * sizeof(unsigned int));
	plane_prioritized_till = (double *)malloc(SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * sizeof(double));
	for(unsigned int i=0;i<SSD_SIZE * PACKAGE_SIZE * DIE_SIZE;i++)
	{
		plane_free_times[i].first = 0;
		plane_free_times[i].second = 0;
		ftl_queue_last_bg_event_index[i] = 0;
		ftl_queue_has_bg_event[i] = false;
		cleaning_queued[i] = 0;
		plane_prioritized_till[i] = -1;
	}


	bg_events_time = -1;
	next_event_time = -1;
	
	for(unsigned int i=0;i<SSD_SIZE * PACKAGE_SIZE * DIE_SIZE;i++)
	{
		background_events[i].reserve(BLOCK_SIZE * PLANE_SIZE);
		//bg_cleaning_blocks[i].reserve(PLANE_SIZE);
		//required_bg_events[i].reserve(BLOCK_SIZE);
		//ftl_queues[i].reserve(BLOCK_SIZE);
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

double FtlImpl_Page::get_average_age(struct ssd_block &block)
{
	if(block.scheduled_for_erasing)
		assert(false);
	double average_age = 0;
	Address supposed_address = block.physical_address;
	for(unsigned int i=0;i<BLOCK_SIZE;i++)
	{
		Address actual_address = logical_page_list[block.page_mapping[i]].physical_address;
		supposed_address.page = i;
		supposed_address.valid = PAGE;
		if(actual_address == supposed_address)
		{
			unsigned int write_time = logical_page_list[block.page_mapping[i]].write_time;
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


Address FtlImpl_Page::find_write_location(double time, Address cur, bool *already_open)
{
	Address ret_address;
	Address possible_ret_address;
	ret_address.valid = NONE;

	std::list<struct ssd_block>::iterator iter, min_queue_iter;
	double min_wait_time = 0;
	min_queue_iter = allocated_block_list.end();
	bool found_block = false;

	for(iter=allocated_block_list.begin();iter!=allocated_block_list.end();)
	{
		if((*iter).page_to_write == BLOCK_SIZE && !(*iter).scheduled_for_erasing)
		{
			struct ssd_block filled_block;
			filled_block.physical_address = iter->physical_address;
			//filled_block.last_write_time = iter->last_write_time;
			filled_block.valid_page_count = iter->valid_page_count;
			filled_block.lifetime_left = iter->lifetime_left;
			filled_block.page_to_write = iter->page_to_write;
			filled_block.page_copy_complete_time = iter->page_copy_complete_time;
			filled_block.scheduled_for_erasing = iter->scheduled_for_erasing;
			filled_block.page_mapping = (unsigned int *)malloc(BLOCK_SIZE * sizeof(unsigned int));
			filled_block.reserved_page_count = iter->reserved_page_count;
			for(unsigned int k=0;k<BLOCK_SIZE;k++)
			{
				filled_block.page_mapping[k] = (iter->page_mapping)[k];
			}
			//printf("pushing to filled from find with address ");
			//filled_block.physical_address.print();
			//printf("\n");
			filled_block_list.push_back(filled_block);
			iter = allocated_block_list.erase(iter);
			free(filled_block.page_mapping);
			filled_block.page_mapping = NULL;
			continue;
		}
		if(iter->page_to_write + iter->reserved_page_count == BLOCK_SIZE)
		{
			iter++;
			continue;
		}
		Address candidate_address = (*iter).physical_address;
		unsigned int plane_num = candidate_address.package*PACKAGE_SIZE*DIE_SIZE + candidate_address.die*DIE_SIZE + candidate_address.plane;
		double wait_time = plane_free_times[plane_num].second;
		if(wait_time < min_wait_time || !found_block)
		{
			min_queue_iter = iter;
			min_wait_time = wait_time;
			found_block = true;
			if(min_wait_time <= time)
				break;
		}
		iter++;
	}
	if(min_queue_iter != allocated_block_list.end())
	{
		ret_address = (*min_queue_iter).physical_address;
		assert(min_queue_iter->page_to_write + min_queue_iter->reserved_page_count != BLOCK_SIZE);
		ret_address.page = min_queue_iter->page_to_write + min_queue_iter->reserved_page_count;
		ret_address.valid = PAGE;
		*already_open = true;
	}
	if(!(found_block && min_wait_time <= time))
	{
		std::list<struct ssd_block>::iterator free_iter, min_iter = free_block_list.end();
		for(free_iter=free_block_list.begin();free_iter!=free_block_list.end();free_iter++)
		{
			Address candidate_address = (*free_iter).physical_address;
			unsigned int plane_num = candidate_address.package*PACKAGE_SIZE*DIE_SIZE + candidate_address.die*DIE_SIZE + candidate_address.plane;
			double wait_time = plane_free_times[plane_num].second;
			if(wait_time < min_wait_time || !found_block)
			{
				min_iter = free_iter;
				min_wait_time = wait_time;
				found_block = true;
				if(min_wait_time <= time)
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

bool FtlImpl_Page::increment_log_write_address(double time, Address asked_for, bool already_allocated)
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
		next_write_address = find_write_location(time, log_write_address, &already_open);
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
		assert(new_ssd_block.page_copy_complete_time == -1);
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
			assert(false);
		struct ssd_block new_ssd_block = (*req_iter);
		assert(new_ssd_block.page_copy_complete_time == -1);
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

bool compare_possible_erase_blocks(const std::pair<unsigned int, float> a, const std::pair<unsigned int, float> b)
{
	return a.second > b.second;
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
	allocated_block_list.clear();
	free_block_list.clear();
	filled_block_list.clear();
	for(unsigned int l=0;l<SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;l++)
	{
		bg_cleaning_blocks[l].clear();
		required_bg_events[l].clear();
		background_events[l].clear();
		std::vector<struct queued_ftl_event *>::iterator i;
		for(i=ftl_queues[l].begin();i!=ftl_queues[l].end();i++)
		{
			delete (*i);
		}
		ftl_queues[l].clear();
	}
	bg_cleaning_blocks.clear();
	required_bg_events.clear();
	background_events.clear();
	ftl_queues.clear();
	free(ftl_queue_has_bg_event);
	free(ftl_queue_last_bg_event_index);
	delete[] logical_page_list;
}

enum status FtlImpl_Page::read(Event &event, bool &op_complete, double &end_time)
{
	unsigned int logical_page_num = event.get_logical_address();
	unset_plane_priorities(event.get_start_time());
	next_event_time = process_ftl_queues(event);
	bg_events_time = process_background_tasks(event);
	process_waiting_events(event.get_start_time());
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
	if(ssd.cache.present_in_cache(event))
	{
		end_time = read_(event);
		op_complete = true;
		return SUCCESS;
	}

	unsigned int plane_num = read_address.package*PACKAGE_SIZE*DIE_SIZE + read_address.die*DIE_SIZE + read_address.plane;
	struct ftl_event fg_read;
	fg_read.type = READ;
	fg_read.logical_address = event.get_logical_address();
	fg_read.physical_address = read_address;
	fg_read.process = FOREGROUND;
	fg_read.op_complete_pointer = &op_complete;
	fg_read.end_time_pointer = &end_time;
	fg_read.start_time = event.get_start_time();
	//printf("READ ");
	//fg_read.physical_address.print();
	//printf(" %f\n", fg_read.start_time);
	if(READ_PREFERENCE)
	{
		//if(fg_read.start_time < plane_free_times[plane_num].first)
		//	fg_read.start_time = plane_free_times[plane_num].first;
		fg_read.start_time = event.get_start_time();
		fg_read.end_time = fg_read.start_time + PAGE_READ_DELAY;
		plane_free_times[plane_num].second += PAGE_READ_DELAY;
		struct queued_ftl_event *stalled_fg_read = new queued_ftl_event();
		stalled_fg_read->event = fg_read;
		stalled_fg_read->child = NULL;
		stalled_fg_read->parent_completed = true;
		std::vector<struct queued_ftl_event *>::iterator find_location = ftl_queues[plane_num].begin();
		int counter = 0;
		while(find_location != ftl_queues[plane_num].end() && (*find_location)->event.type == READ && (*find_location)->event.process == FOREGROUND)
		{
			counter++;
			find_location++;
		}
		printf("read arrived at %f for %d\n", event.get_start_time(), event.get_logical_address());
		printf("waiting behind %d reads\n", counter);
		stalled_fg_read->predecessor_completed = find_location == ftl_queues[plane_num].begin() ? true : false;
		ftl_queues[plane_num].insert(find_location, stalled_fg_read);
		if( ftl_queue_has_bg_event[plane_num] && 
			std::distance(ftl_queues[plane_num].begin(), find_location) >= ftl_queue_last_bg_event_index[plane_num])
			ftl_queue_last_bg_event_index[plane_num]++;
	}
	else
	{
		if(fg_read.start_time < plane_free_times[plane_num].first)
			fg_read.start_time = plane_free_times[plane_num].first;
		fg_read.end_time = fg_read.start_time + PAGE_READ_DELAY;
		plane_free_times[plane_num].second = fg_read.end_time;
		struct queued_ftl_event *stalled_fg_read = new queued_ftl_event();
		stalled_fg_read->event = fg_read;
		stalled_fg_read->child = NULL;
		stalled_fg_read->parent_completed = true;
		stalled_fg_read->predecessor_completed = ftl_queues[plane_num].size() == 0 ? true : false;
		ftl_queues[plane_num].push_back(stalled_fg_read);
	}
	next_event_time = process_ftl_queues(event);
	bg_events_time = process_background_tasks(event);
	return SUCCESS;
}


enum status FtlImpl_Page::noop(Event &event, bool &op_complete, double &end_time)
{
	double e_time = std::numeric_limits<double>::max();
	unset_plane_priorities(event.get_start_time());
	process_waiting_events(event.get_start_time());
	next_event_time = process_ftl_queues(event);
	bg_events_time = process_background_tasks(event);
	process_waiting_events(event.get_start_time());
	next_event_time = process_ftl_queues(event);
	e_time = next_event_time < e_time ? next_event_time : e_time;
	event.incr_time_taken(e_time - event.get_start_time());
	end_time = e_time;
	op_complete = true;
	//printf("NOOP returning with time ");
	//if(end_time == std::numeric_limits<double>::max())
	//	printf("MAX");
	//else
	//	printf("%f", end_time);
	//printf("\n");
	return SUCCESS;
}

enum status FtlImpl_Page::write(Event &event, bool &op_complete, double &end_time)
{
	printf("op complete in FTL is %d at address %p\n", op_complete, &op_complete);
	unset_plane_priorities(event.get_start_time());
	next_event_time = process_ftl_queues(event);
	bg_events_time = process_background_tasks(event);
	struct ftl_event fg_write;
	fg_write.type = WRITE;
	fg_write.logical_address = event.get_logical_address();
	fg_write.process = FOREGROUND;
	fg_write.op_complete_pointer = &op_complete;
	fg_write.end_time_pointer = &end_time;
	fg_write.start_time = event.get_start_time();
	waiting_events_queue.push_back(fg_write);
	process_waiting_events(event.get_start_time());
	next_event_time = process_ftl_queues(event);
	bg_events_time = process_background_tasks(event);
	return SUCCESS;
}


//enum status FtlImpl_Page::write(Event &event, bool &op_complete, double &end_time)
void FtlImpl_Page::process_waiting_events(double time)
{
	while(waiting_events_queue.size() > 0)
	{
		struct ftl_event fg_write = waiting_events_queue.front();
		fg_write.start_time = time;
		Address cur_address;
		cur_address.valid = NONE;
		unsigned int pre_fbl_size = free_block_list.size();
		if(!increment_log_write_address(fg_write.start_time, cur_address, false))
		{
			if(garbage_collect(fg_write.start_time))
			{
				printf("calling queue required from process waiting 1\n");
				queue_required_bg_events(fg_write.start_time, true);
			}
			break; 
		} 
		unsigned int post_fbl_size = free_block_list.size();
		Address log_write_block_address = log_write_address;
		log_write_block_address.page = 0;
		log_write_block_address.valid = BLOCK;
		std::list<struct ssd_block>::iterator write_block_iter;
		for(write_block_iter=allocated_block_list.begin();write_block_iter!=allocated_block_list.end();write_block_iter++)
		{
			if(write_block_iter->physical_address == log_write_block_address)
				break;
		}
		assert(write_block_iter != allocated_block_list.end());
		bool wait_for_erase = write_block_iter->scheduled_for_erasing;
		Address write_address = log_write_address;
		fg_write.physical_address = write_address;
		unsigned int plane_num = write_address.package*PACKAGE_SIZE*DIE_SIZE + write_address.die*DIE_SIZE + write_address.plane;
		if(fg_write.start_time < plane_free_times[plane_num].first)
			fg_write.start_time = plane_free_times[plane_num].first;
		fg_write.end_time = fg_write.start_time + PAGE_WRITE_DELAY;
		plane_free_times[plane_num].second += PAGE_WRITE_DELAY;
		fg_write.end_time = fg_write.start_time;
		mark_reserved(fg_write.physical_address, true);
		struct queued_ftl_event *stalled_fg_write = new queued_ftl_event();
		stalled_fg_write->event = fg_write;
		stalled_fg_write->child = NULL;
		stalled_fg_write->parent_completed = true;
		stalled_fg_write->write_from_address = logical_page_list[fg_write.logical_address].physical_address; 
		std::vector<struct queued_ftl_event *>::reverse_iterator find_location = ftl_queues[plane_num].rbegin();
		while(find_location != ftl_queues[plane_num].rend() && (*find_location)->event.process == BACKGROUND)
		{
			if(wait_for_erase)
			{
				struct queued_ftl_event *check_event = (*((find_location+1).base()));
				if(check_event->event.type == ERASE && check_event->event.physical_address == log_write_block_address)
				{
					break;
				}
			}
			find_location++;
		}
		std::vector<struct queued_ftl_event *>::iterator insert_location = find_location.base();
		stalled_fg_write->predecessor_completed = insert_location == ftl_queues[plane_num].begin() ? true : false;
		ftl_queues[plane_num].insert(find_location.base(), stalled_fg_write);
		printf("Application write %d at %f is begin q-ed on plane %d ", fg_write.logical_address, fg_write.start_time, plane_num);
		log_write_address.print();
		printf("\n");
		app_writes++;
		if(gc_required)
		{
			bool gc_ret = garbage_collect(fg_write.start_time);
			if(post_fbl_size < low_watermark)// && pre_fbl_size >= low_watermark) 
			{
				printf("calling queue required from process waiting 2 fbl is %d\n", post_fbl_size);
				if(!queue_required_bg_events(fg_write.start_time, true))
				{
					//break;
				}
			}
		}
		waiting_events_queue.erase(waiting_events_queue.begin());
	}
}


double FtlImpl_Page::read_(Event &event)
{
	controller.issue(event);
	return event.get_total_time();
}

double FtlImpl_Page::write_(Event &event)
{
	unsigned int logical_page_num = event.get_logical_address();
	Address write_address = event.get_address();
	printf("Received write on ");
	write_address.print();
	printf("\n");
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
	if(need_invalidation)
	{
		for(iter=free_block_list.begin();iter!=free_block_list.end();iter++)
		{
			if(iter->physical_address == currently_mapped_block_address)
			{
				assert(iter->scheduled_for_erasing);
				iter->valid_page_count -= 1;
				need_invalidation = false;
				break;
			}
		}
	}
	if(need_invalidation)
	{
		unsigned int currently_mapped_plane = currently_mapped_block_address.package*PACKAGE_SIZE*DIE_SIZE + currently_mapped_block_address.die*DIE_SIZE + currently_mapped_block_address.plane;
		std::vector<struct ssd_block>::iterator bg_iter;
		for(bg_iter=bg_cleaning_blocks[currently_mapped_plane].begin();bg_iter!=bg_cleaning_blocks[currently_mapped_plane].end();bg_iter++)
		{
			if(bg_iter->physical_address == currently_mapped_block_address)
			{
				bg_iter->valid_page_count -= 1;
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
	assert(currently_mapped_address.valid != PAGE || !need_invalidation);
	assert(log_write_iter != allocated_block_list.end());

	unsigned int write_address_page = write_address.page;
	write_address.page = (*log_write_iter).page_to_write;

	event.set_address(write_address);
	printf("Issuing write for %d on ", logical_page_num);
	write_address.print();
	printf("\n");
	fflush(stdout);
	controller.issue(event);
	logical_page_list[logical_page_num].physical_address = write_address;
	logical_page_list[logical_page_num].write_time = latest_write_time++;
	//(*log_write_iter).last_write_time = latest_write_time++;    
	(*log_write_iter).valid_page_count += 1;
	(*log_write_iter).page_mapping[write_address.page] = logical_page_num;  
	mark_reserved(write_address, false);
	(*log_write_iter).page_to_write = write_address.page + 1;
	printf("set the page to write to %d and reserved to %d\n", log_write_iter->page_to_write, log_write_iter->reserved_page_count);
	total_writes++;
	return event.get_total_time();
}

enum status FtlImpl_Page::trim(Event &event)
{
	return SUCCESS;
}


enum status FtlImpl_Page::garbage_collect(double time)
{
	enum status ret_status;
	unsigned int total_bg_cleaning_blocks = 0;
	unsigned int total_bg_cleaning_planes = 0;

	printf("GC catch all\n");
	for(unsigned int i=0;i<SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;i++)
	{
		total_bg_cleaning_blocks += bg_cleaning_blocks[i].size();
		if(bg_cleaning_blocks[i].size() > 0)
		{
			total_bg_cleaning_planes++;
		}
	}
	if(MAX_GC_BLOCKS > 0 && total_bg_cleaning_blocks >= MAX_GC_BLOCKS)
	{
		return FAILURE;
	}
	//if(MAX_GC_PLANES > 0 && GC_SCHEME == 1 && total_bg_cleaning_planes >= MAX_GC_PLANES)
	//{
	//	return FAILURE;
	//}
	switch(GC_SCHEME)
	{
		case(0):
			ret_status = garbage_collect_default(time);
			break;
		case(1):
			ret_status = garbage_collect_cached(time);
			break;
		default:
			ret_status = garbage_collect_default(time);
			break;
	}
	return ret_status;
}


enum status FtlImpl_Page::garbage_collect_default(double time)
{
	std::list<struct ssd_block>::iterator iter;
	std::list<struct ssd_block>::iterator max_benefit_block_reference = filled_block_list.end();
	float max_benefit = 0, cur_benefit;
	bool cleaning_possible = false;


	for(iter=allocated_block_list.begin();iter!=allocated_block_list.end();)
	{
		if(iter->scheduled_for_erasing)
		{
			iter++;
			continue;
		}
		if(iter->page_to_write == BLOCK_SIZE)
		{
			struct ssd_block filled_block;
			filled_block.physical_address = iter->physical_address;
			//filled_block.last_write_time = iter->last_write_time;
			filled_block.valid_page_count = iter->valid_page_count;
			filled_block.lifetime_left = iter->lifetime_left;
			filled_block.page_to_write = iter->page_to_write;
			filled_block.scheduled_for_erasing = iter->scheduled_for_erasing;
			filled_block.page_copy_complete_time = iter->page_copy_complete_time;
			filled_block.page_mapping = (unsigned int *)malloc(BLOCK_SIZE * sizeof(unsigned int));
			filled_block.reserved_page_count = iter->reserved_page_count;
			for(unsigned int k=0;k<BLOCK_SIZE;k++)
			{
				filled_block.page_mapping[k] = (iter->page_mapping)[k];
			}
			filled_block_list.push_back(filled_block);
			iter = allocated_block_list.erase(iter);
			free(filled_block.page_mapping);
			filled_block.page_mapping = NULL;
			continue;
		}
		else
		{
			iter++;
		}
	}
	

	cleaning_possible = false;

	bool found_valid = false;
	for(iter=filled_block_list.begin();iter!=filled_block_list.end();iter++)
	{
		float utilization = (float)iter->valid_page_count/(float)BLOCK_SIZE;
		if(iter->valid_page_count == BLOCK_SIZE)
		{
			continue;
		}
		found_valid = true;
		double age = get_average_age(*iter);
		cur_benefit = (1.0 - utilization)*(float)age / (1.0 + utilization);
		Address possible_address = iter->physical_address;
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
		//printf("cleaning is not possible %d\n", found_valid);
		return FAILURE;
	} 
	

	
	struct ssd_block block_to_clean = *max_benefit_block_reference;
	Address cur_page_address = block_to_clean.physical_address;
	unsigned int plane_num = cur_page_address.package*PACKAGE_SIZE*DIE_SIZE + cur_page_address.die*DIE_SIZE + cur_page_address.plane;
	struct required_bg_events_pointer required_bg_events_location;
	required_bg_events_location.rw_start_index = background_events[plane_num].size();
	for(unsigned int i=0;i<BLOCK_SIZE;i++)
	{
		cur_page_address.page = i;
		cur_page_address.valid = PAGE; 
		if(cur_page_address == logical_page_list[block_to_clean.page_mapping[i]].physical_address)
		{
			required_bg_events_location.rw_end_index = background_events[plane_num].size();
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
		}
	}
	required_bg_events_location.rw_end_index = background_events[plane_num].size();
	required_bg_events_location.erase_index = background_events[plane_num].size();
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
	assert(required_bg_events_location.rw_end_index == required_bg_events_location.erase_index);
	required_bg_events[plane_num].push_back(required_bg_events_location);
	return SUCCESS;
}


enum status FtlImpl_Page::garbage_collect_cached(double time)
{
	printf("GC cached\n");
	std::list<struct ssd_block>::iterator iter;
	//std::list<struct ssd_block>::iterator max_benefit_block_reference = filled_block_list.end();
	float max_benefit = 0, cur_benefit;
	unsigned int max_benefit_plane = SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;
	bool cleaning_possible = false;

	for(iter=allocated_block_list.begin();iter!=allocated_block_list.end();)
	{
		if(iter->scheduled_for_erasing)
		{
			iter++;
			continue;
		}
		if(iter->page_to_write == BLOCK_SIZE)
		{
			struct ssd_block filled_block;
			filled_block.physical_address = iter->physical_address;
			//filled_block.last_write_time = iter->last_write_time;
			filled_block.valid_page_count = iter->valid_page_count;
			filled_block.lifetime_left = iter->lifetime_left;
			filled_block.page_to_write = iter->page_to_write;
			filled_block.scheduled_for_erasing = iter->scheduled_for_erasing;
			filled_block.page_copy_complete_time = iter->page_copy_complete_time;
			filled_block.page_mapping = (unsigned int *)malloc(BLOCK_SIZE * sizeof(unsigned int));
			filled_block.reserved_page_count = iter->reserved_page_count;
			for(unsigned int k=0;k<BLOCK_SIZE;k++)
			{
				filled_block.page_mapping[k] = (iter->page_mapping)[k];
			}
			//printf("pushing to filled with address ");
			//filled_block.physical_address.print();
			//printf("\n");
			filled_block_list.push_back(filled_block);
			iter = allocated_block_list.erase(iter);
			continue;
		}
		else
		{
			iter++;
		}
	}
	cleaning_possible = false;

	double plane_valid_page_count[SSD_SIZE*PACKAGE_SIZE*DIE_SIZE];
	double plane_average_age[SSD_SIZE*PACKAGE_SIZE*DIE_SIZE];
	unsigned int num_possible_blocks[SSD_SIZE*PACKAGE_SIZE*DIE_SIZE];

	std::vector<std::pair<unsigned int, float>> possible_erase_blocks[SSD_SIZE*PACKAGE_SIZE*DIE_SIZE];

	for(unsigned int p_num = 0;p_num < SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;p_num++)
	{
		plane_valid_page_count[p_num] = 0;
		plane_average_age[p_num] = 0;
		num_possible_blocks[p_num] = 0;
	}

	for(iter=filled_block_list.begin();iter!=filled_block_list.end();iter++)
	{
		Address cur_address = iter->physical_address;
		unsigned int cur_plane_num = cur_address.package*PACKAGE_SIZE*DIE_SIZE + cur_address.die*DIE_SIZE + cur_address.plane;
		plane_valid_page_count[cur_plane_num] += iter->valid_page_count;
		plane_average_age[cur_plane_num] += get_average_age(*iter);

		float utilization = (float)iter->valid_page_count/(float)BLOCK_SIZE;
		double age = get_average_age(*iter);
		cur_benefit = (1.0 - utilization)*(float)age / (1.0 + utilization);
		if(iter->valid_page_count == BLOCK_SIZE || bg_cleaning_blocks[cur_plane_num].size() > 0)
		{
			cur_benefit = 0;
		}
		else
		{
			num_possible_blocks[cur_plane_num]++;
		}
		possible_erase_blocks[cur_plane_num].push_back(std::pair<unsigned int, float>(std::distance(filled_block_list.begin(), iter), cur_benefit));
	}

	for(unsigned int p_num = 0;p_num < SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;p_num++)
	{
		if(num_possible_blocks[p_num] < MIN_BLOCKS_PER_GC)
			continue;
		plane_valid_page_count[p_num] = (double)plane_valid_page_count[p_num]/((double)(PLANE_SIZE * BLOCK_SIZE));
		plane_average_age[p_num] = (double)plane_average_age[p_num]/(double)PLANE_SIZE;
		if(plane_valid_page_count[p_num] == 1)
			continue;
		cur_benefit = (1.0 - plane_valid_page_count[p_num]) * plane_average_age[p_num] /(1.0 + plane_valid_page_count[p_num]);
		if(cur_benefit > 0 && (max_benefit == 0 || cur_benefit > max_benefit))
		{
			max_benefit = cur_benefit;
			max_benefit_plane = p_num; 
			cleaning_possible = true;
		}    
	}
	if(!cleaning_possible)
	{
		printf("cleaning is not possible\n");
		return FAILURE;
	} 
	unsigned int target_plane = max_benefit_plane;
	//printf("GC target plane is %d\n", target_plane);
	for(iter = allocated_block_list.begin();iter!=allocated_block_list.end();iter++)
	{
		Address cur_address = iter->physical_address;
		unsigned int cur_plane = cur_address.package*PACKAGE_SIZE*DIE_SIZE + cur_address.die*DIE_SIZE + cur_address.plane;
		if(cur_plane == target_plane)
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
					bg_read.start_time = time;
					bg_read.end_time = 0;
					bg_read.process = BACKGROUND;
					bg_read.op_complete_pointer = NULL;
					bg_read.end_time_pointer = NULL;
					background_events[target_plane].push_back(bg_read);
				}
			}
		}
	}


	std::vector<unsigned int> erase_block_list;
	std::sort(possible_erase_blocks[target_plane].begin(), possible_erase_blocks[target_plane].end(), compare_possible_erase_blocks);
	
	unsigned int num_blocks_to_gc = possible_erase_blocks[target_plane].size() < MAX_BLOCKS_PER_GC ? possible_erase_blocks[target_plane].size() : MAX_BLOCKS_PER_GC;
	
	for(unsigned int top_candidate = 0;top_candidate < num_blocks_to_gc;top_candidate++)
		erase_block_list.push_back(possible_erase_blocks[target_plane][top_candidate].first);

	struct required_bg_events_pointer required_bg_events_location[num_blocks_to_gc];
	unsigned int cur_block_to_gc_num = 0;


	//TODO this can be optimized in terms of runtime because we already know the indexes which correspond to the target plane
	for(iter = filled_block_list.begin();iter!=filled_block_list.end();iter++)
	{
		bool schedule_writes = false;
		Address cur_address = iter->physical_address;
		unsigned int cur_plane_num = cur_address.package*PACKAGE_SIZE*DIE_SIZE + cur_address.die*DIE_SIZE + cur_address.plane;
		if(cur_plane_num == target_plane)
		{
			if(std::find(erase_block_list.begin(), erase_block_list.end(), std::distance(filled_block_list.begin(), iter)) != erase_block_list.end()	
				)  
			{
				printf("writes will be scheduled\n");
				schedule_writes = true;
				required_bg_events_location[cur_block_to_gc_num].rw_start_index = background_events[target_plane].size();
				erase_block_list.push_back(std::distance(filled_block_list.begin(), iter));
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
					bg_read.start_time = time;
					bg_read.end_time = bg_read.start_time;
					bg_read.process = BACKGROUND;
					bg_read.op_complete_pointer = NULL;
					bg_read.end_time_pointer = NULL;
					background_events[target_plane].push_back(bg_read);

					if(schedule_writes)
					{
						struct ftl_event bg_write;
						bg_write.type = WRITE;
						bg_write.physical_address = cur_page_address;
						bg_write.logical_address = (iter->page_mapping)[i];
						bg_write.start_time = time;
						bg_write.end_time = bg_write.start_time;
						bg_write.process = BACKGROUND;
						bg_write.op_complete_pointer = NULL;
						bg_write.end_time_pointer = NULL;
						background_events[target_plane].push_back(bg_write);
						printf("pushed back write on ");
						bg_write.physical_address.print();
						printf("\n");
					}
				}
			}
			if(schedule_writes)
			{
				required_bg_events_location[cur_block_to_gc_num].rw_end_index = background_events[target_plane].size();
				cur_block_to_gc_num++;
			}
		}
	}
	assert(erase_block_list.size() == 2*num_blocks_to_gc);
	std::vector<unsigned int>::iterator remove_till = erase_block_list.begin();
	std::advance(remove_till, num_blocks_to_gc);
	erase_block_list.erase(erase_block_list.begin(), remove_till);
	cur_block_to_gc_num = 0;
	for(;cur_block_to_gc_num < num_blocks_to_gc;cur_block_to_gc_num++)
	{
		unsigned int offset = erase_block_list[cur_block_to_gc_num];
		std::list<struct ssd_block>::iterator erase_block_iterator = filled_block_list.begin();
		std::advance(erase_block_iterator, offset);
		struct ssd_block erase_block = *erase_block_iterator;
		required_bg_events_location[cur_block_to_gc_num].erase_index = background_events[target_plane].size();
		printf("Setting block ");
		erase_block.physical_address.print();
		printf(" for GC\n");
		struct ftl_event bg_erase;
		bg_erase.type = ERASE;
		bg_erase.physical_address = erase_block.physical_address;
		bg_erase.logical_address = translate_pba_lba(erase_block.physical_address);
		bg_erase.start_time = time;
		bg_erase.end_time = 0;
		bg_erase.process = BACKGROUND;
		bg_erase.op_complete_pointer = NULL;
		bg_erase.end_time_pointer = NULL;
		background_events[target_plane].push_back(bg_erase);
		bg_cleaning_blocks[target_plane].push_back(erase_block);

	}
	std::sort(erase_block_list.begin(), erase_block_list.end());
	cur_block_to_gc_num = num_blocks_to_gc;
	for(;cur_block_to_gc_num-- > 0;)
	{
		unsigned int offset = erase_block_list[cur_block_to_gc_num];
		std::list<struct ssd_block>::iterator erase_block_iterator = filled_block_list.begin();
		std::advance(erase_block_iterator, offset);
		filled_block_list.erase(erase_block_iterator);
	}
	cur_block_to_gc_num = 0;
	for(;cur_block_to_gc_num < num_blocks_to_gc;cur_block_to_gc_num++)
	{
		required_bg_events[target_plane].push_back(required_bg_events_location[cur_block_to_gc_num]);
	}
	return SUCCESS;
}


double FtlImpl_Page::process_background_tasks(Event &event)
{
	double ret_time = std::numeric_limits<double>::max();
	double cur_simulated_time = event.get_start_time();
	bool urgent_cleaning = false;
	if(free_block_list.size() < low_watermark)
	{
		urgent_cleaning = true;
	}
	for(unsigned int plane_num=0;plane_num<SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;plane_num++)
	{
		if(background_events[plane_num].size() == 0)
		{
			continue;
		}
		bool first = true;
		unsigned int last_plane_num = plane_num;
		bool continuous_erases = false;
		while(	background_events[plane_num].size() > 0 )
		{
			struct ftl_event first_event = background_events[plane_num].front();
			Address candidate_address = first_event.physical_address;
			bool write_address_already_open;
			bool perform_first_task = true;
			if(first_event.type != ERASE)
				continuous_erases = false;
			if(first_event.type == READ)
			{
				if(!(logical_page_list[first_event.logical_address].physical_address == first_event.physical_address))
				{
					background_events[plane_num].erase(background_events[plane_num].begin());
					if(background_events[plane_num].size() > 0)
					{
						background_events[plane_num].front().start_time = first_event.start_time;
					}
					move_required_pointers(plane_num, 0, 1);
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
					{
						background_events[plane_num].front().start_time = first_event.start_time;
					}
					move_required_pointers(plane_num, 0, 1);
					continue;
				}
				candidate_address = find_write_location(first_event.start_time, log_write_address, &write_address_already_open);
				if(candidate_address.valid == NONE)
				{
					perform_first_task = false;
				}
			}
			std::vector<struct ftl_event>::iterator iter;
			unsigned int candidate_plane = candidate_address.package*PACKAGE_SIZE*DIE_SIZE + candidate_address.die*DIE_SIZE + candidate_address.plane;
			if(first_event.type == ERASE) 			
			{
				if(bg_cleaning_blocks[plane_num].front().valid_page_count != 0)
				{
					break;
				}
				else if(first_event.start_time < bg_cleaning_blocks[plane_num].front().page_copy_complete_time)
				{
					first_event.start_time = bg_cleaning_blocks[plane_num].front().page_copy_complete_time;
				}
			}
			if(ftl_queues[candidate_plane].size() != 0)
			{
				background_events[plane_num].front().start_time = plane_free_times[candidate_plane].second;
				//first_event.start_time = plane_free_times[candidate_plane];
				break;
			}
			if(first_event.start_time < plane_free_times[candidate_plane].first && !continuous_erases)
				first_event.start_time = plane_free_times[candidate_plane].first;
			if(first_event.start_time > cur_simulated_time)
			{
				perform_first_task = false;
			}
			Event e(first_event.type, first_event.logical_address, 1, first_event.start_time);
			e.set_address(candidate_address);
			if(perform_first_task && !(urgent_cleaning && first_event.type == WRITE ))
			{
				//TODO: It might be a better idea to set priority plane only before dispatching the event
				//from process_ftl_queues, just as removal of priority planes is done from there
				if(first)
					ssd.cache.add_priority_plane(plane_num);
				bool is_erase = false;
				if(first_event.type == READ)
				{
					first_event.end_time = read_(e);
					/*
					first_event.end_time = first_event.start_time + PAGE_READ_DELAY;
					plane_free_times[plane_num] = first_event.end_time;
					//struct queued_ftl_event *stalled_bg_read = (struct queued_ftl_event *)malloc(sizeof(struct queued_ftl_event));
					struct queued_ftl_event *stalled_bg_read = new queued_ftl_event();
					stalled_bg_read->event = first_event;
					stalled_bg_read->child = NULL;
					stalled_bg_read->parent_completed = true;
					stalled_bg_read->predecessor_completed = ftl_queues[candidate_plane].size() == 0 ? true : false;
					ftl_queue_has_bg_event[plane_num] = true;
					ftl_queue_last_bg_event_index[plane_num] = ftl_queues[plane_num].size();
					ftl_queues[plane_num].push_back(stalled_bg_read);
					last_plane_num = plane_num;
					*/
					background_events[plane_num].erase(background_events[plane_num].begin());
					move_required_pointers(plane_num, 0, 1);
				}
				else if(first_event.type == WRITE)
				{
					unsigned int pre_fbl_size = free_block_list.size();
					bool increment_ret_val = increment_log_write_address(e.get_start_time(), candidate_address, write_address_already_open); 
					unsigned int post_fbl_size = free_block_list.size();
					printf("bg event writing on ");
					candidate_address.print();
					printf("\n");
					fflush(stdout);
					mark_reserved(log_write_address, true);
					first_event.end_time = write_(e);

					Address prev_block_address = logical_page_list[first_event.logical_address].physical_address;
					prev_block_address.page = 0;
					prev_block_address.valid = BLOCK;
					
					std::vector<struct ssd_block>::iterator prev_block_iter = bg_cleaning_blocks[plane_num].begin();
					for(;prev_block_iter != bg_cleaning_blocks[plane_num].end();prev_block_iter++)
					{
						if(prev_block_iter->physical_address ==	prev_block_address)
						{
							prev_block_iter->page_copy_complete_time = first_event.end_time;
							break;
						}
					}

					/*
					if(!mark_reserved(log_write_address, true))
						assert(false);
					
					first_event.end_time = plane_free_times[log_write_plane] + PAGE_WRITE_DELAY;
					

					//struct queued_ftl_event *urgent_bg_write = (struct queued_ftl_event *)malloc(sizeof(struct queued_ftl_event));
					struct queued_ftl_event *urgent_bg_write = new queued_ftl_event();
					urgent_bg_write->event = first_event;
					urgent_bg_write->predecessor_completed = ftl_queues[log_write_plane].size() == 0 ? true : false;
					urgent_bg_write->parent_completed = first ? true : false;
					if(!first)
					{
						std::vector<struct queued_ftl_event *>::iterator last_iterator = ftl_queues[last_plane_num].end();
						--last_iterator;
						struct queued_ftl_event *last_event = *(last_iterator);
						last_event->child = urgent_bg_write;
					}
					urgent_bg_write->child = NULL;
					urgent_bg_write->write_from_address = logical_page_list[first_event.logical_address].physical_address;
					ftl_queue_has_bg_event[plane_num] = true;
					ftl_queue_last_bg_event_index[plane_num] = ftl_queues[plane_num].size();
					ftl_queues[log_write_plane].push_back(urgent_bg_write);
					last_plane_num = log_write_plane;
					*/
					background_events[plane_num].erase(background_events[plane_num].begin());
					move_required_pointers(plane_num, 0, 1);
					if(post_fbl_size < low_watermark)// && pre_fbl_size >= low_watermark)
					{
						urgent_cleaning = true;
						printf("calling queue required from bg write\n");
						queue_required_bg_events(event.get_start_time(), true);
					}
				}
				else if(first_event.type == ERASE)
				{
					//printf("BG ERASE ");
					//candidate_address.print();
					//printf("\n");
					controller.issue(e);
					first_event.end_time = e.get_total_time();
					continuous_erases = true;
					plane_prioritized_till[plane_num] = first_event.end_time;
					
					/*
					first_event.end_time = plane_free_times[plane_num] + BLOCK_ERASE_DELAY;
					plane_free_times[plane_num] = first_event.end_time;
					//struct queued_ftl_event *urgent_bg_erase = (struct queued_ftl_event *)malloc(sizeof(struct queued_ftl_event));
					struct queued_ftl_event *urgent_bg_erase = new queued_ftl_event();
					urgent_bg_erase->event = first_event;
					urgent_bg_erase->predecessor_completed = ftl_queues[plane_num].size() == 0 ? true : false;
					urgent_bg_erase->parent_completed = first ? true : false; //TODO: This should be set to true


					urgent_bg_erase->child = NULL;
					ftl_queue_has_bg_event[plane_num] = true;
					ftl_queue_last_bg_event_index[plane_num] = ftl_queues[plane_num].size();
					ftl_queues[plane_num].push_back(urgent_bg_erase);
					*/
					struct ssd_block block_to_clean = bg_cleaning_blocks[plane_num].front();
					block_to_clean.page_to_write = 0;
					block_to_clean.page_copy_complete_time = -1;
					block_to_clean.scheduled_for_erasing = false;
					block_to_clean.lifetime_left -= 1;
					free_block_list.push_back(block_to_clean);
					if(free_block_list.size() > clean_threshold)
					{
						gc_required = false;
					}
					bg_cleaning_blocks[plane_num].erase(bg_cleaning_blocks[plane_num].begin());
					required_bg_events[plane_num].erase(required_bg_events[plane_num].begin());
					is_erase = true;
					/*
					last_plane_num = plane_num;
					*/
					background_events[plane_num].erase(background_events[plane_num].begin());
					move_required_pointers(plane_num, 0, 1);
				}
				plane_free_times[candidate_plane].first = first_event.end_time;
				plane_free_times[candidate_plane].second = first_event.end_time;
				first = false;
				if(urgent_cleaning && is_erase)
				{
					if(free_block_list.size() >= low_watermark)
						urgent_cleaning = false;
				}
				if(background_events[plane_num].size() > 0)
				{
					if(first_event.type == WRITE && background_events[plane_num].front().type == READ)
						background_events[plane_num].front().start_time = first_event.start_time;
					else if(continuous_erases && background_events[plane_num].front().type == ERASE)
						background_events[plane_num].front().start_time = first_event.start_time;
					else
						background_events[plane_num].front().start_time = first_event.end_time;
				}
			}
			else
			{
				break;
			}
		}
		if(background_events[plane_num].size() > 0 && background_events[plane_num].front().start_time < ret_time)
		{
			ret_time = background_events[plane_num].front().start_time;
		}
		/*
		else if(background_events[plane_num].size() == 0)
		{
			ssd.cache.remove_priority_plane(plane_num);
		}
		*/
	}
	return ret_time;
}

bool FtlImpl_Page::queue_required_bg_events(double time, bool necessary)
{
	bool urgent_cleaning = true;
	unsigned int urgent_cleaning_plane;
	unsigned int min_ops_required = UINT_MAX;
	double min_time_required = std::numeric_limits<double>::max();
	std::vector<std::pair<unsigned int, double>>plane_sequence;
	std::vector<std::pair<unsigned int, double>>::iterator insert_location_iter;
	for(unsigned int plane_num=0;plane_num<SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;plane_num++)
	{
		if(required_bg_events[plane_num].size() == 0)
			continue;
		
		unsigned int num_ops = required_bg_events[plane_num].front().rw_end_index - required_bg_events[plane_num].front().rw_start_index;
		double time_required = num_ops * (PAGE_READ_DELAY + PAGE_WRITE_DELAY) + plane_free_times[plane_num].second;
		insert_location_iter = plane_sequence.begin();
		while(insert_location_iter != plane_sequence.end() && (*insert_location_iter).second < time_required)
		{
			insert_location_iter++;
		}
		plane_sequence.insert(insert_location_iter, std::pair<unsigned int, double>(plane_num, time_required));
		if(time_required < min_time_required)
		{
			min_time_required = time_required;
			urgent_cleaning_plane = plane_num;
		}
		
		//if(num_ops < min_ops_required)
		//{
		//	min_ops_required = num_ops;
		//	urgent_cleaning_plane = plane_num;
		//}
	}
	if(min_time_required == std::numeric_limits<double>::max())
	{
		printf("returning false from queue required because all are queued already\n");
		return false;
	}

	//if(min_ops_required == UINT_MAX)
	//{
	//	return false;
	//}
	
	//unsigned int plane_num = urgent_cleaning_plane;

	bool max_benefit_plane = true;
	std::vector<std::pair<unsigned int, double>>::iterator plane_iter = plane_sequence.begin();
	for(;plane_iter != plane_sequence.end();plane_iter++)
	{
		if(!max_benefit_plane)
		{
			printf("breaking because \n");
			break;
		}
		unsigned int plane_num = (*plane_iter).first;
		if(cleaning_queued[plane_num] > 0)
		{
			max_benefit_plane = false;
			printf("plane cant be queued\n");
			continue;
		}

		double first_event_start_time = background_events[plane_num].front().start_time;
		
		// Uncomment this code if the urgent bg events should include only the minimal
		// required reads, writes and erases (i.e. get rid of the reads for the caching
		
		/*
		struct required_bg_events_pointer required_bg_events_location = required_bg_events[plane_num].front();
		std::vector<struct ftl_event>::iterator begin_pointer = background_events[plane_num].begin();
		std::vector<struct ftl_event>::iterator rw_start_pointer = background_events[plane_num].begin();
		std::advance(rw_start_pointer, required_bg_events_location.rw_start_index);
		std::vector<struct ftl_event>::iterator rw_end_pointer = background_events[plane_num].begin();
		std::advance(rw_end_pointer, required_bg_events_location.rw_end_index);
		std::vector<struct ftl_event>::iterator erase_pointer = background_events[plane_num].begin();
		std::advance(erase_pointer, required_bg_events_location.erase_index);

		std::vector<struct ftl_event> cur_plane_bg_events;

		printf("%d %d\n", rw_start_pointer != background_events[plane_num].end(), rw_end_pointer != background_events[plane_num].end());

		std::vector<struct ftl_event>::iterator pointer = rw_start_pointer;
		for(;pointer!=rw_end_pointer;pointer++)
		{
			struct ftl_event cur_bg_event;
			cur_bg_event.type = pointer->type;
			cur_bg_event.physical_address = pointer->physical_address;
			cur_bg_event.logical_address = pointer->logical_address;
			cur_bg_event.start_time = pointer->start_time;
			cur_bg_event.end_time = pointer->end_time;
			cur_bg_event.process = pointer->process;
			cur_bg_event.op_complete_pointer = pointer->op_complete_pointer;
			cur_bg_event.end_time_pointer = pointer->end_time_pointer;
			cur_plane_bg_events.push_back(cur_bg_event);
			printf("Planning to to %d at ", cur_bg_event.type);
			cur_bg_event.physical_address.print();
			printf("\n");
		}

		cur_plane_bg_events.push_back(*erase_pointer);
		printf("planning to do %d at ", erase_pointer->type);
		erase_pointer->physical_address.print();
		printf("\n");
		printf("Next head is %d at ", rw_end_pointer->type);
		rw_end_pointer->physical_address.print();
		printf("\n");

		required_bg_events[plane_num].erase(required_bg_events[plane_num].begin());

		printf("erasing at num %d type %d ", required_bg_events_location.erase_index, erase_pointer->type);
		erase_pointer->physical_address.print();
		printf("\n");
		erase_pointer = background_events[plane_num].erase(erase_pointer);
		move_required_pointers(plane_num, required_bg_events_location.erase_index, required_bg_events_location.erase_index + 1);

		if(required_bg_events[plane_num].size() > 0)
		{
			if(required_bg_events[plane_num].front().rw_start_index > required_bg_events_location.erase_index)
			{
				rw_end_pointer = background_events[plane_num].erase(rw_end_pointer, erase_pointer);
				printf("erasing special from %d to %d\n", required_bg_events_location.rw_end_index, required_bg_events_location.erase_index);
				move_required_pointers(plane_num, required_bg_events_location.rw_end_index, required_bg_events_location.erase_index); 
			}
		}

		std::vector<struct ftl_event>::iterator temp_iter = rw_start_pointer;
		while(temp_iter != rw_end_pointer)
		{
			printf("erasing type %d ", temp_iter->type);
			temp_iter->physical_address.print();
			printf("\n");
			temp_iter++;
		}

		background_events[plane_num].erase(rw_start_pointer, rw_end_pointer);
		printf("erasing from %d to %d\n", required_bg_events_location.rw_start_index, required_bg_events_location.rw_end_index);
		move_required_pointers(plane_num, required_bg_events_location.rw_start_index, required_bg_events_location.rw_end_index);
		*/



		//cur_plane_bg_events.front().start_time = first_event_start_time;
		background_events[plane_num].front().start_time = first_event_start_time;
		bool first = true;
		unsigned int last_plane_num = plane_num;
		std::vector<std::pair<unsigned int, unsigned int>> write_locations;
		//while(cur_plane_bg_events.size() > 0 && urgent_cleaning)
		bool queue_erases = false;
		Address queue_erases_upto;
		queue_erases_upto.valid = NONE;
		unsigned int queue_erases_offset = 0;
		std::vector<struct ftl_event>::iterator bg_events_iter = background_events[plane_num].begin();
		//while(background_events[plane_num].size() > 0 && urgent_cleaning)
		for(;bg_events_iter != background_events[plane_num].end();)
		{
			//TODO: It might be a better idea to set priority plane only before dispatching the event
			//from process_ftl_queues, just as removal of priority planes is done from there
			if(first)
				ssd.cache.add_priority_plane(plane_num);
			//struct ftl_event first_event = cur_plane_bg_events.front();
			//struct ftl_event first_event = background_events[plane_num].front();
			struct ftl_event first_event = *bg_events_iter;
			if(first_event.start_time < time)
				first_event.start_time = time;
			if(queue_erases && first_event.type != ERASE)
			{
				queue_erases_offset++;
				bg_events_iter++;
				continue;
			}

			if(first_event.type != ERASE && 
				!(logical_page_list[first_event.logical_address].physical_address == first_event.physical_address) )
			{
				//TODO write NOOP
				//cur_plane_bg_events.erase(cur_plane_bg_events.begin());
				//if(cur_plane_bg_events.size() > 0)
				//	cur_plane_bg_events.front().start_time = first_event.start_time;
				
				//background_events[plane_num].erase(background_events[plane_num].begin());
				bg_events_iter = background_events[plane_num].erase(bg_events_iter);
				//if(background_events[plane_num].size() > 0)
				if(bg_events_iter != background_events[plane_num].end())
					bg_events_iter->start_time = first_event.start_time;
					//background_events[plane_num].front().start_time = first_event.start_time;
				move_required_pointers(plane_num, 0, 1);
				continue;
			}
			bool is_erase = false;
			if(first_event.type == READ)
			{
				if(plane_free_times[plane_num].second > first_event.start_time)
					first_event.start_time = plane_free_times[plane_num].second;
				first_event.end_time = first_event.start_time + PAGE_READ_DELAY;
				plane_free_times[plane_num].second += PAGE_READ_DELAY;
				struct queued_ftl_event *urgent_bg_read = new queued_ftl_event();
				urgent_bg_read->event = first_event;
				urgent_bg_read->parent_completed = true;
				urgent_bg_read->predecessor_completed = ftl_queues[plane_num].size() == 0 ? true : false;
				urgent_bg_read->child = NULL;
				ftl_queue_has_bg_event[plane_num] = true;
				ftl_queue_last_bg_event_index[plane_num] = ftl_queues[plane_num].size();
				ftl_queues[plane_num].push_back(urgent_bg_read);
				last_plane_num = plane_num;
				move_required_pointers(plane_num, 0, 1);
				printf("Read from ");
				first_event.physical_address.print();
				printf("\n");
			}
			else if(first_event.type == WRITE)
			{
				Address candidate_address;
				bool write_address_already_open;
				candidate_address = find_write_location(first_event.start_time, log_write_address, &write_address_already_open);
				if(candidate_address.valid == NONE)
				{
					queue_erases_upto = first_event.physical_address;
					queue_erases_upto.page = 0;
					queue_erases_upto.valid = BLOCK;
					queue_erases_offset = 0;
					queue_erases = true;
					printf("Can't queue because out of space\n");
					/* This is the smart switch ;) */
					//MAX_GC_PLANES = 0;
					//if(MAX_GC_BLOCKS < SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*MAX_BLOCKS_PER_GC)
					//	MAX_GC_BLOCKS = SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*MAX_BLOCKS_PER_GC;
					//printf("This is the problem\n");
					//continue;	
					break;
				}
				//if(!write_address_already_open && !max_benefit_plane)
				//{
				//	printf("Other planes were getting too demanding\n");
				//	break;
				//}
				bool increment_ret_val = increment_log_write_address(first_event.start_time, candidate_address, write_address_already_open);
				first_event.physical_address = log_write_address;
				if(!mark_reserved(log_write_address, true))
					assert(false);
				unsigned int log_write_plane = log_write_address.package*PACKAGE_SIZE*DIE_SIZE + log_write_address.die*DIE_SIZE + log_write_address.plane;
				if(plane_free_times[log_write_plane].second > first_event.start_time)
					first_event.start_time = plane_free_times[log_write_plane].second;
				first_event.end_time = first_event.start_time + PAGE_WRITE_DELAY;
				plane_free_times[log_write_plane].second += PAGE_WRITE_DELAY;
				struct queued_ftl_event *urgent_bg_write = new queued_ftl_event();
				urgent_bg_write->event = first_event;
				urgent_bg_write->predecessor_completed = ftl_queues[log_write_plane].size() == 0 ? true : false;
				urgent_bg_write->parent_completed = first ? true : false;
				if(!first)
				{
					std::vector<struct queued_ftl_event *>::iterator last_iterator = ftl_queues[last_plane_num].end();
					--last_iterator;
					struct queued_ftl_event *last_event = *(last_iterator);
					last_event->child = urgent_bg_write;
				}
				urgent_bg_write->child = NULL;
				urgent_bg_write->write_from_address = logical_page_list[first_event.logical_address].physical_address;
				ftl_queue_has_bg_event[plane_num] = true;
				ftl_queue_last_bg_event_index[plane_num] = ftl_queues[plane_num].size();
				ftl_queues[log_write_plane].push_back(urgent_bg_write);
				last_plane_num = log_write_plane;
				write_locations.push_back(std::pair<unsigned int, unsigned int>(log_write_plane, ftl_queues[log_write_plane].size() -1));
				move_required_pointers(plane_num, 0, 1);
				printf("Write to ");
				log_write_address.print();
				printf("\n");
			}
			else if(first_event.type == ERASE)
			{
				if(queue_erases && first_event.physical_address == queue_erases_upto)
				{
					break;
				}
				if(plane_free_times[plane_num].second > first_event.start_time)
					first_event.start_time = plane_free_times[plane_num].second;
				first_event.end_time = first_event.start_time + BLOCK_ERASE_DELAY;
				plane_free_times[plane_num].second += BLOCK_ERASE_DELAY;
				struct queued_ftl_event *urgent_bg_erase = new queued_ftl_event();
				urgent_bg_erase->event = first_event;
				urgent_bg_erase->predecessor_completed = ftl_queues[plane_num].size() == 0 ? true : false;
				urgent_bg_erase->parent_completed = first ? true : false; //TODO this should be set to true
				/*
				if(!first)
				{
					std::vector<std::pair<unsigned int, unsigned int>>::iterator write_locations_iter = write_locations.begin();
					for(;write_locations_iter!=write_locations.end();write_locations_iter++)
					{
						struct queued_ftl_event *parent_write_event = ftl_queues[write_locations_iter->first][write_locations_iter->second];
						Address write_from = parent_write_event->write_from_address;
						write_from.page = 0;
						write_from.valid = BLOCK;
						if(write_from == first_event.physical_address)
							parent_write_event->child = urgent_bg_erase;
					}
				}
				*/
				urgent_bg_erase->child = NULL;
				ftl_queue_has_bg_event[plane_num] = true;
				ftl_queue_last_bg_event_index[plane_num] = ftl_queues[plane_num].size();
				ftl_queues[plane_num].push_back(urgent_bg_erase);
				//struct ssd_block block_to_clean = bg_cleaning_blocks[plane_num].front();
				//block_to_clean.page_to_write = 0;
				//printf("setting scheduled to true in queue\n");
				//block_to_clean.scheduled_for_erasing = true;
				//free_block_list.push_back(block_to_clean);
				//if(free_block_list.size() > clean_threshold)
				//{
				//	gc_required = false;
				//}
				//bg_cleaning_blocks[plane_num].erase(bg_cleaning_blocks[plane_num].begin());
				required_bg_events[plane_num].erase(required_bg_events[plane_num].begin());
				is_erase = true;
				last_plane_num = plane_num;
				if(queue_erases)
					move_required_pointers(plane_num, queue_erases_offset, queue_erases_offset + 1);
				else
					move_required_pointers(plane_num, 0, 1);
				cleaning_queued[plane_num]++;
				printf("Erase at ");
				first_event.physical_address.print();
				printf("\n");
			}
		
			//cur_plane_bg_events.erase(cur_plane_bg_events.begin());
			//background_events[plane_num].erase(background_events[plane_num].begin());
			bg_events_iter = background_events[plane_num].erase(bg_events_iter);
				
			//if(urgent_cleaning && is_erase)
			//{
			//	if(free_block_list.size() >= low_watermark)
			//	{
			//		urgent_cleaning = false;
			//		break;
			//	}
			//}
			first = false;
			if(is_erase && bg_events_iter->type == READ)
				break;
		}
		max_benefit_plane = false;
	}
	printf("returning from here\n");
	return true;
	/*
	if(background_events[plane_num].size() == 0)
	{
		ssd.cache.remove_priority_plane(plane_num);
	}
	*/
}


double FtlImpl_Page::process_ftl_queues(Event &event)
{
	double time = event.get_start_time();
	double ret_time = std::numeric_limits<double>::max();
	bool processed_event = false;
	do
	{
		processed_event = false;
		for(unsigned int plane_num = 0;plane_num < SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;plane_num++)
		{
			if(ftl_queues[plane_num].size() == 0)
			{
				continue;
			}
			std::vector<struct queued_ftl_event *>::iterator first_iterator = ftl_queues[plane_num].begin();
			struct queued_ftl_event *first_pointer = *(first_iterator);
			first_iterator = ftl_queues[plane_num].begin();


			bool process_worthy = first_pointer->parent_completed;
			std::list<struct ssd_block>::iterator erase_block_pointer = allocated_block_list.end();
			if(first_pointer->event.type == ERASE)
			{
				Address erase_address = first_pointer->event.physical_address;
				std::vector<struct ssd_block>::iterator bg_iter;
				for(bg_iter=bg_cleaning_blocks[plane_num].begin();bg_iter!=bg_cleaning_blocks[plane_num].end();bg_iter++)
				{
					if(bg_iter->physical_address == erase_address)
						break;
				}
				assert(bg_iter != bg_cleaning_blocks[plane_num].end());
				process_worthy = (bg_iter->valid_page_count == 0);
				if(process_worthy && bg_iter->page_copy_complete_time > first_pointer->event.start_time)
					first_pointer->event.start_time = bg_iter->page_copy_complete_time;

			}
			if(first_pointer)
			{
				printf("%d %d %d %d %d ", plane_num, first_pointer->event.type, process_worthy, first_pointer->event.start_time <= time, first_pointer->predecessor_completed);
				first_pointer->event.physical_address.print();
				printf("\n");
			}
			while(	first_pointer && 
					process_worthy && 
					first_pointer->event.start_time <= time &&
					first_pointer->predecessor_completed)
			{
				processed_event = true;
				struct ftl_event first_event = first_pointer->event;
				bool requires_processing = true;
				double next_event_time;
				//printf("%d %d %d\n", plane_num, first_pointer->event.type, first_pointer->event.process);
				if(	(first_event.type == READ && 
					!(logical_page_list[first_event.logical_address].physical_address == first_event.physical_address))
					||	
					(first_event.type == WRITE && first_event.process == BACKGROUND &&  
					!(logical_page_list[first_event.logical_address].physical_address == first_pointer->write_from_address))
					)
				{
					//TODO write NOOP
					//TODO If this is a foreground read, then it must be redirected to a new address
					requires_processing = false;
					first_event.end_time = first_event.start_time;
					next_event_time = first_event.start_time;
					if(first_event.type == WRITE)
					{
						//printf("Skipping write on ");
						//first_event.physical_address.print();
						//printf("\n");
						mark_reserved(first_event.physical_address, false);
					}
				}
				if(requires_processing)
				{
					Event e(first_event.type, first_event.logical_address, 1, first_event.start_time);		
					e.set_address(first_event.physical_address);
					if(first_event.type == ERASE)
					{
						controller.issue(e);
						next_event_time = e.get_total_time();
						struct ssd_block block_to_clean = bg_cleaning_blocks[plane_num].front();
						block_to_clean.page_to_write = 0;
						block_to_clean.lifetime_left -= 1;
						block_to_clean.scheduled_for_erasing = false;
						block_to_clean.page_copy_complete_time = -1;
						free_block_list.push_back(block_to_clean);
						if(free_block_list.size() > clean_threshold)
						{
							gc_required = false;
						}
						bg_cleaning_blocks[plane_num].erase(bg_cleaning_blocks[plane_num].begin());
						first_event.end_time = next_event_time;
						cleaning_queued[plane_num]--;
						printf("QUEUE ERASE ");
						first_event.physical_address.print();
						printf(" %f %f\n", first_event.start_time, first_event.end_time);
						plane_prioritized_till[plane_num] = next_event_time;
					}
					else if(first_event.type == READ)
					{
						next_event_time = read_(e);
						first_event.end_time = next_event_time;
						printf("QUEUE READ %d ", first_event.logical_address);
						first_event.physical_address.print();
						printf(" %f %f\n", first_event.start_time, first_event.end_time);
					}
					else if(first_event.type == WRITE)
					{
						next_event_time = write_(e);
						first_event.end_time = next_event_time;

						Address prev_block_address = first_pointer->write_from_address;
						prev_block_address.page = 0;
						prev_block_address.valid = BLOCK;
						unsigned int prev_plane = prev_block_address.package*PACKAGE_SIZE*DIE_SIZE + prev_block_address.die*DIE_SIZE + prev_block_address.plane;
						std::vector<struct ssd_block>::iterator prev_block_iter = bg_cleaning_blocks[prev_plane].begin();
						for(;prev_block_iter != bg_cleaning_blocks[prev_plane].end();prev_block_iter++)
						{
							if(prev_block_iter->physical_address ==	prev_block_address)
							{
								prev_block_iter->page_copy_complete_time = first_event.end_time;
								break;
							}
						}
						printf("QUEUE WRITE ");
						first_event.physical_address.print();
						printf(" %f %f\n", first_event.start_time, first_event.end_time);
					}
					//TODO: Right now, this is checking that there are absolutely no background events
					//A possibly better way would be to check that this round of GC has ended, i.e. the last 
					//erase in this round has ended (there might be events from a new round queued up
					//This would require setting a flag to indicate the same in process_background_tasks
					//if((first_event.type == READ || first_event.type == WRITE) && ftl_queue_has_bg_event[plane_num] && ftl_queue_last_bg_event_index[plane_num] == 0 && first_pointer->event.process == FOREGROUND && background_events[plane_num].size() == 0)
					//{
					//	printf("removing priority of plane %d\n", plane_num);
					//	ssd.cache.remove_priority_plane(plane_num);

					//}
				}
				if(first_event.op_complete_pointer)
				{
					printf("setting op complete for %d on %d to true at address %p\n", first_event.type, first_event.logical_address, first_event.op_complete_pointer);
					*(first_event.op_complete_pointer) = true;
				}
				if(first_event.end_time_pointer)
				{
					*(first_event.end_time_pointer) = first_event.end_time;
				}
				if(first_pointer->child)
				{
					bool set_child_parent_completed = first_pointer->child->event.type != ERASE;
					if(set_child_parent_completed)
					{
						first_pointer->child->parent_completed = true;
						if(first_pointer->child->event.start_time < next_event_time)
							first_pointer->child->event.start_time = next_event_time;
					}
				}

				plane_free_times[plane_num].first = next_event_time;
				delete (first_pointer);
				first_pointer = NULL;
				ftl_queues[plane_num].erase(ftl_queues[plane_num].begin());
				if(ftl_queue_has_bg_event[plane_num])
					ftl_queue_last_bg_event_index[plane_num]--;
				if(ftl_queues[plane_num].size() > 0)
				{
					first_pointer = *(ftl_queues[plane_num].begin());
					if(first_event.type == ERASE && first_pointer->event.type == ERASE)
						first_pointer->event.start_time = first_event.start_time;
					else
					{
						if(first_pointer->event.start_time < next_event_time)
							first_pointer->event.start_time = next_event_time;
					}
					double delay = 0;
					if(first_pointer->event.type == READ)
						delay = PAGE_READ_DELAY;
					else if(first_pointer->event.type == WRITE)
						delay = PAGE_WRITE_DELAY;
					else if(first_pointer->event.type == ERASE)
						delay = BLOCK_ERASE_DELAY;

					first_pointer->event.end_time = first_pointer->event.start_time + delay;
					if(plane_free_times[plane_num].second < first_pointer->event.end_time)
					{
						plane_free_times[plane_num].second = first_pointer->event.end_time;
					}
					
					first_pointer->predecessor_completed = true;
				}
				else
				{
					if(plane_free_times[plane_num].second < first_event.end_time)
					{
						plane_free_times[plane_num].second = first_event.end_time;
					}
					first_pointer = NULL;
				}
				if(first_pointer)
				{
					process_worthy = first_pointer->parent_completed;
					if(first_pointer->event.type == ERASE)
					{
						Address erase_address = first_pointer->event.physical_address;
						std::vector<struct ssd_block>::iterator bg_iter = bg_cleaning_blocks[plane_num].begin();
						for(;bg_iter!=bg_cleaning_blocks[plane_num].end();bg_iter++)
						{
							if(bg_iter->physical_address == erase_address)
								break;
						}
						assert(bg_iter != bg_cleaning_blocks[plane_num].end());
						process_worthy = (bg_iter->valid_page_count == 0);
						if(process_worthy && bg_iter->page_copy_complete_time > first_pointer->event.start_time)
							first_pointer->event.start_time = bg_iter->page_copy_complete_time;

					}
				}
			}
			if(	first_pointer && first_pointer->event.start_time < ret_time && 
				process_worthy && first_pointer->predecessor_completed)
			{
				ret_time = first_pointer->event.start_time;
				//printf("set ret time to ");
				//if(ret_time == std::numeric_limits<double>::max())
				//	printf("MAX");
				//else
				//	printf("%f", ret_time);
				//printf(" from plane %d\n", plane_num);
			}
			fflush(stdout);
		}
	}
	while(processed_event);
	return ret_time;
}


void FtlImpl_Page::move_required_pointers(unsigned int plane_num, unsigned int start, unsigned int end)
{
	unsigned int offset = end - start;
	std::vector<struct required_bg_events_pointer>::iterator urgent_pointer;
	//printf("Moving pointers for plane %d\n", plane_num);
	for(urgent_pointer=required_bg_events[plane_num].begin();urgent_pointer!=required_bg_events[plane_num].end();urgent_pointer++)
	{
		if(urgent_pointer->rw_start_index > start)
		{
			//printf("Moving start from %d ", urgent_pointer->rw_start_index);
			assert(urgent_pointer->rw_start_index >= end);
			urgent_pointer->rw_start_index -= offset;
			//printf("to %d\n", urgent_pointer->rw_start_index);
		}
		if(urgent_pointer->rw_end_index > start)
		{
			//printf("Moving end from %d ", urgent_pointer->rw_end_index);
			assert(urgent_pointer->rw_end_index >= end);
			urgent_pointer->rw_end_index -= offset;
			//printf("to %d\n", urgent_pointer->rw_end_index);
		}
		if(urgent_pointer->erase_index > start)
		{
			//printf("Moving erase from %d to ", urgent_pointer->erase_index);
			assert(urgent_pointer->erase_index >= end);
			urgent_pointer->erase_index -= offset;
			//printf("to %d\n", urgent_pointer->erase_index);
		}
	}
}

bool FtlImpl_Page::mark_reserved(Address address, bool is_reserved)
{
	std::list<struct ssd_block>::iterator iter;
	Address block_address = address;
	block_address.page = 0;
	block_address.valid = BLOCK;
	for(iter=allocated_block_list.begin();iter!=allocated_block_list.end();iter++)
	{
		if(iter->physical_address == block_address)
		{
			if(is_reserved)
				iter->reserved_page_count++;
			else
				iter->reserved_page_count--;
			return true;
		}
	}
	return false;
}

void FtlImpl_Page::unset_plane_priorities(double time)
{
	for(unsigned int i=0;i<SSD_SIZE*PACKAGE_SIZE*DIE_SIZE;i++)
	{
		if(plane_prioritized_till[i] != -1 && plane_prioritized_till[i] <= time)
		{
			ssd.cache.remove_priority_plane(i);
			plane_prioritized_till[i] = -1;
		}
	}
}
