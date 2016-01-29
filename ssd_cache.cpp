#include "ssd.h"
#include <stdio.h>

using namespace ssd;

Cache::Cache()
{
	size = CACHE_SIZE;
}

Cache::~Cache()
{
}

bool Cache::present_in_cache(Event &event, bool actual_time)
{
	if(!actual_time)
		return false;
	process_future(event);
	unsigned int logical_add = event.get_logical_address();
	std::unordered_map<unsigned int, double>::iterator iter;
	iter = reverse_map.find(logical_add);
	return iter != reverse_map.end();
}

void Cache::place_in_cache(Event &event, bool actual_time)
{
	if(actual_time)
		process_future(event);
	else
		return place_in_future_cache(event); 
	unsigned int logical_add = event.get_logical_address();
	return insert(logical_add, event.get_total_time());
}

void Cache::insert(unsigned int logical_add, double time)
{
	std::unordered_map<unsigned int, double>::iterator iter;
	iter = reverse_map.find(logical_add);
	if(iter != reverse_map.end())
	{
		actual_cache.erase(iter->second);
	}
	actual_cache[time] = logical_add;
	reverse_map[logical_add] = time;
	/*
	actual_cached_addresses.push_back();
	if(actual_time && present_in_cache(event, actual_time))
		return;
	struct cache_entry new_cache_entry;
	new_cache_entry.address = event.get_logical_address();
	new_cache_entry.entry_time = event.get_total_time();
	std::list<struct cache_entry>::iterator iter, pointer, last;
	last = cached_addresses.end();
	for(iter=cached_addresses.begin();iter!=last;iter++)
	{
		if((*iter).entry_time > event.get_total_time())
			break;
	}
	pointer = cached_addresses.insert(iter, new_cache_entry);
	if(actual_time)
	{
		unsigned int num_elements = std::distance(cached_addresses.begin(), pointer) + 1;
		while(num_elements > size)
		{
			cached_addresses.pop_front();
			num_elements--;
		}
	}
	*/
}

void Cache::process_future(Event &event)
{
	std::map<double, unsigned int>::iterator iter;
	double start_time = event.get_start_time();
	for(iter = future_cache.begin();iter!=future_cache.end();)
	{
		if(iter->first > start_time)
			break;
		insert(iter->second, iter->first);
		iter = future_cache.erase(iter);
	}
}	

void Cache::place_in_future_cache(Event &event)
{
	future_cache[event.get_total_time()] = event.get_logical_address();
}
