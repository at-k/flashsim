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

bool Cache::present_in_cache(Event &event)
{
	unsigned int logical_add = event.get_logical_address();
	std::unordered_map<unsigned int, double>::iterator iter;
	iter = reverse_map.find(logical_add);
	bool ret_val = iter != reverse_map.end();
	return ret_val;
}

void Cache::place_in_cache(Event &event)
{
	Address iter_address = event.get_address();
//	unsigned int iter_plane = iter_address.package*PACKAGE_SIZE*DIE_SIZE + iter_address.die*DIE_SIZE + iter_address.plane;
//	printf("Placing %d in cache for plane %d\n", event.get_logical_address(), iter_plane);
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
//	printf("inserted %d in cache\n", logical_add);
	while(actual_cache.size() > size)
	{
		std::map<double, unsigned int>::iterator lru;
		lru = actual_cache.begin();
		reverse_map.erase(lru->second);
		actual_cache.erase(lru);
//		printf("removed %d from cache\n", lru->second);
	}
}

void Cache::invalidate(Event &event)
{
	unsigned int logical_add = event.get_logical_address();
		//if(present_in_cache(event, actual_time))
		//{
	Address iter_address = event.get_address();
	unsigned int iter_plane = iter_address.package*PACKAGE_SIZE*DIE_SIZE + iter_address.die*DIE_SIZE + iter_address.plane;
//	printf("Write placing %d in cache for plane %d\n", event.get_logical_address(), iter_plane);
	insert(logical_add, event.get_total_time());
		//}
}

