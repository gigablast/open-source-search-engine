

#ifndef _SANITY_H_
#define _SANITY_H_

#define GBASSERT(c)            (gb_sanityCheck((c),__FILE__,__FUNCTION__,__LINE__))
#define GBASSERTMSG(c, msg)    (gb_sanityCheckMsg((c),(msg),__FILE__,__FUNCTION__,__LINE__))

inline void gb_sanityCheck ( bool cond, 
			     const char *file, const char *func, const int line ) {
	if ( ! cond ) {
		log( LOG_LOGIC, "SANITY CHECK FAILED /%s:%s:%d/", 
		     file, func, line );
		char *xx = NULL; *xx = 0;
	}
}

inline void gb_sanityCheckMsg ( bool cond, char *msg, 
				const char *file, const char *func, const int line ) {
	if ( ! cond ) {
		log( LOG_LOGIC, "SANITY CHECK FAILED: %s /%s:%s:%d/", 
		     msg, 
		     file, func, line );
		char *xx = NULL; *xx = 0;
	}
}


#endif // _SANITY_H_

