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
	virtual void parse     ( char *page, long pageLen );

	// build the meta html page from this container
	virtual long buildPage ( char *page );
	virtual void buildPage ( SafeBuf *sb );

	// url
	char m_url[MAX_URLLEN+1];
	long m_urlLen;

	// base container name
	char m_baseName[MAX_URLLEN+1];
	long m_baseNameLen;

	// ID
	long long m_id;
};

#endif
