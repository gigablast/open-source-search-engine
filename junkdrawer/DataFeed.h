// Gigablast, copyright Dec 2004
// Author: Javier Olivares
//
// . Contains Data Feed information for customers
//   Has passcode and pricing table for this feed

#ifndef _DATAFEED_H_
#define _DATAFEED_H_

#include "MetaContainer.h"
//#include "Customer.h"
//#include "CollectionRec.h" // for MAX_PRICE

#define DATAFEEDCOUNT_REQUESTS    1
#define DATAFEEDCOUNT_CURRENTBILL 2

#define MAX_USERNAMELEN     48 
#define MAX_DATAFEEDPAGELEN (32*1024)
#define MAX_PASSCODELEN      8

#define MAX_PRICE_TIERS         6
#define MAX_PRICE_RESULTLEVELS  16
#define MAX_PRICE_LEVELCOSTS   (MAX_PRICE_TIERS*MAX_PRICE_RESULTLEVELS*2)


class PriceTable {
public:
	PriceTable() {
		m_numTiers = 0;
		m_numResultLevels = 0;
		m_monthlyFee = 0;
	}

	~PriceTable() {
	}

	void reset ( ) {
		m_numTiers = 0;
		m_numResultLevels = 0;
		m_monthlyFee = 0;
	}

	// . Get the cost of this query (in micro cents)
	//   given the total number of requests made for
	//   this feed this month and the number of results
	//   requested.
	int32_t getCost ( uint32_t totalRequests,
		       uint32_t numResults,
		       bool hasGigabits ) {
		if (m_numTiers == 0 || m_numResultLevels == 0)
			return 0;
		// get the tier
		int32_t tier;
		for (tier = 0; tier < m_numTiers-1; tier++)
			if (totalRequests <= m_tierMax[tier])
				break;
		// get the level
		int32_t level;
		for (level = 0; level < m_numResultLevels-1; level++)
			if (numResults <= m_resultLevels[level])
				break;
		// return the cost
		if (hasGigabits)
			return m_levelCosts[ (tier * (m_numResultLevels*2)) 
					     + (level*2+1) ];
		else
			return m_levelCosts[ (tier * (m_numResultLevels*2)) 
					     + (level*2) ];
	}

	int32_t getIndex ( uint32_t numResults,
			bool hasGigabits ) {
		// Check if we have result levels
		if (m_numResultLevels == 0) return 0;
		// Find the result level the query meets
		int32_t i;
		for(i = 0; i < m_numResultLevels-1; i++)
			if(numResults <= m_resultLevels[i])
				break;
		// Calculate any other changes to index
		int32_t opt = 0;
		if(hasGigabits) opt |= 1;
		// Return the proper Countdb index
		return (m_resultLevels[i]/10 | opt);
	}
/*
	// clone the price table
	void clone ( PriceTable *pt ) {
		m_numTiers = pt->m_numTiers;
		m_numResultLevels = pt->m_numResultLevels;
		m_monthlyFee = pt->m_monthlyFee;
		gbmemcpy(m_tierMax, pt->m_tierMax, sizeof(int32_t)*m_numTiers);
		gbmemcpy(m_resultLevels, pt->m_resultLevels, sizeof(int32_t)*m_numResultLevels);
		int32_t numCosts = m_numTiers*m_numResultLevels*2;
		gbmemcpy(m_levelCosts, pt->m_levelCosts, sizeof(int32_t)*numCosts);
	}
*/
	// locals
	int32_t m_numTiers;
	uint32_t m_tierMax[MAX_PRICE_TIERS];
	int32_t m_numResultLevels;
	uint32_t m_resultLevels[MAX_PRICE_RESULTLEVELS];
	uint32_t m_levelCosts[MAX_PRICE_LEVELCOSTS];
	int32_t  m_monthlyFee;
};

class DataFeed : public MetaContainer {
public:

	DataFeed();
	~DataFeed();

	void setUrl ( char *name,
		      int32_t  nameLen );

	void set ( int32_t  creationTime,
		   char *dataFeedUrl,
		   int32_t  dataFeedUrlLen,
		   char *passcode,
		   int32_t  passcodeLen,
		   bool  isActive,
		   bool  isLocked = false );

	void parse ( char *dataFeedPage,
		     int32_t  dataFeedPageLen );

	int32_t buildPage ( char *page );
	void buildPage ( SafeBuf *sb );

	// locals
	char m_passcode[MAX_PASSCODELEN+1];
	int32_t m_passcodeLen;
	bool m_isActive;
	bool m_isLocked;

	// ID
	int64_t m_customerId;

	// Price Table
	PriceTable m_priceTable;

	// creation time
	int32_t m_creationTime;
};

#endif
