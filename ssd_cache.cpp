#include "ssd.h"
#include <stdio.h>

using namespace ssd;


bool CompareCacheEntries::operator()(const std::pair<bool, double> &a, const std::pair<bool, double> &b)
{
	if(CACHE_EVICTION_POLICY == 1)
	{
		if(!a.first && b.first)
			return true;
		else if (a.first && !b.first)
			return false;
	}
	return a.second < b.second;
}

Cache::Cache()
{
	size = CACHE_SIZE;
	cached_pages = (struct cache_entry *)malloc(size * sizeof(struct cache_entry));
	for(unsigned int i=0;i<size;i++)
	{
		cached_pages[i].physical_address.valid = NONE;
	}
	insert_at_location = 0;
}

Cache::Cache(const Cache &c)
{
	fflush(stdout);
}

Cache & Cache::operator=(const Cache &c)
{
	fflush(stdout);
}

Cache::~Cache()
{
	free(cached_pages);
	std::multimap<std::pair<bool, double>, unsigned int>::iterator evict_iter = eviction_map.begin();
}

bool Cache::present_in_cache(Event &event)
{
	unsigned int logical_add = event.get_logical_address();
	std::unordered_map<unsigned int, unsigned int>::iterator iter;
	iter = logical_address_map.find(logical_add);
	Address event_address = event.get_address();
	bool ret_val = iter != logical_address_map.end() && cached_pages[iter->second].physical_address == event_address;
	//printf("Searching for %d. Found %d %d\n", logical_add, ret_val, iter!=logical_address_map.end());
	//printf("Event add ");
	//event_address.print();
	//printf("\n");
	return ret_val;
}

void Cache::place_in_cache(Event &event)
{
	std::unordered_map<unsigned int, unsigned int>::iterator iter;
 	unsigned int logical_add = event.get_logical_address();
	iter = logical_address_map.find(logical_add);
	unsigned int evict_index = size;
	bool target_plane_chosen = false;
	unsigned int target_plane_index = 0;
	assert(logical_address_map.size() == eviction_map.size());
	Address cache_entry_address = event.get_address();
	unsigned int plane_num = cache_entry_address.package*PACKAGE_SIZE*DIE_SIZE + cache_entry_address.die*DIE_SIZE + cache_entry_address.plane;
	bool priority_page = (std::find(priority_planes.begin(), priority_planes.end(), plane_num) != priority_planes.end());
	if(iter != logical_address_map.end())
 	{
		evict_index = iter->second;
		std::pair<bool, double> cur_key(cached_pages[evict_index].evict_priority, cached_pages[evict_index].time);
		std::map<std::pair<bool, double>, unsigned int, CompareCacheEntries>::iterator iter;
		std::pair<	std::map <std::pair<bool, double>, unsigned int>::iterator, 
					std::map <std::pair<bool, double>, unsigned int>::iterator
				 > possible_entries = eviction_map.equal_range(cur_key); 
		for(iter = possible_entries.first; iter != possible_entries.second; iter++)
		{
			if(iter->second == evict_index)
				break;
		}
		assert(iter != possible_entries.second);
		eviction_map.erase(iter);

 	}
	else
 	{
		if(logical_address_map.size() == size)
		{
			evict_index = eviction_map.begin()->second;
			//printf("%d %d\n", i, size);
			//cached_pages[evict_index].physical_address.valid = NONE;
			//
			
			if(cached_pages[evict_index].evict_priority && !priority_page)
			{
				//printf("Won't evict a priority page for a non priority page\n");
				return;
			}
			
			//printf("Removing %d from the map again priority was %d ", cached_pages[evict_index].logical_address, cached_pages[evict_index].evict_priority);
			//cached_pages[evict_index].physical_address.print();
			//printf("\n");
			logical_address_map.erase(cached_pages[evict_index].logical_address);
			eviction_map.erase(eviction_map.begin());
		}
		else
		{
			//TODO: this can be made faster by maintaining a pointer to the last empty index
			//for(unsigned int i=0;i<size;i++)
			//{
			//	if(cached_pages[i].physical_address.valid == NONE)
			//	{
			//		evict_index = i;
			//		break;
			//	}
			//}
			evict_index = insert_at_location;
			insert_at_location++;
		}
	}
	assert(evict_index != size);
	
	

	cached_pages[evict_index].physical_address = event.get_address();
	cached_pages[evict_index].time = event.get_total_time();
	cached_pages[evict_index].logical_address = logical_add;
	cached_pages[evict_index].evict_priority = priority_page;
	logical_address_map[logical_add] = evict_index;
	eviction_map.insert(std::pair<std::pair<bool, double>, unsigned int>(std::pair<bool, double>(cached_pages[evict_index].evict_priority, cached_pages[evict_index].time), evict_index));
	

	//printf("Adding %d at %f with %d for ", cached_pages[evict_index].logical_address, cached_pages[evict_index].time, cached_pages[evict_index].evict_priority);
	//cached_pages[evict_index].physical_address.print();
	//printf("\n");
}

bool Cache::add_priority_plane(unsigned int plane_num)
{
	std::vector<unsigned int>::iterator plane_iterator = std::find(priority_planes.begin(), priority_planes.end(), plane_num);
	if(plane_iterator != priority_planes.end())
		return true;
	//printf("Adding priority plane %d\n", plane_num);
	priority_planes.push_back(plane_num);
	return true;
}

bool Cache::remove_priority_plane(unsigned int plane_num)
{
	std::vector<unsigned int>::iterator plane_iterator = std::find(priority_planes.begin(), priority_planes.end(), plane_num);
	if(plane_iterator == priority_planes.end())
		return false;
	priority_planes.erase(plane_iterator);
	//printf("Removing priority plane %d\n", plane_num);
	for(unsigned int i=0;i<size;i++)
	{
		Address cache_entry_address = cached_pages[i].physical_address;
		if(cache_entry_address.valid == NONE)
			continue;
		unsigned int cache_entry_plane = cache_entry_address.package*PACKAGE_SIZE*DIE_SIZE + cache_entry_address.die*DIE_SIZE + cache_entry_address.plane;
		if(cache_entry_plane == plane_num)
		{
			std::pair<bool, double> cur_key(cached_pages[i].evict_priority, cached_pages[i].time);
			std::map<std::pair<bool, double>, unsigned int, CompareCacheEntries>::iterator iter;
			std::pair<	std::map <std::pair<bool, double>, unsigned int>::iterator, 
						std::map <std::pair<bool, double>, unsigned int>::iterator
					 > possible_entries = eviction_map.equal_range(cur_key); 
			for(iter = possible_entries.first; iter != possible_entries.second; iter++)
			{
				if(iter->second == i)
					break;

			}
			//if(iter == possible_entries.second)
			//{
			//	assert(false);
			//}
			assert(iter != possible_entries.second);
			cached_pages[i].evict_priority = false;
			eviction_map.erase(iter);
			std::pair<bool, double> new_key(cached_pages[i].evict_priority, cached_pages[i].time);
			eviction_map.insert(std::pair<std::pair<bool, double>, unsigned int>(new_key, i));
			/*
		std::multimap<std::pair<bool, double>, unsigned int>::iterator evict_iter = eviction_map.begin();
		for(;evict_iter != eviction_map.end();evict_iter++)
		{
			if(cached_pages[evict_iter->second].logical_address == 27907)
			{
				std::multimap<std::pair<bool, double>, unsigned int>::iterator temp_iter = evict_iter;
				temp_iter++;
				if(temp_iter != eviction_map.end())
					printf("cur next of 27907 is %p with logical %u\n", temp_iter._M_node, cached_pages[temp_iter->second].logical_address);
				else
					printf("cur next of 27909 is the end\n");
			}
		}
		*/
		}
	}
	return true;
}	
