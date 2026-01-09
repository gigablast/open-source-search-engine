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
	bool generateDirectory ( int32_t   dirId,
				 void  *state,
				 void (*callback)(void *state) );

	// serialize/deserialize
	int32_t getStoredSize ( );
	int32_t serialize     ( char *buf, int32_t bufLen );
	int32_t deserialize   ( char *buf, int32_t bufLen );

	// callback
	void  *m_st;
	void (*m_callback)(void *state);

	// dir ID to get
	int32_t m_dirId;

	// buffers for directory
	SubCategory *m_subCats;
	int32_t         m_subCatsSize;
	int32_t         m_numSubCats;
	char        *m_catBuffer;
	int32_t         m_catBufferSize;
	int32_t         m_catBufferLen;
};

#endif
