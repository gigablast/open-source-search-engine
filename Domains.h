// Matt Wells, copyright Nov 2001

#ifndef _DOMAINS_H_
#define _DOMAINS_H_

// . get the domain name (name + tld) from a hostname
// . returns NULL if not in the accepted list
// . "host" must be NULL terminated and in LOWER CASE
// . returns ptr into host that marks the domain name
char *getDomain ( char *host , int32_t hostLen , char *tld , int32_t *dlen );

// when host is like 1.2.3.4 use this one
char *getDomainOfIp ( char *host , int32_t hostLen , int32_t *dlen );

// used by getDomain() above
char *getTLD ( char *host , int32_t hostLen ) ;

// used by getTLD() above
bool isTLD ( char *tld , int32_t tldLen ) ;

#endif
