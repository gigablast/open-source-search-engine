#ifndef DIFF_H__
#define DIFF_H__

#include "Xml.h"
class DiffOpt {
public:
	DiffOpt(){
		m_tagOnly = false;
		m_debug   = 0;
		m_context = 0;
		m_compare = NULL;
	};
	bool m_tagOnly;
	int  m_debug;
	int  m_context;

	int  m_eltSize;
	int (*m_compare)(void *arg1, void *arg2);
};

class Diff{
public:
	// m_op is negative, zero or positive
	// meaning deleted, same or added
	char m_op;
	long m_index1;
	long m_index2;
	long m_length;
};

// Diff 2 xml or html files on stdout
void diffXmlFiles(char *file1, char *file2, DiffOpt *opt=NULL);
void printXmlDiff(Xml *xml1, Xml *xml2, DiffOpt *opt=NULL);

// longest common subsequence of 2 xml objects
long lcsXml(long *lcsBuf1,        // xml1 indexes of nodes in lcs
	    long *lcsBuf2,        // xml2 indexes of nodes in lcs
	    long *lcsLenBuf,      // number of consecutive nodes in each lcsBuf
	    long lcsBufLen,       // max number of sequence matches we can fit
	    Xml *xml1, Xml *xml2, // the xml structures to compare
	    DiffOpt *opt,   
	    const long start1 = 0, 
	    const long start2 = 0,
	    const long n1 = -1,    // limit on number of nodes in xml1 and xml2 
	    const long n2 = -1,
	    const long rlevel = 0); // level of recursion


long longestCommonSubseq(long *outbuf1, // out1 indexes of nodes in lcs
			 long *outbuf2,  // xml2 indexes of nodes in lcs
			 long *outlen, // number of consecutive nodes lcsBuf
			 long outbuflen, // Size of output bufs (elts not bytes)
			 char *seq1, //long seq1Len, 
			 char *seq2, //long seq2Len,
			 const DiffOpt *opt,
			 const long start1, 
			 const long start2,
			 const long argN1, // elt count for seq and seq2
			 const long argN2, 
			 const long rlevel=0); 


int xmlNodeCompare(Xml *xml1, const long index1, 
		   Xml *xml2, const long index2, 
		   const DiffOpt *opt);

#endif
