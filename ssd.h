/* Copyright 2009, 2010 Brendan Tauras */

/* ssd.h is part of FlashSim. */

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

/* ssd.h
 * Brendan Tauras 2010-07-16
 * Main SSD header file
 * 	Lists definitions of all classes, structures,
 * 		typedefs, and constants used in ssd namespace
 *		Controls options, such as debug asserts and test code insertions
 */

#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <queue>
#include <list>
#include <map>
#include <unordered_map>
#include <time.h>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <limits>
 
#ifndef _SSD_H
#define _SSD_H

namespace ssd {

/* define exit codes for errors */
#define MEM_ERR -1
#define FILE_ERR -2

/* Uncomment to disable asserts for production */
#define NDEBUG


/* some obvious typedefs for laziness */
typedef unsigned int uint;
typedef unsigned long ulong;


/* Simulator configuration from ssd_config.cpp */

/* Configuration file parsing for extern config variables defined below */
void load_entry(char *name, double value, uint line_number);
void load_config(void);
void print_config(FILE *stream);

/* Ram class:
 * 	delay to read from and write to the RAM for 1 page of data */
extern const double RAM_READ_DELAY;
extern const double RAM_WRITE_DELAY;

/* Bus class:
 * 	delay to communicate over bus
 * 	max number of connected devices allowed
 * 	flag value to detect free table entry (keep this negative)
 * 	number of time entries bus has to keep track of future schedule usage
 * 	number of simultaneous communication channels - defined by SSD_SIZE */
extern const double BUS_CTRL_DELAY;
extern const double BUS_DATA_DELAY;
extern const uint BUS_MAX_CONNECT;
extern const double BUS_CHANNEL_FREE_FLAG;
extern const uint BUS_TABLE_SIZE;
/* extern const uint BUS_CHANNELS = 4; same as # of Packages, defined by SSD_SIZE */
/*Plane class
 *Signifies that the plane is not undergoing any operation
 * */
extern const double PLANE_INACTIVE_FLAG;
/* Ssd class:
 * 	number of Packages per Ssd (size) */
extern const uint SSD_SIZE;

/* Package class:
 * 	number of Dies per Package (size) */
extern const uint PACKAGE_SIZE;

/* Die class:
 * 	number of Planes per Die (size) */
extern const uint DIE_SIZE;

/* Plane class:
 * 	number of Blocks per Plane (size)
 * 	delay for reading from plane register
 * 	delay for writing to plane register
 * 	delay for merging is based on read, write, reg_read, reg_write 
 * 		and does not need to be explicitly defined */
extern const uint PLANE_SIZE;
extern const double PLANE_REG_READ_DELAY;
extern const double PLANE_REG_WRITE_DELAY;

/* Block class:
 * 	number of Pages per Block (size)
 * 	number of erases in lifetime of block
 * 	delay for erasing block */
extern const uint BLOCK_SIZE;
extern const uint BLOCK_ERASES;
extern const double BLOCK_ERASE_DELAY;

/*Cache Class
	Size of the cache	
*/
extern const uint CACHE_SIZE;


/* Page class:
 * 	delay for Page reads
 * 	delay for Page writes */
extern const double PAGE_READ_DELAY;
extern const double PAGE_WRITE_DELAY;
extern const uint PAGE_SIZE;
extern const bool PAGE_ENABLE_DATA;


extern const uint OVERPROVISIONING;
/*
 * Mapping directory
 */
extern const uint MAP_DIRECTORY_SIZE;

/*
 * FTL Implementation
 */
extern const uint FTL_IMPLEMENTATION;

/* GC SCHME */
extern const uint GC_SCHEME;

/* Maximum blocks to queue up for cleaning, 0 means no restriction */
extern const uint MAX_GC_BLOCKS;

/* Maximum planes to clean in parallel, 0 means no restriction */
extern const uint MAX_GC_PLANES;

/* Maximum blocks to clean in one call to GC */
extern const uint MAX_BLOCKS_PER_GC;

/* Minimum blocks to clean in one call the GC */
extern const uint MIN_BLOCKS_PER_GC;

/* Cache eviction policy 0->LRU, 1->LRU + Plane Priority */
extern const uint CACHE_EVICTION_POLICY;

/* number of pages to write in a block before moving to 
 * next plane while zeroing out the ssd in ftl init
 * if set to 0, the ssd is not zeroed out
 * */
extern const uint STRIPE_SIZE;

extern const uint NUMBER_OF_ADDRESSABLE_BLOCKS;

enum ftl_implementation {IMPL_PAGE, IMPL_FAST};


/*
 * Memory area to support pages with data.
 */
extern void *page_data;
extern void *global_buffer;

/* Enumerations to clarify status integers in simulation
 * Do not use typedefs on enums for reader clarity */

/* Page states
 * 	empty   - page ready for writing (and contains no valid data)
 * 	valid   - page has been written to and contains valid data
 * 	invalid - page has been written to and does not contain valid data */
enum page_state{EMPTY, VALID, INVALID};

/* Block states
 * 	free     - all pages in block are empty
 * 	active   - some pages in block are valid, others are empty or invalid
 * 	inactive - all pages in block are invalid */
enum block_state{FREE, ACTIVE, INACTIVE};

/* I/O request event types
 * 	read  - read data from address
 * 	write - write data to address (page state set to valid)
 * 	erase - erase block at address (all pages in block are erased - 
 * 	                                page states set to empty)
 * 	merge - move valid pages from block at address (page state set to invalid)
 * 	           to free pages in block at merge_address */
enum event_type{READ, WRITE, ERASE, MERGE, TRIM, NOOP};

/* General return status
 * return status for simulator operations that only need to provide general
 * failure notifications */
enum status{FAILURE, SUCCESS};

/* Address valid status
 * used for the valid field in the address class
 * example: if valid == BLOCK, then
 * 	the package, die, plane, and block fields are valid
 * 	the page field is not valid */
enum address_valid{NONE, PACKAGE, DIE, PLANE, BLOCK, PAGE};



/*
 * Block type status
 * used for the garbage collector specify what pool
 * it should work with.
 * the block types are log, data and map (Directory map usually)
 */
enum block_type {LOG, DATA};


#define BOOST_MULTI_INDEX_ENABLE_SAFE_MODE 1

/* List classes up front for classes that have references to their "parent"
 * (e.g. a Package's parent is a Ssd).
 *
 * The order of definition below follows the order of this list to support
 * cases of agregation where the agregate class should be defined first.
 * Defining the agregate class first enables use of its non-default
 * constructors that accept args
 * (e.g. a Ssd contains a Controller, Ram, Bus, and Packages). */
class Address;
class Stats;
class Event;
class Channel;
class Bus;
class Page;
class Block;
class Plane;
class Die;
class Package;
class Garbage_Collector;
class Wear_Leveler;
class Block_manager;
class FtlParent;
class FtlImpl_Page;
class FtlImpl_Fast;

class Ram;
class Controller;
class Ssd;
class Cache;



/* Class to manage physical addresses for the SSD.  It was designed to have
 * public members like a struct for quick access but also have checking,
 * printing, and assignment functionality.  An instance is created for each
 * physical address in the Event class. */
class Address
{
public:
	uint package;
	uint die;
	uint plane;
	uint block;
	uint page;
	ulong real_address;
	enum address_valid valid;
	Address(void);
	Address(const Address &address);
	Address(const Address *address);
	Address(uint package, uint die, uint plane, uint block, uint page, enum address_valid valid);
	Address(uint address, enum address_valid valid);
	~Address();
	enum address_valid check_valid(uint ssd_size = SSD_SIZE, uint package_size = PACKAGE_SIZE, uint die_size = DIE_SIZE, uint plane_size = PLANE_SIZE, uint block_size = BLOCK_SIZE);
	enum address_valid compare(const Address &address) const;
	void print(FILE *stream = stdout);

	void operator+(int);
	void operator+(uint);
	Address &operator+=(const uint rhs);
	Address &operator=(const Address &rhs);
	bool operator==(const Address &rhs);

	void set_linear_address(ulong address, enum address_valid valid);
	void set_linear_address(ulong address);
	ulong get_linear_address() const;
};

class Stats
{
public:
	
	long numRead;
	long numWrite;
	long numErase;
	long minErase;
	long maxErase;
	
	// Flash Translation Layer
	/**** These counters are not maintained *********/
	//TODO Remove the unmaintained counters
	long numFTLRead;
	long numFTLWrite;
	long numFTLErase;
	long numFTLTrim;

	// Garbage Collection
	long numGCRead;
	long numGCWrite;
	long numGCErase;

	// Wear-leveling
	long numWLRead;
	long numWLWrite;
	long numWLErase;

	// Log based FTL's
	long numLogMergeSwitch;
	long numLogMergePartial;
	long numLogMergeFull;

	// Page based FTL's
	long numPageBlockToPageConversion;

	// Cache based FTL's
	long numCacheHits;
	long numCacheFaults;

	// Memory consumptions (Bytes)
	long numMemoryTranslation;
	long numMemoryCache;

	long numMemoryRead;
	long numMemoryWrite;

	// Advance statictics
	double translation_overhead() const;
	double variance_of_io() const;
	double cache_hit_ratio() const;

	// Constructors, maintainance, output, etc.
	Stats(void);

	void print_statistics();
	void reset_statistics();
	void write_statistics(FILE *stream);
	void write_header(FILE *stream);
private:
	void reset();
};

/* Class to emulate a log block with page-level mapping. */
class LogPageBlock
{
public:
	LogPageBlock(void);
	~LogPageBlock(void);

	int *pages;
	long *aPages;
	Address address;
	int numPages;

	LogPageBlock *next;

	bool operator() (const ssd::LogPageBlock& lhs, const ssd::LogPageBlock& rhs) const;
	bool operator() (const ssd::LogPageBlock*& lhs, const ssd::LogPageBlock*& rhs) const;
};


/* Class to manage I/O requests as events for the SSD.  It was designed to keep
 * track of an I/O request by storing its type, addressing, and timing.  The
 * SSD class creates an instance for each I/O request it receives. */
class Event 
{
public:
	Event(enum event_type type, ulong logical_address, uint size, double start_time);
	~Event(void);
	void consolidate_metaevent(Event &list);
	ulong get_logical_address(void) const;
	const Address &get_address(void) const;
	const Address &get_merge_address(void) const;
	const Address &get_log_address(void) const;
	const Address &get_replace_address(void) const;
	uint get_size(void) const;
	enum event_type get_event_type(void) const;
	double get_start_time(void) const;
	double get_time_taken(void) const;
	double get_total_time(void) const;
	double get_bus_wait_time(void) const;
	bool get_noop(void) const;
	Event *get_next(void) const;
	void set_address(const Address &address);
	void set_merge_address(const Address &address);
	void set_log_address(const Address &address);
	void set_replace_address(const Address &address);
	void set_next(Event &next);
	void set_payload(void *payload);
	void set_event_type(const enum event_type &type);
	void set_noop(bool value);
	void *get_payload(void) const;
	double incr_bus_wait_time(double time);
	double incr_time_taken(double time_incr);
	void print(FILE *stream = stdout);
private:
	double start_time;
	double time_taken;
	double bus_wait_time;
	enum event_type type;

	ulong logical_address;
	Address address;
	Address merge_address;
	Address log_address;
	Address replace_address;
	uint size;
	void *payload;
	Event *next;
	bool noop;
};

struct lock_times {
	enum event_type type;
	double lock_time;
	double unlock_time;
};
/* Single bus channel
 * Simulate multiple devices on 1 bus channel with variable bus transmission
 * durations for data and control delays with the Channel class.  Provide the 
 * delay times to send a control signal or 1 page of data across the bus
 * channel, the bus table size for the maximum number channel transmissions that
 * can be queued, and the maximum number of devices that can connect to the bus.
 * To elaborate, the table size is the size of the channel scheduling table that
 * holds start and finish times of events that have not yet completed in order
 * to determine where the next event can be scheduled for bus utilization. */
class Channel
{
public:
	Channel(double ctrl_delay = BUS_CTRL_DELAY, double data_delay = BUS_DATA_DELAY, uint table_size = BUS_TABLE_SIZE, uint max_connections = BUS_MAX_CONNECT);
	~Channel(void);
	enum status lock(double start_time, double duration, Event &event);
	enum status connect(void);
	enum status disconnect(void);
	double ready_time(void);
	void unlock(double current_time);


	static bool timings_sorter(lock_times const& lhs, lock_times const& rhs);
	std::vector<lock_times> timings;

private:
	uint table_entries;
	uint selected_entry;
	uint num_connected;
	uint max_connections;
	double ctrl_delay;
	double data_delay;

	// Stores the highest unlock_time in the vector timings list.
	double ready_at;
};

/* Multi-channel bus comprised of Channel class objects
 * Simulates control and data delays by allowing variable channel lock
 * durations.  The sender (controller class) should specify the delay (control,
 * data, or both) for events (i.e. read = ctrl, ctrl+data; write = ctrl+data;
 * erase or merge = ctrl).  The hardware enable signals are implicitly
 * simulated by the sender locking the appropriate bus channel through the lock
 * method, then sending to multiple devices by calling the appropriate method
 * in the Package class. */
class Bus
{
public:
	Bus(uint num_channels = SSD_SIZE, double ctrl_delay = BUS_CTRL_DELAY, double data_delay = BUS_DATA_DELAY, uint table_size = BUS_TABLE_SIZE, uint max_connections = BUS_MAX_CONNECT);
	~Bus(void);
	enum status lock(uint channel, double start_time, double duration, Event &event);
	enum status connect(uint channel);
	enum status disconnect(uint channel);
	Channel &get_channel(uint channel);
	double ready_time(uint channel);
private:
	uint num_channels;
	Channel * const channels;
};



/* The page is the lowest level data storage unit that is the size unit of
 * requests (events).  Pages maintain their state as events modify them. */
class Page 
{
public:
	Page(const Block &parent, double read_delay = PAGE_READ_DELAY, double write_delay = PAGE_WRITE_DELAY);
	~Page(void);
	enum status _read(Event &event);
	enum status _write(Event &event);
	const Block &get_parent(void) const;
	enum page_state get_state(void) const;
	void set_state(enum page_state state);
private:
	enum page_state state;
	const Block &parent;
	double read_delay;
	double write_delay;
};

/* The block is the data storage hardware unit where erases are implemented.
 * Blocks maintain wear statistics for the FTL. */
class Block 
{
public:
	long physical_address;
	uint pages_invalid;
	Block(const Plane &parent, uint size = BLOCK_SIZE, ulong erases_remaining = BLOCK_ERASES, double erase_delay = BLOCK_ERASE_DELAY, long physical_address = 0);
	~Block(void);
	enum status read(Event &event);
	enum status write(Event &event);
	enum status replace(Event &event);
	enum status _erase(Event &event);
	const Plane &get_parent(void) const;
	uint get_pages_valid(void) const;
	uint get_pages_invalid(void) const;
	enum block_state get_state(void) const;
	enum page_state get_state(uint page) const;
	enum page_state get_state(const Address &address) const;
	double get_last_erase_time(void) const;
	double get_modification_time(void) const;
	ulong get_erases_remaining(void) const;
	uint get_size(void) const;
	enum status get_next_page(Address &address) const;
	void invalidate_page(uint page);
	long get_physical_address(void) const;
	Block *get_pointer(void);
	block_type get_block_type(void) const;
	void set_block_type(block_type value);

private:
	uint size;
	Page * const data;
	const Plane &parent;
	uint pages_valid;
	enum block_state state;
	ulong erases_remaining;
	double last_erase_time;
	double erase_delay;
	double modification_time;

	block_type btype;
};

/* The plane is the data storage hardware unit that contains blocks.
 * Plane-level merges are implemented in the plane.  Planes maintain wear
 * statistics for the FTL. */
class Plane 
{
public:
	Plane(const Die &parent, uint plane_size = PLANE_SIZE, double reg_read_delay = PLANE_REG_READ_DELAY, double reg_write_delay = PLANE_REG_WRITE_DELAY, long physical_address = 0);
	~Plane(void);
	enum status read(Event &event);
	enum status write(Event &event);
	enum status erase(Event &event);
	enum status replace(Event &event);
	enum status _merge(Event &event);
	const Die &get_parent(void) const;
	double get_last_erase_time(const Address &address) const;
	ulong get_erases_remaining(const Address &address) const;
	void get_least_worn(Address &address) const;
	uint get_size(void) const;
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	ssd::uint get_num_free(const Address &address) const;
	ssd::uint get_num_valid(const Address &address) const;
	ssd::uint get_num_invalid(const Address &address) const;
	Block *get_block_pointer(const Address & address);
	void unlock(double start_time);
	static bool timings_sorter(lock_times const& lhs, lock_times const& rhs);
	std::vector<lock_times> timings;
private:
	void update_wear_stats(void);
	void serialize_access(double start_time, double duration, Event &event);
	enum status get_next_page(void);
	uint size;
	Block * const data;
	const Die &parent;
	uint least_worn;
	ulong erases_remaining;
	double last_erase_time;
	double reg_read_delay;
	double reg_write_delay;
	Address next_page;
	uint free_blocks;
	const int PLANE_NOOP;
};

/* The die is the data storage hardware unit that contains planes and is a flash
 * chip.  Dies maintain wear statistics for the FTL. */
class Die 
{
public:
	Die(const Package &parent, Channel &channel, uint die_size = DIE_SIZE, long physical_address = 0);
	~Die(void);
	enum status read(Event &event);
	enum status write(Event &event);
	enum status erase(Event &event);
	enum status replace(Event &event);
	enum status merge(Event &event);
	enum status _merge(Event &event);
	const Package &get_parent(void) const;
	double get_last_erase_time(const Address &address) const;
	ulong get_erases_remaining(const Address &address) const;
	void get_least_worn(Address &address) const;
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	ssd::uint get_num_free(const Address &address) const;
	ssd::uint get_num_valid(const Address &address) const;
	ssd::uint get_num_invalid(const Address &address) const;
	Block *get_block_pointer(const Address & address);
private:
	void update_wear_stats(const Address &address);
	uint size;
	Plane * const data;
	const Package &parent;
	Channel &channel;
	uint least_worn;
	ulong erases_remaining;
	double last_erase_time;
};

/* The package is the highest level data storage hardware unit.  While the
 * package is a virtual component, events are passed through the package for
 * organizational reasons, including helping to simplify maintaining wear
 * statistics for the FTL. */
class Package 
{
public:
	Package (const Ssd &parent, Channel &channel, uint package_size = PACKAGE_SIZE, long physical_address = 0);
	~Package ();
	enum status read(Event &event);
	enum status write(Event &event);
	enum status erase(Event &event);
	enum status replace(Event &event);
	enum status merge(Event &event);
	const Ssd &get_parent(void) const;
	double get_last_erase_time (const Address &address) const;
	ulong get_erases_remaining (const Address &address) const;
	void get_least_worn (Address &address) const;
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	ssd::uint get_num_free(const Address &address) const;
	ssd::uint get_num_valid(const Address &address) const;
	ssd::uint get_num_invalid(const Address &address) const;
	Block *get_block_pointer(const Address & address);
private:
	void update_wear_stats (const Address &address);
	uint size;
	Die * const data;
	const Ssd &parent;
	uint least_worn;
	ulong erases_remaining;
	double last_erase_time;
};

/* place-holder definitions for GC, WL, FTL, RAM, Controller 
 * please make sure to keep this order when you replace with your definitions */
class Garbage_collector 
{
public:
	Garbage_collector(FtlParent &ftl);
	~Garbage_collector(void);
private:
	void clean(Address &address);
};

class Wear_leveler 
{
public:
	Wear_leveler(FtlParent &FTL);
	~Wear_leveler(void);
	enum status insert(const Address &address);
};

class Block_manager
{
public:
	Block_manager(FtlParent *ftl);
	~Block_manager(void);

	// Usual suspects
	Address get_free_block(Event &event);
	Address get_free_block(block_type btype, Event &event);
	void invalidate(Address address, block_type btype);
	void print_statistics();
	void insert_events(Event &event);
	void change_block_type(Address block_address, block_type new_type);
	bool is_log_full();
	void erase_and_invalidate(Event &event, Address &address, block_type btype);
	int get_num_free_blocks();

	// Used to update GC on used pages in blocks.
	void update_block(Block * b);

	// Singleton
	static Block_manager *instance();
	static void instance_initialize(FtlParent *ftl);
	static Block_manager *inst;

	void cost_insert(Block *b);

	void print_cost_status();
	void init_free_list(Event &event);


private:
	void get_page_block(Address &address, Event &event);
	static bool block_comparitor_simple (Block const *x,Block const *y);

	FtlParent *ftl;

	ulong data_active;
	ulong log_active;
	ulong logseq_active;

	ulong max_log_blocks;
	ulong max_blocks;

	ulong max_map_pages;
	ulong map_space_capacity;

	// Cost/Benefit priority queue.
	typedef boost::multi_index_container<
			Block*,
			boost::multi_index::indexed_by<
				boost::multi_index::random_access<>,
				boost::multi_index::ordered_non_unique<BOOST_MULTI_INDEX_MEMBER(Block,uint,pages_invalid) >
		  >
		> active_set;

	typedef active_set::nth_index<0>::type ActiveBySeq;
	typedef active_set::nth_index<1>::type ActiveByCost;

	active_set active_cost;

	// Usual block lists
	std::vector<Block*> active_list;
	std::vector<Block*> free_list;
	std::vector<Block*> invalid_list;

	// Counter for returning the next free page.
	ulong directoryCurrentPage;
	// Address on the current cached page in SRAM.
	ulong directoryCachedPage;

	ulong simpleCurrentFree;

	// Counter for handling periodic sort of active_list
	uint num_insert_events;

	uint current_writing_block;

	bool inited;

	bool out_of_blocks;
};

class FtlParent
{
public:
	FtlParent(Controller &controller, Ssd &parent);

	virtual ~FtlParent () {};
	virtual enum status read(Event &event, bool &op_complete, double &end_time) = 0;
	virtual enum status write(Event &event, bool &op_complete, double &end_time) = 0;
	virtual enum status noop(Event &event, bool &op_complete, double &end_time) = 0;
	virtual enum status trim(Event &event) = 0;
	virtual void cleanup_block(Event &event, Block *block);

	virtual void print_ftl_statistics();

	virtual void get_min_max_erases();
	friend class Block_manager;

	ulong get_erases_remaining(const Address &address) const;
	void get_least_worn(Address &address) const;
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	Block *get_block_pointer(const Address & address);

	Address resolve_logical_address(unsigned int logicalAddress);
protected:
	Controller &controller;
	Ssd &ssd;
};

struct logical_page
{
	Address physical_address;
	double write_time;
	logical_page():physical_address(), write_time(-1) {}
};

struct ssd_block
{
	Address physical_address;
//	unsigned int last_write_time;
	unsigned int valid_page_count;
	unsigned int lifetime_left;
	unsigned int *page_mapping;
	bool *reserved_page;
	unsigned int last_page_written;
	bool scheduled_for_erasing;
	ssd_block():physical_address(), page_mapping(NULL), reserved_page(NULL) {}
	ssd_block(const struct ssd_block &b):physical_address(b.physical_address)
	{
		valid_page_count = b.valid_page_count;
		lifetime_left = b.lifetime_left;
		last_page_written = b.last_page_written;
		scheduled_for_erasing = b.scheduled_for_erasing;
		page_mapping = (unsigned int *)malloc(BLOCK_SIZE*sizeof(unsigned int));
		reserved_page = (bool *)malloc(BLOCK_SIZE * sizeof(bool));
		for(unsigned int i=0;i<BLOCK_SIZE;i++)
		{
			page_mapping[i] = b.page_mapping[i];
			reserved_page[i] = b.reserved_page[i];
		}
	}
	struct ssd_block& operator=(const struct ssd_block &b)
	{
		physical_address = b.physical_address;
		valid_page_count = b.valid_page_count;
		lifetime_left = b.lifetime_left;
		last_page_written = b.last_page_written;
		scheduled_for_erasing = b.scheduled_for_erasing;
		page_mapping = (unsigned int *)malloc(BLOCK_SIZE*sizeof(unsigned int));
		reserved_page = (bool *)malloc(BLOCK_SIZE * sizeof(bool));
		for(unsigned int i=0;i<BLOCK_SIZE;i++)
		{
			page_mapping[i] = b.page_mapping[i];
			reserved_page[i] = b.reserved_page[i];
		}
		return *this;
	}
};

enum ftl_event_process{BACKGROUND, FOREGROUND};

struct ftl_event
{
	Address physical_address;
	enum event_type type;
	unsigned int logical_address;
	double start_time;
	double end_time;
	enum ftl_event_process process;
	bool *op_complete_pointer;
	double *end_time_pointer;
	ftl_event():physical_address(), op_complete_pointer(NULL), end_time_pointer(NULL) {}
	ftl_event(const struct ftl_event &e)//:physical_address(e.physical_address)
	{
		physical_address = e.physical_address;
		type = e.type;
		logical_address = e.logical_address;
		start_time = e.start_time;
		end_time = e.end_time;
		process = e.process;
		op_complete_pointer = e.op_complete_pointer;
		end_time_pointer = e.end_time_pointer;
	}
	struct ftl_event& operator=(const struct ftl_event &e)
	{
		physical_address = e.physical_address;
		type = e.type;
		logical_address = e.logical_address;
		start_time = e.start_time;
		end_time = e.end_time;
		process = e.process;
		op_complete_pointer = e.op_complete_pointer;
		end_time_pointer = e.end_time_pointer;
		return *this;
	}
};

struct queued_ftl_event
{
	Address write_from_address;
	struct ftl_event event;
	bool parent_completed;
	bool predecessor_completed;
	struct queued_ftl_event *child;
	queued_ftl_event():write_from_address() {}
	queued_ftl_event(const struct queued_ftl_event &e):write_from_address(e.write_from_address)
	{
		event = e.event;
		parent_completed = e.parent_completed;
		predecessor_completed = e.predecessor_completed;
		child = e.child;
	}
	struct queued_ftl_event& operator=(const struct queued_ftl_event &e)
	{
		write_from_address = e.write_from_address;
		event = e.event;
		parent_completed = e.parent_completed;
		predecessor_completed = e.predecessor_completed;
		child = e.child;
		return *this;
	}
};

struct background_cleaning_blocks
{
	struct ssd_block block_to_clean;
	struct ssd_block cleaning_block;
};

struct required_bg_events_pointer
{
	unsigned int rw_start_index;
	unsigned int rw_end_index;
	unsigned int erase_index;
};


struct cache_entry
{
	Address physical_address;
	unsigned int logical_address;
	double time;
	bool evict_priority;
	cache_entry() : physical_address() {}
};

class CompareCacheEntries
{
public:
	bool operator()(const std::pair<bool, double> &a, const std::pair<bool, double> &b);
};

class Cache
{
public:
	Cache();
	~Cache();
	Cache(const Cache &c);
	Cache & operator=(const Cache &c);
	bool present_in_cache(Event &event);
	void place_in_cache(Event &event);
	bool add_priority_plane(unsigned int plane_num);
	bool remove_priority_plane(unsigned int plane_num);
private:
	unsigned int size;
	std::unordered_map<unsigned int, unsigned int> logical_address_map; 
	std::multimap<std::pair<bool, double>, unsigned int, CompareCacheEntries> eviction_map;
	std::vector<unsigned int> priority_planes;
	struct cache_entry *cached_pages;
};


class FtlImpl_Page : public FtlParent
{
public:
	FtlImpl_Page(Controller &controller, Ssd &parent);
	~FtlImpl_Page();
	enum status read(Event &event, bool &op_complete, double &end_time);
	enum status write(Event &event, bool &op_complete, double &end_time);
	enum status noop(Event &event, bool &op_complete, double &end_time);
	enum status trim(Event &event);
	void get_min_max_erases();
private:
	unsigned int latest_write_time;
	bool gc_required;
	unsigned int RAW_SSD_BLOCKS, ADDRESSABLE_SSD_PAGES;
	unsigned int clean_threshold;
	bool READ_PREFERENCE;
	std::vector< std::vector<struct ftl_event> >background_events;
	std::vector< std::vector<struct queued_ftl_event *> >ftl_queues;
	std::vector< std::vector<struct ssd_block> >bg_cleaning_blocks;
	std::vector< std::vector<struct required_bg_events_pointer> >required_bg_events;

	double *plane_free_times;
	struct logical_page *logical_page_list;
	std::list<struct ssd_block> free_block_list;
	std::list<struct ssd_block> allocated_block_list;
	std::list<struct ssd_block> filled_block_list;
	Address log_write_address;
	unsigned int low_watermark;

	unsigned int *ftl_queue_last_bg_event_index;
	bool *ftl_queue_has_bg_event;

	double bg_events_time;
	double next_event_time;

	double get_average_age(struct ssd_block &block);
	Address translate_lba_pba(unsigned int lba);
	unsigned int translate_pba_lba(Address pba);
	unsigned int get_page_number_in_block(unsigned int lba);
	unsigned int get_block_starting_lba(unsigned int lba);
	unsigned int get_logical_block_num(unsigned int lba);
	Address find_write_location(Event &event, Address cur, bool *already_open);
	bool increment_log_write_address(Event &event, Address asked_for, bool already_allocated);
	bool allocate_new_block(Address requested_address);
	unsigned int get_next_block_lba(unsigned int lba);
	Address get_next_block_pba(Address pba);
	enum status garbage_collect(Event &event);
	enum status garbage_collect_default(Event &event);
	enum status garbage_collect_cached(Event &event);
	double process_background_tasks(Event &event);
	double read_(Event &event);
	double write_(Event &event);
	void queue_required_bg_events(Event &event);
	double process_ftl_queues(Event &event);
	void move_required_pointers(unsigned int plane_num, unsigned int start, unsigned int end);
	bool mark_reserved(Address address, bool is_reserved);
};

class FtlImpl_Fast : public FtlParent
{
public:
	FtlImpl_Fast(Controller &controller, Ssd &parent);
	~FtlImpl_Fast();
	enum status read(Event &event, bool &op_complete, double &end_time);
	enum status write(Event &event, bool &op_complete, double &end_time);
	enum status noop(Event &event, bool &op_complete, double &end_time);
	enum status trim(Event &event);
	void get_min_max_erases();
private:
	void initialize_log_pages();

	std::map<long, LogPageBlock*> log_map;

	long *data_list;
	bool *pin_list;

	enum status write_to_log_block(Event &event, long logicalBlockAddress, bool *issueEventRequired);

	void switch_sequential(Event &event);
	enum status merge_sequential(Event &event, bool issueWrite);
	bool random_merge(LogPageBlock *logBlock, Event &event);

	void update_map_block(Event &event);

	void print_ftl_statistics();

	long sequential_logicalblock_address;
	Address sequential_address;
	uint sequential_offset;

	uint log_page_next;
	LogPageBlock *log_pages;

	int addressShift;
	int addressSize;
	unsigned int FAST_LOG_PAGE_LIMIT;
};





/* This is a basic implementation that only provides delay updates to events
 * based on a delay value multiplied by the size (number of pages) needed to
 * be written. */
class Ram 
{
public:
	Ram(double read_delay = RAM_READ_DELAY, double write_delay = RAM_WRITE_DELAY);
	~Ram(void);
	enum status read(Event &event);
	enum status write(Event &event);
private:
	double read_delay;
	double write_delay;
};

/* The controller accepts read/write requests through its event_arrive method
 * and consults the FTL regarding what to do by calling the FTL's read/write
 * methods.  The FTL returns an event list for the controller through its issue
 * method that the controller buffers in RAM and sends across the bus.  The
 * controller's issue method passes the events from the FTL to the SSD.
 *
 * The controller also provides an interface for the FTL to collect wear
 * information to perform wear-leveling.  */
class Controller 
{
public:
	Controller(Ssd &parent);
	~Controller(void);
	enum status event_arrive(Event &event, bool &op_complete, double &end_time);
	friend class FtlParent;
	friend class FtlImpl_Page;
	friend class FtlImpl_Fast;
	friend class Block_manager;

	Stats stats;
	void print_ftl_statistics(FILE *fp);
	const FtlParent &get_ftl(void) const;
private:
	enum status issue(Event &event_list);
	void translate_address(Address &address);
	ssd::ulong get_erases_remaining(const Address &address) const;
	void get_least_worn(Address &address) const;
	double get_last_erase_time(const Address &address) const;
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	ssd::uint get_num_free(const Address &address) const;
	ssd::uint get_num_valid(const Address &address) const;
	ssd::uint get_num_invalid(const Address &address) const;
	Block *get_block_pointer(const Address & address);
	Ssd &ssd;
	FtlParent *ftl;
	//FtlImpl_Page *ftl;
};

/* The SSD is the single main object that will be created to simulate a real
 * SSD.  Creating a SSD causes all other objects in the SSD to be created.  The
 * event_arrive method is where events will arrive from DiskSim. */
class Ssd 
{
public:
	Ssd (uint ssd_size = SSD_SIZE);
	~Ssd(void);
	bool event_arrive(enum event_type type, ulong logical_address, uint size, double start_time, bool &op_complete, double &end_time);
	bool event_arrive(enum event_type type, ulong logical_address, uint size, double start_time, bool &op_complete, double &end_time, void *buffer);
	void *get_result_buffer();
	friend class Controller;
	void print_statistics();
	void reset_statistics();
	void write_statistics(FILE *stream);
	void write_header(FILE *stream);
	const Controller &get_controller(void) const;

	void print_ftl_statistics(FILE *fp);
	double ready_at(void);
	Cache cache;
private:
	enum status read(Event &event);
	enum status write(Event &event);
	enum status erase(Event &event);
	enum status merge(Event &event);
	enum status replace(Event &event);
	enum status merge_replacement_block(Event &event);
	ulong get_erases_remaining(const Address &address) const;
	void update_wear_stats(const Address &address);
	void get_least_worn(Address &address) const;
	double get_last_erase_time(const Address &address) const;	
	Package &get_data(void);
	enum page_state get_state(const Address &address) const;
	enum block_state get_block_state(const Address &address) const;
	void get_free_page(Address &address) const;
	ssd::uint get_num_free(const Address &address) const;
	ssd::uint get_num_valid(const Address &address) const;
	ssd::uint get_num_invalid(const Address &address) const;
	Block *get_block_pointer(const Address & address);

	uint size;
	Controller controller;
	Ram ram;
	Bus bus;
	Package * const data;
	ulong erases_remaining;
	ulong least_worn;
	double last_erase_time;
};

class RaidSsd
{
public:
	RaidSsd (uint ssd_size = SSD_SIZE);
	~RaidSsd(void);
	double event_arrive(enum event_type type, ulong logical_address, uint size, double start_time);
	double event_arrive(enum event_type type, ulong logical_address, uint size, double start_time, void *buffer);
	void *get_result_buffer();
	friend class Controller;
	void print_statistics();
	void reset_statistics();
	void write_statistics(FILE *stream);
	void write_header(FILE *stream);
	const Controller &get_controller(void) const;

	void print_ftl_statistics();
private:
	uint size;

	Ssd *Ssds;

};
} /* end namespace ssd */

#endif
