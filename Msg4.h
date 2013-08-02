// Gigablast, Inc., copyright Jul 2007

// like Msg1.h but buffers up the add requests to avoid packet storms

#ifndef _MSG4_H_
#define _MSG4_H_

bool registerHandler4   ( ) ;
bool saveAddsInProgress ( char *filenamePrefix );
bool loadAddsInProgress ( char *filenamePrefix );
// used by Repair.cpp to make sure we are not adding any more data ("writing")
bool hasAddsInQueue     ( ) ;

//#include "RdbList.h"

bool addMetaList ( char *p , class UdpSlot *slot = NULL ) ;

bool isInMsg4LinkedList ( class Msg4 *msg4 ) ;

class Msg4 {

 public:
	/*
	bool addList ( RdbList *list                   ,
		       char     rdbId                  ,
		       char    *coll                   ,
		       void    *state                  ,
		       void  (* callback)(void *state) ,
		       long     niceness               ,
		       bool     forceLocal = false     ,
		       bool     splitList  = true      );

	bool addList ( RdbList   *list                   ,
		       char       rdbId                  ,
		       collnum_t  collnum                ,
		       void      *state                  ,
		       void    (* callback)(void *state) ,
		       long       niceness               ,
		       bool       forceLocal = false     ,
		       bool       splitList  = true      );

	bool storeList ( RdbList *list , char rdbId , collnum_t collnum ) ;
	*/

	// meta list format =
	// (rdbId | 0x08) then rdb record [if nosplit]
	// (rdbId | 0x00) then rdb record [if split  ]
	bool addMetaList ( char *metaList                 ,
			   long  metaListSize             ,
			   char *coll                     ,
			   void *state                    ,
			   void (* callback)(void *state) ,
			   long  niceness                 ,
			   char  rdbId = -1               );

	// this one is faster...
	bool addMetaList ( char      *metaList                 ,
			   long       metaListSize             ,
			   collnum_t  collnum                  ,
			   void      *state                    ,
			   void      (* callback)(void *state) ,
			   long       niceness                 ,
			   char       rdbId = -1               );

	bool addMetaList2 ( );

	Msg4() { m_inUse = false; };
	~Msg4() { if ( m_inUse ) { char *xx=NULL;*xx=0; } };

	// injecting into the "test" collection likes to flush the buffers
	// after each injection to make sure the data is available for
	// following injections
	bool flush ( void *state                    ,
		     void (* callback)(void *state) ,
		     long  niceness                 );

	// private:

	void         (*m_callback ) ( void *state );
	void          *m_state;

	char      m_rdbId;
	char      m_inUse;
	collnum_t m_collnum;
	long      m_niceness;
	//bool      m_splitList;

	char *m_metaList     ;
	long  m_metaListSize ;
	char *m_currentPtr   ; // into m_metaList

	// the linked list for waiting in line
	class Msg4 *m_next;
};

// returns false if blocked and callback will be called when flush is done
bool flushMsg4Buffers ( void *state , void (* callback) (void *) ) ;

#endif
