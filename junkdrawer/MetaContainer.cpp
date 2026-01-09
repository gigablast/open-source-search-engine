#include "gb-include.h"
#include "MetaContainer.h"

MetaContainer::MetaContainer() {
	m_id = -1;
	m_urlLen = 0;
	m_url[0] = '\0';
}

MetaContainer::~MetaContainer() {};
void MetaContainer::parse ( char *page, int32_t pageLen ) {
	log( "build: USING VIRTUAL PARSE" );
};
int32_t MetaContainer::buildPage ( char *page ) { 
	log( "build: USING VIRTUAL BUILDPAGE!" ); 
	return 0; 
};
void MetaContainer::buildPage ( SafeBuf *sb ) { 
	log( "build: USING VIRTUAL SB BUILDPAGE!" ); 
};
