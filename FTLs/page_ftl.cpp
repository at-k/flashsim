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

bool FtlImpl_Page::increment_log_write_address(Event &event)
{
	if(log_write_address.valid == PAGE)
	{
		unsigned int cur_page_num = log_write_address.page;
		if(cur_page_num < BLOCK_SIZE - 1)
		{
			log_write_address.page += 1;
			return true;
		} 
	}  
	//(log_file, "calling allocate from increment\n");
	return allocate_new_block(false, event);
}

bool FtlImpl_Page::allocate_new_block(bool for_cleaning, Event &event)
{
	unsigned int cur_write_lba = event.get_logical_address();
	unsigned int free_list_size = free_block_list.size();
	if(free_list_size == 0)
	{
		//(log_file, "returning failure because no free blocks are available\n");
		return false;
	}  
	if(free_list_size > clean_threshold || for_cleaning)
	{
		printf("no need to clean, we got %d blocks\n", free_list_size);
		struct ssd_block new_ssd_block = free_block_list.front();
		allocated_block_list.push_back(new_ssd_block);
		free_block_list.pop_front();
		log_write_address = allocated_block_list.back().physical_address;
		log_write_address.page = 0;
		log_write_address.valid = PAGE;
		return true;
	}
	else
	{
		//do cleaning and wear levelling
		//printf("need to erase some block\n");
		//Event garbage_collect_event(ERASE, cur_write_lba, 1, event.get_total_time());
		if(garbage_collect(event) == SUCCESS)
			return true;
		else
		{
			//(log_file, "returning failure from allocate\n");
			return false;
		}

	}
}

FtlImpl_Page::FtlImpl_Page(Controller &controller):FtlParent(controller)
{
	RAW_SSD_PAGES = SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE*BLOCK_SIZE; 
	ADDRESSABLE_SSD_PAGES = int( ( 1- ( float)OVERPROVISIONING/100.0)*RAW_SSD_PAGES); 
	unsigned int RAW_SSD_BLOCKS = RAW_SSD_PAGES/BLOCK_SIZE;
	unsigned int ADDRESSABLE_SSD_BLOCKS = ADDRESSABLE_SSD_PAGES/BLOCK_SIZE;
	logical_page_list = (struct logical_page *)malloc(ADDRESSABLE_SSD_PAGES * sizeof (struct logical_page));
	for (unsigned int i=0;i<ADDRESSABLE_SSD_PAGES;i++)
	{
		logical_page_list[i].physical_address.valid = NONE;
	}
	log_write_address.valid = NONE;

	printf("Raw SSD Blocks = %d\n", RAW_SSD_BLOCKS);
	for(unsigned int i=0;i<RAW_SSD_BLOCKS;i++)
	{
		struct ssd_block new_ssd_block;
		new_ssd_block.physical_address = translate_lba_pba(i*BLOCK_SIZE);
		new_ssd_block.physical_address.valid = BLOCK;
		new_ssd_block.last_write_time = 0;
		new_ssd_block.valid_page_count = 0;
		new_ssd_block.lifetime_left = BLOCK_ERASES;
		new_ssd_block.page_mapping = (unsigned int *)malloc(BLOCK_SIZE * sizeof(unsigned int));
		for(unsigned int i=0;i<BLOCK_SIZE;i++)
			new_ssd_block.page_mapping[i] = 0;
		free_block_list.push_back(new_ssd_block);
	}
	clean_threshold = 10;
	age_variance_limit = 1;	
}

FtlImpl_Page::~FtlImpl_Page(void)
{
}
enum status FtlImpl_Page::read(Event &event)
{
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
	controller.stats.numFTLRead++;
	return controller.issue(event, true);
}

enum status FtlImpl_Page::write(Event &event)
{
	unsigned int logical_page_num = event.get_logical_address();
	printf("Got a write at %f\n", event.get_start_time());
	if(!increment_log_write_address(event))
	{
		//fprintf(log_file, "should return failure from write\n");
		return FAILURE; 
	}  
	std::list<struct ssd_block>::iterator iter;
	Address currently_mapped_address = logical_page_list[logical_page_num].physical_address;
	if(currently_mapped_address.valid == PAGE)
	{
		currently_mapped_address.page = 0;
		currently_mapped_address.valid = BLOCK;
		for(iter=allocated_block_list.begin();iter!=allocated_block_list.end();iter++)
		{
			if(iter->physical_address == currently_mapped_address)
			{
				iter->valid_page_count -= 1;
			}
		}
	}
	logical_page_list[logical_page_num].physical_address = log_write_address;
	allocated_block_list.back().last_write_time = latest_write_time++;    
	allocated_block_list.back().valid_page_count += 1;
	allocated_block_list.back().page_mapping[log_write_address.page] = logical_page_num;  
	event.set_address(logical_page_list[logical_page_num].physical_address);
	controller.stats.numFTLWrite++;
	return controller.issue(event, true);
}

enum status FtlImpl_Page::trim(Event &event)
{
	return SUCCESS;
}

enum status FtlImpl_Page::garbage_collect(Event &event)
{
	//printf("collecting garbage\n");
	unsigned int cur_write_lba = event.get_logical_address();
	if(free_block_list.size() == 0)
	{
		printf("garbage collector returned FAILURE \n");
		return FAILURE; 
	}  		
	std::list<struct ssd_block>::iterator iter, max_benefit_block_reference;
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
	average_lifetime_left = average_lifetime_left/(double)(RAW_SSD_PAGES/BLOCK_SIZE);
	printf("average %f, min %f, max %f\n", average_lifetime_left, min_lifetime_left, max_lifetime_left);
	for(iter=allocated_block_list.begin();iter!=allocated_block_list.end();iter++)
	{
		float utilization = (float)iter->valid_page_count/(float)BLOCK_SIZE;
		double age = get_average_age(*iter);
		cur_benefit = (1.0 - utilization)*(float)age / (1.0 + utilization);
		if(iter->lifetime_left == 0)
			continue;  
		//float probab_to_skip = 0;
		//srand(time(NULL));
		if(iter->lifetime_left < age_variance_limit*average_lifetime_left)
		{
			//probab_to_skip = 1.0 - (float)iter->lifetime_left/(age_variance_limit*average_lifetime_left);
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
		return FAILURE;
		//return wear_level(event);
	} 
	struct ssd_block block_to_clean = *max_benefit_block_reference;
	struct ssd_block *cleaning_block = &(free_block_list.front());
	unsigned int page_pointer = 0;
	cleaning_block->valid_page_count += block_to_clean.valid_page_count;
	Address cur_page_address = block_to_clean.physical_address;
	for(unsigned int i=0;i<BLOCK_SIZE;i++)
	{
		cur_page_address.page = i;
		cur_page_address.valid = PAGE; 
		if(cur_page_address == logical_page_list[block_to_clean.page_mapping[i]].physical_address)
		{
			Event read_to_copy(READ, block_to_clean.page_mapping[i], 1, event.get_total_time());
			read_to_copy.set_address(cur_page_address);
			controller.issue(read_to_copy);
			event.incr_time_taken(read_to_copy.get_time_taken());
			Event write_to_copy(WRITE, block_to_clean.page_mapping[i], 1, event.get_total_time());
			Address write_to_address = cleaning_block->physical_address;
			write_to_address.page = page_pointer;
			write_to_address.valid = PAGE;
			write_to_copy.set_address(write_to_address);
			controller.issue(write_to_copy);
			event.incr_time_taken(write_to_copy.get_time_taken());
			logical_page_list[block_to_clean.page_mapping[i]].physical_address = write_to_address;
			cleaning_block->page_mapping[page_pointer] = block_to_clean.page_mapping[i];
			page_pointer += 1;

		}
	}
	block_to_clean.valid_page_count = 0;
	Event erase_event(ERASE, translate_pba_lba(block_to_clean.physical_address), 1, event.get_total_time());
	erase_event.set_address(block_to_clean.physical_address);
	//printf("issuing an erase!!!\n");
	controller.issue(erase_event);
	event.incr_time_taken(erase_event.get_time_taken());
	block_to_clean.lifetime_left -= 1;
	cleaning_block->last_write_time = block_to_clean.last_write_time;
	if(max_benefit_block_reference == allocated_block_list.end())
		printf("This was causing the problem\n");
	allocated_block_list.erase(max_benefit_block_reference);
	free_block_list.push_back(block_to_clean);  
	allocated_block_list.push_back(*cleaning_block);
	free_block_list.pop_front();
	log_write_address = cleaning_block->physical_address;
	log_write_address.page = page_pointer;
	log_write_address.valid = PAGE;
	return SUCCESS;
}

enum status FtlImpl_Page::wear_level(Event &event)
{
	printf("wear leveller called\n\n");
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

	//fprintf(log_file, "THe block to clean chosen for cleaning has %d erases left\n", wear_block_reference->lifetime_left);

	struct ssd_block *cleaning_block = &(free_block_list.front());
	unsigned int page_pointer = 0;
	cleaning_block->valid_page_count += wear_block_reference->valid_page_count;
	Address cur_page_address = wear_block_reference->physical_address;
	//printf("copying from clock to cean to the cleaning block\n");
	for(unsigned int i=0;i<BLOCK_SIZE;i++)
	{
		cur_page_address.page = i;
		cur_page_address.valid = PAGE; 
		if(cur_page_address == logical_page_list[wear_block_reference->page_mapping[i]].physical_address)
		{
			Event read_to_copy(READ, wear_block_reference->page_mapping[i], 1, event.get_total_time());
			read_to_copy.set_address(cur_page_address);
			controller.issue(read_to_copy);
			event.incr_time_taken(read_to_copy.get_time_taken());
			Event write_to_copy(WRITE, wear_block_reference->page_mapping[i], 1, event.get_total_time());
			Address write_to_address = cleaning_block->physical_address;
			write_to_address.page = page_pointer;
			write_to_address.valid = PAGE;
			write_to_copy.set_address(write_to_address);
			controller.issue(write_to_copy);
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
	event.incr_time_taken(erase_event.get_time_taken());
	wear_block_reference->lifetime_left -= 1;
	//fprintf(log_file, "Comparing %d with %d\n", wear_block_reference->lifetime_left, wear_block_reference->lifetime_left);
	cleaning_block->last_write_time = wear_block_reference->last_write_time;
	log_write_address = cleaning_block->physical_address;
	log_write_address.page = page_pointer;
	log_write_address.valid = PAGE;

	struct ssd_block block_to_copy = *healthiest_block_reference;
	page_pointer = 0;
	wear_block_reference->valid_page_count += block_to_copy.valid_page_count;
	cur_page_address = block_to_copy.physical_address;
	//printf("copying from block to copy to block to clean\n");
	for(unsigned int i=0;i<BLOCK_SIZE;i++)
	{
		cur_page_address.page = i;
		cur_page_address.valid = PAGE;
		if(cur_page_address == logical_page_list[block_to_copy.page_mapping[i]].physical_address)
		{
			Event read_to_copy(READ, block_to_copy.page_mapping[i], 1, event.get_total_time());
			read_to_copy.set_address(cur_page_address);
			controller.issue(read_to_copy);
			event.incr_time_taken(read_to_copy.get_time_taken());
			Event write_to_copy(WRITE, block_to_copy.page_mapping[i], 1, event.get_total_time());
			Address write_to_address = wear_block_reference->physical_address;
			write_to_address.page = page_pointer;
			write_to_address.valid = PAGE;
			write_to_copy.set_address(write_to_address);
			controller.issue(write_to_copy);
			event.incr_time_taken(write_to_copy.get_time_taken());
			logical_page_list[block_to_copy.page_mapping[i]].physical_address = write_to_address;
			wear_block_reference->page_mapping[page_pointer] = block_to_copy.page_mapping[i];
			page_pointer += 1;
		}
	}
	//printf("wear levelling done\n");
	block_to_copy.valid_page_count = 0;
	Event copy_event(ERASE, translate_pba_lba(block_to_copy.physical_address), 1, event.get_total_time());
	copy_event.set_address(block_to_copy.physical_address);
	controller.issue(copy_event);
	event.incr_time_taken(copy_event.get_time_taken());
	block_to_copy.lifetime_left -= 1;
	wear_block_reference->last_write_time = block_to_copy.last_write_time;
	allocated_block_list.erase(healthiest_block_reference);
	free_block_list.push_back(block_to_copy);  
	allocated_block_list.push_back(*cleaning_block);
	free_block_list.pop_front();
	return SUCCESS;
}
