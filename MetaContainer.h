// Gigablast, copyright Dec 2004
// Author: Javier Olivares
//
// . Generic Container Interface

#ifndef _METACONTAINER_H_
#define _METACONTAINER_H_

#define MAX_URLLEN 128

#include "Xml.h"
#include "Log.h"
#include "Url.h"

class MetaContainer {
public:
	MetaContainer();
	virtual ~MetaContainer();

	// parse a page into this container
	virtual void parse     ( char *page, int32_t pageLen );

	// build the meta html page from this container
	virtual int32_t buildPage ( char *page );
	virtual void buildPage ( SafeBuf *sb );

	// url
	char m_url[MAX_URLLEN+1];
	int32_t m_urlLen;

	// base container name
	char m_baseName[MAX_URLLEN+1];
	int32_t m_baseNameLen;

	// ID
	int64_t m_id;
};

#endif
