#include "ssd.h"
#include <stdio.h>

using namespace ssd;

Cache::Cache()
{
	size = CACHE_SIZE;
	cache = (struct cache_entry *)malloc(size * sizeof(struct cache_entry));
	for(unsigned int i=0;i<size;i++)
	{
		cache[i].physical_address.valid = NONE;
	}
}

Cache::~Cache()
{
	free(cache);
}

bool Cache::present_in_cache(Event &event)
{
	unsigned int logical_add = event.get_logical_address();
	std::unordered_map<unsigned int, unsigned int>::iterator iter;
	iter = logical_address_map.find(logical_add);
	if(iter != logical_address_map.end())
		assert(cache[iter->second].physical_address == event.get_address());
	return iter != logical_address_map.end();
}

void Cache::place_in_cache(Event &event)
{
	std::unordered_map<unsigned int, unsigned int>::iterator iter;
	unsigned int logical_add = event.get_logical_address();
	iter = logical_address_map.find(logical_add);
	unsigned int evict_index = size;
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
				if(!cache[i].evict_priority)
				{
					cache[i].physical_address.valid = NONE;
					logical_address_map.erase(cache[i].logical_address);
					evict_index = i;
					break;
				}
				else
				{

					Address cache_entry_address = event.get_address();
					unsigned int plane_num = cache_entry_address.package*PACKAGE_SIZE*DIE_SIZE + cache_entry_address.die*DIE_SIZE + cache_entry_address.plane;
					std::vector<unsigned int>::iterator plane_iterator = std::find(priority_planes.begin(), priority_planes.end(), plane_num);
					assert(plane_iterator != priority_planes.end());
					unsigned int plane_index = std::distance(priority_planes.begin(), plane_iterator);
					if(evict_index == size || plane_index < evict_index)
					{
						evict_index = plane_index;
					}
				}
			}
			cache[evict_index].physical_address.valid = NONE;
			logical_address_map.erase(cache[evict_index].logical_address);
		}
		else
		{
			for(unsigned int i=0;i<size;i++)
			{
				if(cache[i].physical_address.valid == NONE)
				{
					evict_index = i;
					break;
				}
			}
		}
	}
	assert(evict_index != size);
	
	cache[evict_index].physical_address = event.get_address();
	cache[evict_index].time = event.get_total_time();
	cache[evict_index].logical_address = logical_add;
	Address cache_entry_address = event.get_address();
	unsigned int plane_num = cache_entry_address.package*PACKAGE_SIZE*DIE_SIZE + cache_entry_address.die*DIE_SIZE + cache_entry_address.plane;
	cache[iter->second].evict_priority = (std::find(priority_planes.begin(), priority_planes.end(), plane_num) != priority_planes.end());
}


bool Cache::add_priority_plane(unsigned int plane_num)
{
	std::vector<unsigned int>::iterator plane_iterator = std::find(priority_planes.begin(), priority_planes.end(), plane_num);
	if(plane_iterator != priority_planes.end())
		return true;
	priority_planes.push_back(plane_num);
	return true;
}

bool Cache::remove_priority_plane(unsigned int plane_num)
{
	std::vector<unsigned int>::iterator plane_iterator = std::find(priority_planes.begin(), priority_planes.end(), plane_num);
	if(plane_iterator == priority_planes.end())
		return false;
	priority_planes.erase(plane_iterator);
	return true;
}
