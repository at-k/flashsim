#include "ssd.h"

using namespace ssd;

LogPageBlock::LogPageBlock()
{
	pages = new int[BLOCK_SIZE];
	aPages = new long[BLOCK_SIZE];

	for (uint i=0;i<BLOCK_SIZE;i++)
	{
		pages[i] = -1;
		aPages[i] = -1;
	}

	numPages = 0;

	next = NULL;
}


LogPageBlock::~LogPageBlock()
{
	delete [] pages;
	delete [] aPages;
}

/* Comparison class for use by FTL to sort the LogPageBlock compared to the number of pages written. */
bool LogPageBlock::operator() (const LogPageBlock& lhs, const LogPageBlock& rhs) const
{
	return lhs.numPages < rhs.numPages;
}
