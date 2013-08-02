// Matt Wells, Copyright Oct 2002

// . used for detecting link spamming
// . 2 docs that link to the same doc that are similar should be considered
//   possible link spam
// . if the 3 linkers are exactly the same (even though from different ips)
//   then 2 should have link spam probability of 100% and the single remaining
//   one should be allowed 100%


#ifndef _VECTOR_H_
#define _VECTOR_H_

#include "Url.h"
#include "Xml.h"
//#include "Links.h"

#define MAX_PAIR_HASHES 100

long getSimilarity ( class Vector *v0 , class Vector *v1 ) ;

class Vector {

 public:

	Vector();

	// serialize into "buf" and returns bytes written
	//long store ( char *buf , long bufMaxSize );

	// deserialize and return bytes read
	//long set ( char *buf , long bufMaxSize );

	//long set2  ( char *buf , long numPairHashes ) ;

	// how many bytes required to store currently held data
	//long getStoredSize ( );
	long getNumPairHashes() {return m_numPairHashes;};
	uint32_t getVectorHash();
	// . set ourselves from a a document (xml) and set of links
	//   and the URL of that document
	// . returns false and sets g_errno on error
	//bool set ( Xml *xml , Links *links , Url *url , long linkNode ,
	//	   char *buf , long bufSize );

	//bool setForDates ( class Words    *w1       , 
	//		   class Sections *sections ,
	//		   long            niceness ) ;

	void reset();

	// is vector "v" a link-farm brother?
	long getLinkBrotherProbability ( Vector *v , bool removeMatches ) ;

	// private:

	bool setPairHashes ( Xml *xml, long linkNode, long niceness );
	bool setLocalPairHashes ( Xml *xml , Links *links , Url *url ) ;
	bool setLinkHashes ( Links *links , Url *url ) ;

	// for comparing one url to another. how many path components do they
	// have in common? used in LinkInfo::merge() to see if similar.
	bool setPathComponentHashes ( Url *url ) ;

	bool setTagPairHashes ( Xml *xml, long niceness );

	// total # of non-local outgoing links
	//long          m_numRemoteLinks;

	long getSize ( ) { 
		//long size = ((char *)m_pairHashes - (char *)&m_init);
		long size = 4;
		// add in pair hashes
		size += m_numPairHashes * 4;
		return size; 
	};

	// set to true after we hash our hashes into m_table
	//bool          m_init;

	// the table we hash into
	//TermTable     m_table;

	// . store top word pair hases in here
	// . these can also be link hashes now, too
	//unsigned long m_pairHashes [ MAX_PAIR_HASHES ];
	long           m_numPairHashes ;
	unsigned long  m_pairHashes[ MAX_PAIR_HASHES ]  ;
};

#endif
