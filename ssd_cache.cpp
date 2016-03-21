#include "ssd.h"
#include <stdio.h>

using namespace ssd;

Cache::Cache()
{
	size = CACHE_SIZE;
	cached_pages = (struct cache_entry *)malloc(size * sizeof(struct cache_entry));
	for(unsigned int i=0;i<size;i++)
	{
		cached_pages[i].physical_address.valid = NONE;
	}
}

Cache::~Cache()
{
	free(cached_pages);
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
	if(iter != logical_address_map.end())
 	{
		evict_index = iter->second;
 	}
	else
 	{
		if(logical_address_map.size() >= size)
		{
			for(unsigned int i=0;i<size;i++)
			{
				if(!cached_pages[i].evict_priority)
				{
					cached_pages[i].physical_address.valid = NONE;
					evict_index = i;
					break;
				}
				else
				{

					Address cache_entry_address = cached_pages[i].physical_address;
					unsigned int plane_num = cache_entry_address.package*PACKAGE_SIZE*DIE_SIZE + cache_entry_address.die*DIE_SIZE + cache_entry_address.plane;
					std::vector<unsigned int>::iterator plane_iterator = std::find(priority_planes.begin(), priority_planes.end(), plane_num);
					assert(plane_iterator != priority_planes.end());
					unsigned int plane_index = std::distance(priority_planes.begin(), plane_iterator);
					if(!target_plane_chosen || plane_index < target_plane_index)
					{
						target_plane_chosen = true;
						evict_index = i;
						target_plane_index = plane_index;
					}
				}
			}
			cached_pages[evict_index].physical_address.valid = NONE;
			//printf("Removing %d from the map again priority was %d\n", cached_pages[evict_index].logical_address, cached_pages[evict_index].evict_priority);
			logical_address_map.erase(cached_pages[evict_index].logical_address);
		}
		else
		{
			for(unsigned int i=0;i<size;i++)
			{
				if(cached_pages[i].physical_address.valid == NONE)
				{
					evict_index = i;
					break;
				}
			}
		}
	}
	assert(evict_index != size);
	
	/*
	if(cached_pages[evict_index].physical_address.valid != NONE)
	{
		printf("Updating %d %f %d ", cached_pages[evict_index].logical_address, cached_pages[evict_index].time, cached_pages[evict_index].evict_priority);
		Address cache_entry_address = cached_pages[evict_index].physical_address;
		unsigned int plane_num = cache_entry_address.package*PACKAGE_SIZE*DIE_SIZE + cache_entry_address.die*DIE_SIZE + cache_entry_address.plane;
		cached_pages[evict_index].physical_address.print();
		printf(" %d\n", plane_num);

	}
	*/
	

	cached_pages[evict_index].physical_address = event.get_address();
	cached_pages[evict_index].time = event.get_total_time();
	cached_pages[evict_index].logical_address = logical_add;
	Address cache_entry_address = event.get_address();
	unsigned int plane_num = cache_entry_address.package*PACKAGE_SIZE*DIE_SIZE + cache_entry_address.die*DIE_SIZE + cache_entry_address.plane;
	cached_pages[evict_index].evict_priority = (std::find(priority_planes.begin(), priority_planes.end(), plane_num) != priority_planes.end());
	logical_address_map[logical_add] = evict_index;
	
	//printf("Adding %d %f %d ", cached_pages[evict_index].logical_address, cached_pages[evict_index].time, cached_pages[evict_index].evict_priority);
	//cached_pages[evict_index].physical_address.print();
	//printf(" %d\n", plane_num);
	
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
		unsigned int cache_entry_plane = cache_entry_address.package*PACKAGE_SIZE*DIE_SIZE + cache_entry_address.die*DIE_SIZE + cache_entry_address.plane;
		if(cache_entry_plane == plane_num)
		{
			cached_pages[i].evict_priority = false;
		}
	}
	return true;
}
