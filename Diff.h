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
	int32_t m_index1;
	int32_t m_index2;
	int32_t m_length;
};

// Diff 2 xml or html files on stdout
void diffXmlFiles(char *file1, char *file2, DiffOpt *opt=NULL);
void printXmlDiff(Xml *xml1, Xml *xml2, DiffOpt *opt=NULL);

// longest common subsequence of 2 xml objects
int32_t lcsXml(int32_t *lcsBuf1,        // xml1 indexes of nodes in lcs
	    int32_t *lcsBuf2,        // xml2 indexes of nodes in lcs
	    int32_t *lcsLenBuf,      // number of consecutive nodes in each lcsBuf
	    int32_t lcsBufLen,       // max number of sequence matches we can fit
	    Xml *xml1, Xml *xml2, // the xml structures to compare
	    DiffOpt *opt,   
	    const int32_t start1 = 0, 
	    const int32_t start2 = 0,
	    const int32_t n1 = -1,    // limit on number of nodes in xml1 and xml2 
	    const int32_t n2 = -1,
	    const int32_t rlevel = 0); // level of recursion


int64_testCommonSubseq(int32_t *outbuf1, // out1 indexes of nodes in lcs
			 int32_t *outbuf2,  // xml2 indexes of nodes in lcs
			 int32_t *outlen, // number of consecutive nodes lcsBuf
			 int32_t outbuflen, // Size of output bufs (elts not bytes)
			 char *seq1, //int32_t seq1Len, 
			 char *seq2, //int32_t seq2Len,
			 const DiffOpt *opt,
			 const int32_t start1, 
			 const int32_t start2,
			 const int32_t argN1, // elt count for seq and seq2
			 const int32_t argN2, 
			 const int32_t rlevel=0); 


int xmlNodeCompare(Xml *xml1, const int32_t index1, 
		   Xml *xml2, const int32_t index2, 
		   const DiffOpt *opt);

#endif
