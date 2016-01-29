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
	while(actual_cache.size() > size)
	{
		std::map<double, unsigned int>::iterator lru;
		lru = actual_cache.begin();
		reverse_map.erase(lru->second);
		actual_cache.erase(lru);
	}
}

void Cache::invalidate(Event &event, bool actual_time)
{
	unsigned int logical_add = event.get_logical_address();
	if(actual_time)
	{
		std::unordered_map<unsigned int, double>::iterator iter;
		iter = reverse_map.find(logical_add);
		if(iter != reverse_map.end())
		{
			actual_cache.erase(iter->second);
			reverse_map.erase(iter);
		}
	}
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
