#include "gb-include.h"

#include "Msg40Cache.h"

// the global cache
Msg40Cache g_msg40Cache;

// returns true if we found in cache and set "msg40", false otherwise
bool Msg40Cache::setFromCache ( Msg40 *msg40 ) {

	// make the key based on the input parms like # of docs wanted,
	// start doc #, site clustering on, ...
	key_t k = msg40->makeKey ( );

	// look in cache, return false if not in there
	if ( ! m_cache.getList ( k , k   , 
				 &list   , 
				 false   ,  // do copy?
				 60*60*2 ,  // 2 hours max age
				 true    )) // keep stats
		return false;

	// set the msg40 from the list's data buf
	char *data     = list.getList();
	int32_t  dataSize = list.getListSize();

	setMsg40FromData ( msg40 , data , dataSize );

	return true;
}

