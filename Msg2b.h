//
// Copyright Gigablast, April 2005
// Author: Javier Olivares <jolivares@gigablast.com>
//
// Message to generate the directory listing of a category.
//

#ifndef _MSG2B_H_
#define _MSG2B_H_

#include "Categories.h"

class Msg2b {
public:
	Msg2b();
	~Msg2b();

	// main call to generate directory
	bool generateDirectory ( long   dirId,
				 void  *state,
				 void (*callback)(void *state) );

	// serialize/deserialize
	long getStoredSize ( );
	long serialize     ( char *buf, long bufLen );
	long deserialize   ( char *buf, long bufLen );

	// callback
	void  *m_st;
	void (*m_callback)(void *state);

	// dir ID to get
	long m_dirId;

	// buffers for directory
	SubCategory *m_subCats;
	long         m_subCatsSize;
	long         m_numSubCats;
	char        *m_catBuffer;
	long         m_catBufferSize;
	long         m_catBufferLen;
};

#endif
