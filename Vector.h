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

int32_t getSimilarity ( class Vector *v0 , class Vector *v1 ) ;

class Vector {

 public:

	Vector();

	// serialize into "buf" and returns bytes written
	//int32_t store ( char *buf , int32_t bufMaxSize );

	// deserialize and return bytes read
	//int32_t set ( char *buf , int32_t bufMaxSize );

	//int32_t set2  ( char *buf , int32_t numPairHashes ) ;

	// how many bytes required to store currently held data
	//int32_t getStoredSize ( );
	int32_t getNumPairHashes() {return m_numPairHashes;};
	uint32_t getVectorHash();
	// . set ourselves from a a document (xml) and set of links
	//   and the URL of that document
	// . returns false and sets g_errno on error
	//bool set ( Xml *xml , Links *links , Url *url , int32_t linkNode ,
	//	   char *buf , int32_t bufSize );

	//bool setForDates ( class Words    *w1       , 
	//		   class Sections *sections ,
	//		   int32_t            niceness ) ;

	void reset();

	// is vector "v" a link-farm brother?
	int32_t getLinkBrotherProbability ( Vector *v , bool removeMatches ) ;

	// private:

	bool setPairHashes ( Xml *xml, int32_t linkNode, int32_t niceness );
	bool setLocalPairHashes ( Xml *xml , Links *links , Url *url ) ;
	bool setLinkHashes ( Links *links , Url *url ) ;

	// for comparing one url to another. how many path components do they
	// have in common? used in LinkInfo::merge() to see if similar.
	bool setPathComponentHashes ( Url *url ) ;

	bool setTagPairHashes ( Xml *xml, int32_t niceness );

	// total # of non-local outgoing links
	//int32_t          m_numRemoteLinks;

	int32_t getSize ( ) { 
		//int32_t size = ((char *)m_pairHashes - (char *)&m_init);
		int32_t size = 4;
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
	//uint32_t m_pairHashes [ MAX_PAIR_HASHES ];
	int32_t           m_numPairHashes ;
	uint32_t  m_pairHashes[ MAX_PAIR_HASHES ]  ;
};

#endif
