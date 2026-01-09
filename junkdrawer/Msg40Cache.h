// Matt Wells, Copyright Nov 2002

// . a cache to store Msg40's (docIds/summaries)

#ifndef _MSG40CACHE_
#define _MSG40CACHE_

class Msg40Cache {

 public:

	// returns true if we found in cache and set "msg40", false otherwise
	bool setFromCache (Msg40 *msg40 );

};

extern class Msg40Cache g_msg40Cache;

#endif
