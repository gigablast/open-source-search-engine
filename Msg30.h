// Copyright Matt Wells Jun 2002

// . call g_collectiondb.reloadList(), resets m_lastTimes in spiderLoop to 0's
// . this will cause it to reset m_startKeys to 0

#ifndef _MSG30_H_
#define _MSG30_H_

//#include "CollectionRec.h"

class Msg30 {

 public:

	bool registerHandler();

	// . send an updated collection rec to ALL hosts
	// . returns false if blocked, true otherwise
	// . sets errno on error
	bool update ( CollectionRec *rec      ,
		      bool           deleteIt ,
		      void          *state    , 
		      void         (* callback)(void *state ) );

	// leave public for wrappers to call
	void    *m_state ;
	void   (* m_callback)(void *state ) ;

	int32_t    m_requests;
	int32_t    m_replies;

	char    m_sendBuf [ sizeof(CollectionRec) ];
	int32_t    m_sendBufSize;
};

#endif
