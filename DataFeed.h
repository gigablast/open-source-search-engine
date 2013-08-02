// Gigablast, copyright Dec 2004
// Author: Javier Olivares
//
// . Contains Data Feed information for customers
//   Has passcode and pricing table for this feed

#ifndef _DATAFEED_H_
#define _DATAFEED_H_

#include "MetaContainer.h"
//#include "Customer.h"
#include "CollectionRec.h" // for MAX_PRICE

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
	long getCost ( unsigned long totalRequests,
		       unsigned long numResults,
		       bool hasGigabits ) {
		if (m_numTiers == 0 || m_numResultLevels == 0)
			return 0;
		// get the tier
		long tier;
		for (tier = 0; tier < m_numTiers-1; tier++)
			if (totalRequests <= m_tierMax[tier])
				break;
		// get the level
		long level;
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

	long getIndex ( unsigned long numResults,
			bool hasGigabits ) {
		// Check if we have result levels
		if (m_numResultLevels == 0) return 0;
		// Find the result level the query meets
		long i;
		for(i = 0; i < m_numResultLevels-1; i++)
			if(numResults <= m_resultLevels[i])
				break;
		// Calculate any other changes to index
		long opt = 0;
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
		memcpy(m_tierMax, pt->m_tierMax, sizeof(long)*m_numTiers);
		memcpy(m_resultLevels, pt->m_resultLevels, sizeof(long)*m_numResultLevels);
		long numCosts = m_numTiers*m_numResultLevels*2;
		memcpy(m_levelCosts, pt->m_levelCosts, sizeof(long)*numCosts);
	}
*/
	// locals
	long m_numTiers;
	unsigned long m_tierMax[MAX_PRICE_TIERS];
	long m_numResultLevels;
	unsigned long m_resultLevels[MAX_PRICE_RESULTLEVELS];
	unsigned long m_levelCosts[MAX_PRICE_LEVELCOSTS];
	long  m_monthlyFee;
};

class DataFeed : public MetaContainer {
public:

	DataFeed();
	~DataFeed();

	void setUrl ( char *name,
		      long  nameLen );

	void set ( long  creationTime,
		   char *dataFeedUrl,
		   long  dataFeedUrlLen,
		   char *passcode,
		   long  passcodeLen,
		   bool  isActive,
		   bool  isLocked = false );

	void parse ( char *dataFeedPage,
		     long  dataFeedPageLen );

	long buildPage ( char *page );
	void buildPage ( SafeBuf *sb );

	// locals
	char m_passcode[MAX_PASSCODELEN+1];
	long m_passcodeLen;
	bool m_isActive;
	bool m_isLocked;

	// ID
	long long m_customerId;

	// Price Table
	PriceTable m_priceTable;

	// creation time
	long m_creationTime;
};

#endif
