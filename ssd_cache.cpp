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

}

Cache::Cache(const Cache &c)
{
	printf("copy constructor called\n");
	fflush(stdout);
}

Cache & Cache::operator=(const Cache &c)
{
	printf("operator = called\n");
	fflush(stdout);
}

Cache::~Cache()
{
	free(cached_pages);
	std::multimap<std::pair<bool, double>, unsigned int>::iterator evict_iter = eviction_map.begin();
	//int c = 0;
	//for(;evict_iter != eviction_map.end();)
	//{
	//	printf("deleting %d\n", c);
	//	evict_iter = eviction_map.erase(evict_iter);
	//	c++;
	//}
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
	else
 	{
		if(logical_address_map.size() == size)
		{
			/*
			unsigned int i;
			for(i=0;i<size;i++)
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
			*/
			evict_index = eviction_map.begin()->second;
			//printf("%d %d\n", i, size);
			//cached_pages[evict_index].physical_address.valid = NONE;
			//printf("Removing %d from the map again priority was %d\n", cached_pages[evict_index].logical_address, cached_pages[evict_index].evict_priority);
			logical_address_map.erase(cached_pages[evict_index].logical_address);
			//eviction_map.erase(std::pair<bool, double>(cached_pages[evict_index].evict_priority, cached_pages[evict_index].time));
			/*
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
			*/
			/*
			printf("size before %u\n=======BEFORE\n", eviction_map.size());
			std::multimap<std::pair<bool, double>, unsigned int>::iterator evict_iter = eviction_map.begin();
			unsigned int cc = 0;
			if(logical_add == 47315)
			{
				printf("Evicting %u for 47315\n", cached_pages[evict_index].logical_address);
			}
			for(;evict_iter != eviction_map.end();evict_iter++)
			{
				if(logical_add==47315)
				{
					printf("%u\n", cc);
					fflush(stdout);
				}
				if(cached_pages[evict_iter->second].logical_address == 27907)
				{
					std::multimap<std::pair<bool, double>, unsigned int>::iterator temp_iter = evict_iter;
					printf("am gonna break now\n");
					fflush(stdout);
					temp_iter++;
					if(temp_iter != eviction_map.end())
						printf("cur next of 27907 is %p with logical %u\n", temp_iter._M_node, cached_pages[temp_iter->second].logical_address);
					else
						printf("cur next of 27909 is the end\n");
					fflush(stdout);
				}
				cc++;
			}
			*/
			eviction_map.erase(eviction_map.begin());
			/*
			printf("===================AFTER\n");
			printf("size after %u\n", eviction_map.size());
			printf("inserting %d\n", logical_add);
			fflush(stdout);
			evict_iter = eviction_map.begin();
			cc = 0;
			if(logical_add == 47315)
			{
				printf("Evicting %u for 47315\n", cached_pages[evict_index].logical_address);
			}
			for(;evict_iter != eviction_map.end();evict_iter++)
			{
				if(logical_add==47315)
				{
					printf("%u\n", cc);
					fflush(stdout);
				}
				if(cached_pages[evict_iter->second].logical_address == 27907)
				{
					std::multimap<std::pair<bool, double>, unsigned int>::iterator temp_iter = evict_iter;
					printf("am gonna break now\n");
					fflush(stdout);
					temp_iter++;
					if(temp_iter != eviction_map.end())
						printf("cur next of 27907 is %p with logical %u\n", temp_iter._M_node, cached_pages[temp_iter->second].logical_address);
					else
						printf("cur next of 27909 is the end\n");
					fflush(stdout);
				}
				bool b = evict_iter->first.first;
				double d = evict_iter->first.second;
				unsigned int u = evict_iter->second;
				cc++;
			}
			*/
			//printf("Evicting %d %f %d\n", cached_pages[evict_index].evict_priority, cached_pages[evict_index].time, cached_pages[evict_index].logical_address);
			//printf("%d %d\n", logical_address_map.size(), eviction_map.size());
		}
		else
		{
			//TODO: this can be made faster by maintaining a pointer to the last empty index
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
	eviction_map.insert(std::pair<std::pair<bool, double>, unsigned int>(std::pair<bool, double>(cached_pages[evict_index].evict_priority, cached_pages[evict_index].time), evict_index));
	
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
	//printf("%d %d\n", logical_address_map.size(), eviction_map.size());

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
