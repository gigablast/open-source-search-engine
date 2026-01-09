#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "gb-include.h"
#include "Titledb.h"
#include "Diff.h"

// local Debug stuff for diff
#define DPRINTF(format,args...)  if ( opt->m_debug > 0 )\
		printf("DEBUG: "/**/format, ## args)
#define DPRINTF2(format, args...) if ( opt->m_debug > 1 )\
		printf("DEBUG2: "/**/format, ## args)

// element compare functions
int xmlTagCompare(void *elt1, void *elt2);
int xmlTagCompare2(void *elt1, void *elt2);
bool xmlWhitespaceNode(XmlNode *node);

// 
int32_t editPath(char *seq1, char *seq2,
	      const DiffOpt *opt,
	      const int32_t start1, const int32_t start2,
	      const int32_t n1, const int32_t n2,
	      int32_t *midX, int32_t *midY, int32_t *midLen);

int32_t xmlEditPath(Xml *xml1, Xml* xml2, 
		 const DiffOpt *opt,
		 const int32_t start1, const int32_t start2,
		 const int32_t n1, const int32_t n2,
		 int32_t *midX, int32_t *midY, int32_t *midLen);


// Load 2 files into Xml classes and print the output from 
// printXmlDiff on them
void diffXmlFiles(char *file1, char *file2, DiffOpt *opt){

	int fd = open(file1, O_RDONLY);
	if (fd < 0 ) {
		fprintf(stderr, "error opening %s\n", file1);
		return;
	}
	int32_t contentLen1 =  lseek(fd, 0, SEEK_END);
	if (contentLen1 < 0){
		fprintf(stderr, "error seeking %s\n", file1);
		close(fd);
		return;
	}
	char *content1 = (char*)mmalloc(contentLen1+1, "xmldiff");
	if (!content1){
		fprintf(stderr, "can't alloc %"INT32" bytes for %s\n",
		       contentLen1+1, file1);
		close(fd);
		return;		
	}
	content1[contentLen1] = '\0';
	lseek(fd, 0, SEEK_SET);
	int32_t n = read(fd, content1, contentLen1);
	if (n && n != contentLen1){
		fprintf(stderr, "error reading %s: "
			"expected %"INT32" bytes got %"INT32".\n"
			"(errno=%s)\n",
			file1, contentLen1, n, strerror(errno));
		return;
	}
	close(fd);
	fd = open(file2, O_RDONLY);
	if (fd < 0 ) {
		fprintf(stderr, "error opening %s\n", file2);
		return;
	}
	int32_t contentLen2 =  lseek(fd, 0, SEEK_END);
	if (contentLen2 < 0){
		fprintf(stderr, "error seeking %s\n", file2);
		close(fd);
		return;
	}
	char *content2 = (char*)mmalloc(contentLen2+1, "xmldiff");
	if (!content2){
		fprintf(stderr, "can't alloc %"INT32" bytes for %s\n",
		       contentLen2+1, file2);
		close(fd);
		return;		
	}
	content2[contentLen2] = '\0';
	lseek(fd, 0, SEEK_SET);
	n = read(fd, content2, contentLen2);
	if (n && n != contentLen2){
		fprintf(stderr, "error reading %s: "
			"expected %"INT32" bytes got %"INT32".\n"
			"(errno=%s)\n",
			file2, contentLen2, n, strerror(errno));
		return;
	}
	close(fd);

	
	Xml xml1;
	xml1.set(csISOLatin1,
		 content1,
		 contentLen1,
		 false, 0, false,
		 TITLEREC_CURRENT_VERSION);
	Xml xml2;
	xml2.set(csISOLatin1,
		 content2,
		 contentLen2,
		 false, 0, false,
		 TITLEREC_CURRENT_VERSION);
	//printf("%s: len=%"INT32", nodes=%"INT32"\n", 
	//       file1, contentLen1, xml1.getNumNodes());
	//printf("%s: len=%"INT32", nodes=%"INT32"\n", 
	//       file2, contentLen2, xml2.getNumNodes());

	printXmlDiff(&xml1,&xml2, opt);
	
}


// Run our Xml Diff algorithm on 2 Xmls
void printXmlDiff(Xml *xml1, Xml *xml2, DiffOpt *argOpt){

	// Supply default args if necessary
	DiffOpt defaultOpt;
	DiffOpt *opt = &defaultOpt;
	if (argOpt) opt = argOpt;
	
	// set default compare function if necessary
	if (!opt->m_compare)
		opt->m_compare = xmlTagCompare2;
	
	opt->m_eltSize = sizeof(XmlNode);

	int32_t seq1  [4096];
	int32_t seq2  [4096];
	int32_t seqLen[4096];
	SafeBuf buf;
	//int32_t numSeq = longestCommonSubsequence(seq1, seq2, seqLen,4096, 
	//				       &xml1-> &xml2->;
	//printf("lcs length: %"INT32"\n", numSeq);
	
	int32_t numSeq = lcsXml(seq1, seq2, seqLen,4096, xml1, xml2, opt);
 	//buf.safePrintf("LCS:\nlength: %"INT32"\n", numSeq);
	
//  	int32_t start = -1;
//  	for (int32_t i=0;i<numSeq;i++){
//  		int32_t i1 = seq1[i];
//  		int32_t i2 = seq2[i];
//  		int32_t iLen = seqLen[i];
 		//buf.safePrintf("node=(%"INT32", %"INT32") len=%"INT32"\n", i1, i2, iLen);
// 		if (xml1->isUnicode()) 
// 			buf.utf16Encode(xml1->getNode(i1),
// 					xml1->getNodeLen(i1));
// 		else
// 			buf.latin1Encode(xml1->getNode(i1),
// 					 xml1->getNodeLen(i1));
// 		buf.safePrintf("-");
// 		if (xml1->isUnicode()) 
// 			buf.utf16Encode(xml1->getNode(i1+iLen-1),
// 					xml1->getNodeLen(i1+iLen-1));
// 		else
// 			buf.latin1Encode(xml1->getNode(i1+iLen-1),
// 					 xml1->getNodeLen(i1+iLen-1));
// 		buf.safePrintf("\n");
 		//if (i < numSeq-1) buf.safePrintf("\n");
//  		continue;
//  	}
	//buf.safePrintf("\n");
	//buf.safePrintf("DIFF:\n");
	// Print Diff
	int32_t n1 = xml1->getNumNodes();
	int32_t n2 = xml2->getNumNodes();
	int32_t i = 0;
	int32_t j = 0;
	int32_t lastC = 0;
	for (int32_t seqi=0; i<n1 || j<n2 || seqi<numSeq ; seqi++){
		// safe values
		int32_t a = n1;
		int32_t b = n2;
		int32_t len = 0;
		int32_t d = 0;
		int32_t numEdits = 0;
		// node before this common sequence
		if (seqi<numSeq){
			// stop at next common sequence
			a =seq1[seqi];
			b = seq2[seqi];
			len = seqLen[seqi];
			d = b-a;
		}
		//buf.safePrintf("***** COMMON SEQ %"INT32": (%"INT32"-%"INT32"), (%"INT32"-%"INT32") "
		//	       "a=%"INT32" b=%"INT32" i=%"INT32" j=%"INT32" "
		//	       "len=%"INT32"\n",
		//	       seqi, 
		//	       a, a+len-1, b, b+len-1,
		//	       a, b, i, j, len);

		//buf.safePrintf("***** COMMON SEQ %"INT32": "
		//	       "(%"INT32"-%"INT32"), (%"INT32"-%"INT32") *****\n",
		//	       seqi, 
		//	       a, a+len-1, b, b+len-1);

		if (i<a || j<b){
			int32_t editLoc = i;
			// print location, in first file, 
			// of the following edits
			// but only if context tags have not caught up
			if (editLoc > lastC){ 
				char name[1024];
				name[0] = '\0';
				int32_t namelen = 0;
				//namelen = xml1->getCompoundName
				//	(editLoc,name,1024);
				buf.safePrintf("@@@ node %"INT32": ",
					       editLoc);
				if(xml1->isUnicode()) 
					buf.utf16Encode(name,namelen);
				else
					buf.latin1Encode(name,namelen);
				buf.safePrintf("\n");
			}


			// leading context
			int32_t c = i;

			// go back opt->m_context nodes, skipping empty tags, 
			// go back as far as the first node, or
			// the last node that was output as trailing context
			for (int32_t x=i-1, numTags = 0; 
			     x >= 0 && numTags < opt->m_context && x > lastC; 
			     x--){
				if (xmlWhitespaceNode(xml1->getNodePtr(x)))
					continue;
				numTags++;
				c = x;
			}
			
			
			for ( ; c < i ; c++) {
//  				char name[1024];
//  				int32_t namelen = xml1->getCompoundName
//  					(c,name,1024);
// 				if (namelen)
// 					buf.safePrintf("- %"INT32": %s\n",c,name);
				//buf.safePrintf("  (node %6li) \t| ", c);
				if (xmlWhitespaceNode(xml1->getNodePtr(c))) 
					continue;
				char *s = xml1->getNode(c);
				int32_t slen = xml1->getNodeLen(c);
				if (xml1->isUnicode()) 
					buf.utf16Encode(s,slen);
				else	buf.latin1Encode(s,slen);
				//lastC = c;
			}
			if (c>lastC) buf.safePrintf("\n");
			//buf.safePrintf("-------------------------\n");
		}
		
      		// Deletes
		if (i<a){
			buf.safePrintf("-------------------------\n");
			//buf.safePrintf("<<<<<<<<<<<<<<<<<<<<<<<<<\n");
			//buf.safePrintf("- (node %6li-%"INT32") \t| ", i,a-1);
			buf.safePrintf("--- (node %"INT32"-%"INT32")\n", i,a-1);
			while (i < a){
				char *s = xml1->getNode(i);
				int32_t slen = xml1->getNodeLen(i);
				if (xml1->isUnicode()) 
					buf.utf16Encode(s,slen);
				else	buf.latin1Encode(s,slen);
				i++;
				numEdits++;
			}
			buf.safePrintf("\n");
			//buf.safePrintf("\n");
			buf.safePrintf("-------------------------\n");
		}
		// Adds
		if (j<b){
			buf.safePrintf("+++++++++++++++++++++++++\n");
			//buf.safePrintf("-------------------------\n");
			//buf.safePrintf(">>>>>>>>>>>>>>>>>>>>>>>>>\n");
			//buf.safePrintf("+ (node %6li-%"INT32") \t| ", j,b-1);
			buf.safePrintf("+++ (node %"INT32"-%"INT32")\n", j,b-1);
			while (j < b){
					char *s = xml2->getNode(j);
				int32_t slen = xml2->getNodeLen(j);
				if (xml2->isUnicode()) 
					buf.utf16Encode(s,slen);
				else	buf.latin1Encode(s,slen);

				j++;
				numEdits++;
			}
			buf.safePrintf("\n");
			buf.safePrintf("+++++++++++++++++++++++++\n");
		}
		
		int32_t c = a;
		//  skip if there were no diffs...sometimes we get
		// 2 consecutive sequences of common nodes, dunno why 
		if (numEdits == 0) goto skipTrail;
		//buf.safePrintf("-------------------------\n");
		if (lastC < a) lastC = a;
		// trailing context
		for ( int32_t numTags = 0; 
		      numTags < opt->m_context && c < a+len ; c++) {
			lastC = c;
			if (xmlWhitespaceNode(xml1->getNodePtr(c))) 
				continue;
			numTags++;
			char *s = xml1->getNode(c);
			int32_t slen = xml1->getNodeLen(c);
			if (xml1->isUnicode()) 
				buf.utf16Encode(s,slen);
			else	buf.latin1Encode(s,slen);
		}
		buf.safePrintf("\n");
	skipTrail:
		if (opt->m_context <= 0)
			buf.safePrintf("  (node %"INT32"-%"INT32", %"INT32"-%"INT32")\n", 
				       a, a+len-1, b, b+len-1);
		// skip past common nodes
		if (i == a) i+=len;
		if (j == b) j+=len;
	}
	//buf.safePrintf("\n");
	buf.print();
}


// a linear space version of LCS/diff algorithm described at
// http://citeseerx.ist.psu.edu/viewdoc/summary?doi=10.1.1.4.6927

// The algorithm works by imagining the elements of the 2 sequences 
// as X and Y nodes on a graph.  A vertical or horizontal edge between nodes
// corresponds to adding or deleting an element, while a diagonal edge 
// is a common element between the two. 
// The int16_test path from (0,0) to (n1, n2) corresponds to the (an)
// optimal diff (int16_test edit script) between the sequences, and the 
// diagonal edges on that path represent a Longest Common Subsequence.

// This recursive version finds a path forward from the start and backward 
// from the end. When they overlap in the middle, we've found the middlemost 
// match (or edit)

// Max number of edits/diffs
#define MAXD 10000


#if 0
int32_t lcsXml(int32_t *lcsBuf1, // xml1 indexes of nodes in lcs
	    int32_t *lcsBuf2,  // xml2 indexes of nodes in lcs
	    int32_t *lcsLenBuf, // number of consecutive nodes in each lcsBuf
	    int32_t lcsBufLen, // max number of sequence matches we can fit
	    Xml *xml1, Xml *xml2, 
	    const DiffOpt *opt,
	    const int32_t argStart1, const int32_t argStart2, // index of start nodes
	    const int32_t argN1, const int32_t argN2, // limit on number of nodes 
	    const int32_t rlevel){ 
	
	// return value...length of overall lcs
	int32_t len = 0;
	
	int32_t start1 = argStart1;
	int32_t start2 = argStart2;
	int32_t n1 = argN1;
	int32_t n2 = argN2;
	
	if (n1 < 0) n1 = xml1->getNumNodes() - start1;
	if (n2 < 0) n2 = xml2->getNumNodes() - start2;

	XmlNode *nodes1 = xml1->getNodes();
	XmlNode *nodes2 = xml2->getNodes();
	
	if (opt->m_debug){
		for (int32_t i=0;i<rlevel;i++) printf("\t");
		printf("lcsXml(%"INT32"): [%"INT32" - %"INT32"] (%"INT32"), [%"INT32" - %"INT32"] (%"INT32") \n",
		       rlevel,
		       start1, start1+n1-1, n1, 
		       start2, start2+n2-1, n2);
	}
	if (lcsBufLen <= 0) return 0;
	if (n1 <= 0 || n2 <= 0) return 0;

	int32_t *p1   = lcsBuf1;
	int32_t *p2   = lcsBuf2;
	int32_t *pLen = lcsLenBuf;
	
	// First, find any common prefix
	int32_t prefixLen = 0;
	for ( int32_t i=0 ; i < n1 && i < n2 ; i++){
		//if (xmlNodeCompare(xml1, start1 + i, 
		//		   xml2, start2 + i, opt) != 0) 
		if (xmlTagCompare(&nodes1[start1 + i], 
				  &nodes2[start2 + i]) != 0) 
			break;
		prefixLen++;
	}
	if (prefixLen > 0){
		// store it in result
		*p1++   = start1;
		*p2++   = start2;
		*pLen++ = prefixLen;
		len++;

		// remove from input
		start1 += prefixLen;
		start2 += prefixLen;
		n1     -= prefixLen;		
		n2     -= prefixLen;
				
	}
	// optimization no further match possible
	if (n1 <= 0 || n2 <= 0) {
		if (opt->m_debug){
			for (int32_t i=0;i<rlevel;i++) printf("\t");
			printf("len=%"INT32"\n",len);
		}
		return len;
	}

	// find any common suffix too
	int32_t suffixLen = 0;
	for ( int32_t i=0 ; i < n1 && i < n2 ; i++){
		//if (xmlNodeCompare(xml1, start1+n1-1 - i, 
		//		   xml2, start2+n2-1 - i, opt) != 0) 
		if (xmlTagCompare(&nodes1[start1+n1-1 - i], 
				  &nodes2[start2+n2-1 - i]) != 0) 
			break;
		suffixLen++;
	}
	if (suffixLen > 0){
		// remove from input
		n1 -= suffixLen;		
		n2 -= suffixLen;
	}
	
	int32_t mid1=0, mid2=0, midLen=0;
	int32_t pathLen = 0;
	// optimization: no possible matches
	if (n1+n2 <= 3) 
		pathLen = n1+n2;
	else {
		if (opt->m_debug){
			for (int32_t i=0;i<rlevel;i++) printf("\t");
			DPRINTF("editPath: [%"INT32" - %"INT32"] (%"INT32"),  "
				"[%"INT32" - %"INT32"] (%"INT32")\n", 
				start1, start1+n1, n1, 
				start2, start2+n2, n2);
		}
		pathLen = xmlEditPath(xml1, xml2, opt,
				      start1, start2, n1, n2, 
				      &mid1, &mid2, &midLen);
		if (opt->m_debug){
			for (int32_t i=0;i<rlevel;i++) printf("\t");
			DPRINTF("editPath -> [%"INT32" - %"INT32"] (%"INT32"),  "
				"[%"INT32" - %"INT32"] (%"INT32")\n", 
				mid1, mid1+midLen-1, midLen, 
				mid2, mid2+midLen-1, midLen);
		}
	}
	
	If (pathLen < 0) {
		log(LOG_WARN, "Diff: Error finding int16_test diff");
		return -1;
	}

	// All nodes different
	if ( pathLen >= n1 + n2 ){
		// Add the suffix
		if (suffixLen > 0 && len < lcsBufLen){
			*p1++   = start1+n1;
			*p2++   = start2+n2;
			*pLen++ = suffixLen;
			if (opt->m_debug){
				for (int32_t i=0;i<rlevel;i++) printf("\t");
				printf("suffix lcs += (%"INT32"-%"INT32"),(%"INT32"-%"INT32")\n",
				       start1, start1+n1-1, 
				       start2, start2+n2-1);
			}
			len++;
		} 
		if (opt->m_debug){
			for (int32_t i=0;i<rlevel;i++) printf("\t");
			printf("len=%"INT32"\n",len);
		}
		return len;
	}
	
	
	if (pathLen > 1){
		// Recurse on the first half
		if (opt->m_debug){
			for (int32_t i=0;i<rlevel;i++) printf("\t");
			printf("Recurse left:\n");
		}
		int32_t n = lcsXml(p1, p2,pLen,lcsBufLen-len, 
				xml1, xml2, opt,
				start1, start2, 
				mid1-start1, mid2-start2,
				rlevel+1);
		if (n<0) return -1;

		p1   += n;
		p2   += n;
		pLen += n;
		len += n;
		if (opt->m_debug){
			for (int32_t i=0;i<rlevel;i++) printf("\t");
			printf("left len=%"INT32"\n", n);
		}
		// add middle common sequence
		if ( len < lcsBufLen && midLen > 0){
			*p1++   = mid1;
			*p2++   = mid2;
			*pLen++ = midLen;
			if (opt->m_debug){
				for (int32_t i=0;i<rlevel;i++) printf("\t");
				printf("middle lcs += (%"INT32"-%"INT32"),(%"INT32"-%"INT32")\n",
				       mid1, mid1+midLen-1, 
				       mid2, mid2+midLen-1);
			}
			len++;
		}
		
		// Recurse on the second half
		
		if (opt->m_debug){
			for (int32_t i=0;i<rlevel;i++) printf("\t");
			printf("Recurse right:\n");
		}
		n = lcsXml(p1, p2,pLen,lcsBufLen-len, 
			   xml1, xml2, opt,
			   mid1+midLen, mid2+midLen, 
			   start1+n1 - (mid1+midLen), 
			   start2+n2 - (mid2+midLen), 
			   rlevel+1);
		if (n<0) return -1;

		p1   += n;
		p2   += n;
		pLen += n;
			   
		len += n;
		if (opt->m_debug){
			for (int32_t i=0;i<rlevel;i++) printf("\t");
			printf("right len=%"INT32"\n", n);
		}
	}
	// edit path length of 1 or less means at most 1 node was 
	// added or deleted...since we've already removed the common prefix,
	// the extra node should be at the beginning of one of the sequences
	
	else {
		if (n1 > n2){
			// remove extra node from xml1
			//if (xmlNodeCompare(xml1, start1 , xml2, start2, 
			//		   opt ))
			start1++;
			n1--;
			
		}
		else if (n2 > n1){
			// remove extra node from xml2
			//if (xmlNodeCompare(xml1, start1 , xml2, start2, 
			//opt ))
			start2++;
			n2--;
		}
		// sanity check
		if ( n1 != n2) {
			log(LOG_WARN, "Diff: PANIC! Sequences do not match! "
			    "n1=%"INT32" n2=%"INT32"", n1, n2);
			char *xx = NULL; *xx=0;
		}
		if ( len < lcsBufLen && n1 > 0 && n2 > 0) {
			*p1++   = start1;
			*p2++   = start2;
			*pLen++ = n1;
			if (opt->m_debug){
				for (int32_t i=0;i<rlevel;i++) printf("\t");
				printf("end lcs += (%"INT32"-%"INT32"),(%"INT32"-%"INT32")\n",
				       start1, start1+n1-1, 
				       start2, start2+n2-1);
			}
			len++;
		}
	}
	// Add the suffix
	if (suffixLen > 0 && len < lcsBufLen){
		*p1++   = start1+n1;
		*p2++   = start2+n2;
		*pLen++ = suffixLen;
			if (opt->m_debug){
				for (int32_t i=0;i<rlevel;i++) printf("\t");
				printf("suffix lcs += (%"INT32"-%"INT32"),(%"INT32"-%"INT32")\n",
				       start1, start1+n1-1, 
				       start2, start2+n2-1);
			}
		len++;
	} 
	if (opt->m_debug){
		for (int32_t i=0;i<rlevel;i++) printf("\t");
		printf("len=%"INT32"\n",len);
	}
	return len;

	// compact the sequences
	int32_t newLen = len;
	for (int32_t i=1, j=0;i<len;i++){
		if ( i <= j ) continue;
		if ( lcsBuf1[i] != lcsBuf1[j] + lcsLenBuf[j] ||
		     lcsBuf2[i] != lcsBuf2[j] + lcsLenBuf[j] ) {
			j++;
			if ( i <= j ) continue;
			//fill hole
			lcsBuf1[j]=lcsBuf1[i];
			lcsBuf2[j]=lcsBuf2[i];
			lcsLenBuf[j]=lcsLenBuf[i];
		}
		// merge consecutive sequences
		lcsLenBuf[j] += lcsLenBuf[i];
		newLen--;
	}
	return newLen;
}
#else
int32_t lcsXml(int32_t *lcsBuf1, // xml1 indexes of nodes in lcs
	    int32_t *lcsBuf2,  // xml2 indexes of nodes in lcs
	    int32_t *lcsLenBuf, // number of consecutive nodes in each lcsBuf
	    int32_t lcsBufLen, // max number of sequence matches we can fit
	    Xml *xml1, Xml *xml2, 
	    DiffOpt *opt,
	    const int32_t argStart1, 
	    const int32_t argStart2, // index of start nodes
	    const int32_t argN1, 
	    const int32_t argN2, // limit on number of nodes 
	    const int32_t rlevel){ 
	
	XmlNode *nodes1 = xml1->getNodes();
	XmlNode *nodes2 = xml2->getNodes();
	int32_t start1 = argStart1;
	int32_t start2 = argStart2;
	int32_t n1 = argN1;
	int32_t n2 = argN2;
	if (n1<0) n1 = xml1->getNumNodes();
	if (n2<0) n2 = xml2->getNumNodes();
	
	
	return longestCommonSubseq(lcsBuf1, lcsBuf2, lcsLenBuf,lcsBufLen, 
				   (char*)nodes1, (char*)nodes2, opt,
				   start1, start2, n1, n2,rlevel);

}
#endif

int64_testCommonSubseq(int32_t *outbuf1, // out1 indexes of nodes in lcs
			 int32_t *outbuf2,  // xml2 indexes of nodes in lcs
			 int32_t *outlens, // number of consecutive nodes lcsBuf
			 int32_t outbuflen, // Size of output bufs (elts not bytes)
			 char *seq1, 
			 char *seq2, 
			 const DiffOpt *opt,
			 const int32_t argStart1,	 // index of first elements
			 const int32_t argStart2,
			 const int32_t argN1, // element count in seq 1 and seq2
			 const int32_t argN2, 
			 const int32_t rlevel){ 
	
	// return value...length of overall lcs
	int32_t len = 0;
	
	int32_t start1 = argStart1;
	int32_t start2 = argStart2;
	int32_t n1 = argN1;
	int32_t n2 = argN2;
	
	if (opt->m_debug){
		for (int32_t i=0;i<rlevel;i++) printf("\t");
		printf("longestCommonSubseq(%"INT32"): [%"INT32" , %"INT32"] [%"INT32" , %"INT32"] \n",
		       rlevel,start1, start1+n1-1, start2, start2+n2-1);
	}
	// no space
	if (outbuflen <= 0) return -1;
	// no input
	if (n1 <= 0 || n2 <= 0) return 0;

	int32_t *p1   = outbuf1;
	int32_t *p2   = outbuf2;
	int32_t *pLen = outlens;
	
	// First, find any common prefix
	int32_t prefixLen = 0;
	for ( int32_t i=0 ; i < n1 && i < n2 ; i++){
		char *e1 = seq1+(start1+i)*opt->m_eltSize;
		char *e2 = seq2+(start2+i)*opt->m_eltSize;
		if (opt->m_compare(e1, e2) != 0) 
			break;
		prefixLen++;
	}
	if (prefixLen > 0){
		// store it in result
		*p1++   = start1;
		*p2++   = start2;
		*pLen++ = prefixLen;
		if (opt->m_debug){
			for (int32_t i=0;i<rlevel;i++) printf("\t");
			printf("prefix lcs += (%"INT32"-%"INT32"),(%"INT32"-%"INT32")\n",
			       start1, start1+n1-1, 
			       start2, start2+n2-1);
		}
		len++;

		// remove from input
		start1 += prefixLen;
		start2 += prefixLen;
		n1     -= prefixLen;		
		n2     -= prefixLen;
				
	}
	// no further match possible
	if (n1 <= 0 || n2 <= 0) return len;

	// find any common suffix too
	int32_t suffixLen = 0;
	for ( int32_t i=0 ; i < n1 && i < n2 ; i++){
		char *e1 = seq1+(start1+n1-1-i)*opt->m_eltSize;
		char *e2 = seq2+(start2+n2-1-i)*opt->m_eltSize;
		if (opt->m_compare(e1, e2) != 0) 
			break;
		suffixLen++;
	}
	if (suffixLen > 0){
		// remove from input
		n1 -= suffixLen;		
		n2 -= suffixLen;
	}
	
	int32_t mid1=0, mid2=0, midLen=0;
	int32_t pathLen = 0;
	if (n1+n2 <= 3) 
		// No matches possible
		pathLen = n1+n2;
	else 
		pathLen = editPath(seq1, seq2, opt,
				   start1, start2, n1, n2, 
				   &mid1, &mid2, &midLen);
	if (pathLen < 0) {
		log(LOG_WARN, "Diff: Error finding int16_test diff");
		return -1;
	}

	// All nodes different
	if ( pathLen >= n1 + n2 ){
		// Add the suffix
		if (suffixLen > 0 && len < outbuflen){
			*p1++   = start1+n1;
			*p2++   = start2+n2;
			*pLen++ = suffixLen;
			if (opt->m_debug){
				for (int32_t i=0;i<rlevel;i++) printf("\t");
				printf("suffix lcs += (%"INT32"-%"INT32"),(%"INT32"-%"INT32")\n",
				       start1, start1+n1-1, 
				       start2, start2+n2-1);
			}
			len++;
		} 
		return len;
	}
	
	
	if (pathLen > 1){
		// Recurse on the first half
		if (opt->m_debug){
			for (int32_t i=0;i<rlevel;i++) printf("\t");
			printf("Recurse left:\n");
		}
		int32_t n = longestCommonSubseq(p1, p2,pLen,outbuflen-len, 
					     seq1, seq2, opt,
					     start1, start2, 
					     mid1-start1, mid2-start2,
					     rlevel+1);
		if (n<0) return -1;

		p1   += n;
		p2   += n;
		pLen += n;
		len += n;
		// add middle common sequence
		if ( len < outbuflen && midLen > 0){
			*p1++   = mid1;
			*p2++   = mid2;
			*pLen++ = midLen;
			if (opt->m_debug){
				for (int32_t i=0;i<rlevel;i++) printf("\t");
				printf("middle lcs += (%"INT32"-%"INT32"),(%"INT32"-%"INT32")\n",
				       start1, start1+n1-1, 
				       start2, start2+n2-1);
			}
			len++;
		}
		
		// Recurse on the second half
		
		if (opt->m_debug){
			for (int32_t i=0;i<rlevel;i++) printf("\t");
			printf("Recurse right:\n");
		}
		n = longestCommonSubseq(p1, p2,pLen,outbuflen-len, 
					seq1, seq2, opt,
					mid1+midLen, mid2+midLen, 
					start1+n1 - (mid1+midLen), 
					start2+n2 - (mid2+midLen), 
					rlevel+1);
		if (n<0) return -1;

		p1   += n;
		p2   += n;
		pLen += n;
			   
		len += n;
	}
	// edit path length of 1 or less means at most 1 node was 
	// added or deleted...since we've already removed the common prefix,
	// the extra node should be at the beginning of one of the sequences
	
	else {
		if (n1 > n2){
			start1++;
			n1--;
		}
		if (n2 > n1){
			start2++;
			n2--;
		}
		// sanity check
		if ( n1 != n2) {
			log(LOG_WARN, "Diff: PANIC! Sequences do not match! "
			    "n1=%"INT32" n2=%"INT32"", n1, n2);
			char *xx = NULL; *xx=0;
		}
		if ( len < outbuflen && n1 > 0 && n2 > 0) {
			*p1++   = start1;
			*p2++   = start2;
			*pLen++ = n1;
			if (opt->m_debug){
				for (int32_t i=0;i<rlevel;i++) printf("\t");
				printf("end lcs += (%"INT32"-%"INT32"),(%"INT32"-%"INT32")\n",
				       start1, start1+n1-1, 
				       start2, start2+n2-1);
			}
			len++;
		}
	}
	// Add the suffix
	if (suffixLen > 0 && len < outbuflen){
		*p1++   = start1+n1;
		*p2++   = start2+n2;
		*pLen++ = suffixLen;
		if (opt->m_debug){
			for (int32_t i=0;i<rlevel;i++) printf("\t");
			printf("suffix lcs += (%"INT32"-%"INT32"),(%"INT32"-%"INT32")\n",
			       start1, start1+n1-1, 
			       start2, start2+n2-1);
		}
		len++;
	} 
	return len;
	// compact the sequences
	//int32_t newLen = len;
	//for (int32_t i=1, j=0;i<len;i++){
	//	if ( i <= j ) continue;
	//	if ( lcsBuf1[i] != lcsBuf1[j] + lcsLenBuf[j] ||
	//	     lcsBuf2[i] != lcsBuf2[j] + lcsLenBuf[j] ) {
	//		j++;
	//		if ( i <= j ) continue;
			//fill hole
	//		lcsBuf1[j]=lcsBuf1[i];
	//		lcsBuf2[j]=lcsBuf2[i];
	//		lcsLenBuf[j]=lcsLenBuf[i];
	//	}
	//	// merge consecutive sequences
	//	lcsLenBuf[j] += lcsLenBuf[i];
	//	newLen--;
	//}
	//return newLen;
}


// access negative array index
#define IDX(asize, idx) ( ((idx)<0) ? ((asize)+(idx)) : (idx) )
#define VSIZE (2*MAXD+1)


// This will return the length of the int16_test edit path between
// xml1(start1, n1 nodes) and xml2(start2, n2 nodes), 
// return the middle "snake" (matching nodes) in mid[XYUV] if one exists
int32_t xmlEditPath(Xml *xml1, Xml* xml2, 
		 const DiffOpt *opt,
		 const int32_t start1, const int32_t start2,
		 const int32_t n1, const int32_t n2,
		 int32_t *midX, int32_t *midY, int32_t *midLen){
	
	XmlNode *nodes1 = xml1->getNodes();
	XmlNode *nodes2 = xml2->getNodes();

	int32_t v1[VSIZE];
	int32_t v2[VSIZE];
	
	int32_t delta = n1-n2;
	// the algorithm terminates on a reverse iteration if the difference 
	// in length is even
	bool reverseFinish =  (delta % 2 == 0);

	int32_t maxD = n1+n2;
	// round up
	if (maxD % 2) maxD++;
	maxD /= 2;
	if (maxD>MAXD) maxD = MAXD;
	
	v1[1] = 0;
	v2[1] = n1;
	*midLen = 0;
	//DPRINTF("xmlEditPath: maxD:%"INT32" delta:%"INT32" "
	//"s1:%"INT32" s2:%"INT32" n1:%"INT32" n2:%"INT32"\n", 
 	DPRINTF2("xmlEditPath: maxD:%"INT32" delta:%"INT32" "
 		"[%"INT32" - %"INT32"] (%"INT32"),  "
 		"[%"INT32" - %"INT32"] (%"INT32")\n", 
 		maxD, delta, 
		start1, start1+n1-1, n1, 
		start2, start2+n2-1, n2);
	for (int32_t d=0 ; d <= maxD ; d++ ){
		DPRINTF2("D: %"INT32" forward\n", d);
		//find forward path
		for (int32_t k = -d ; k <= d ; k += 2) {
			DPRINTF2("\tK: %"INT32"\n", k);
			// get best endpoints of last iteration
			int32_t a = v1[IDX(VSIZE, k-1)];
			int32_t b = v1[IDX(VSIZE, k+1)];

			int32_t x;
			// select best path along diagonal k
			if (k == -d || k != d && a < b)	x = b;
			else x = a+1;

			int32_t y = x-k;
//			DPRINTF2("\t\tx=%"INT32" y=%"INT32"\n",x,y);
			int32_t startX = x;
			int32_t startY = y;
			// follow any matched nodes
			while (x >=0 && x < n1 && 
			       y >= 0 && y < n2 && 
			       //!xmlNodeCompare(xml1, x + start1 , 
			       //	       xml2, y + start2, opt )){
			       !xmlTagCompare(&nodes1[ x + start1 ], 
					      &nodes2[ y + start2 ] )){
				x++;
				y++;
//				DPRINTF2("\t\tx=%"INT32" y=%"INT32"\n",x,y);
			}
			// store best endpoint for this iteration
			v1[IDX(VSIZE,k)] = x;
			// store last match
			if (startX != x){
				*midX   = startX + start1;
				*midY   = startY + start2;
				*midLen = x - startX; 
			} 
			// check if odd
			//if (delta % 2 == 0) continue;
			if (reverseFinish) continue;
			// out of range for overlap
			if (k < delta-(d-1) || k > delta+(d-1)) continue;
			// check for overlap with reverse path
			int32_t j = -k+delta;
			int32_t u = v2[IDX(VSIZE,j)];
			int32_t v = u + j - delta;
			if (x-y != u-v || x < u) continue;

			// found middle match...add the start offsets
			DPRINTF2("FORWARD MATCH: x=%"INT32" y=%"INT32" u=%"INT32" v=%"INT32"\n", 
				x, y, u, v);
			DPRINTF2("pathlen=%"INT32" midX=%"INT32" midY=%"INT32" midLen=%"INT32"\n",
				2*d-1,*midX, *midY, *midLen);
			
			// return length of SES
			return 2*d-1;
		}
		DPRINTF2("D: %"INT32" reverse\n",d);
		// find reverse path
		for (int32_t k = -d ; k <= d ; k += 2) {
			DPRINTF2("\tK: %"INT32"\n", k);
			int32_t a = v2[IDX(VSIZE, k-1)];
			int32_t b = v2[IDX(VSIZE, k+1)];

			int32_t u;
			// select best path along diagonal k
			if (k == -d || k != d && a > b)  u = b;
			else                             u = a-1;

			int32_t v = u - delta + k;

			DPRINTF2("\t\tu=%"INT32" v=%"INT32"\n",u,v);
			// follow any matched nodes
			int32_t startU = u;
			//int32_t startV = v;
			while (u > 0 && v > 0 && 
			       (!xmlTagCompare(&nodes1[ u-1 + start1 ], 
					       &nodes2[ v-1 + start2 ]))){
				u--;
				v--;
				DPRINTF2("\t\tu=%"INT32" v=%"INT32"\n",u,v);
			}
			v2[IDX(VSIZE,k)] = u;
			// save last match
			if (u != startU){
				*midX   = u + start1;
				*midY   = v + start2;
				*midLen = startU - u;
			} 
			// only check if even
			//if (delta % 2) continue; 
			if (!reverseFinish) continue;
			int32_t j=-k+delta;
			// out of range for overlap
			if (j < -d || j > d) continue;
			// check for overlap with forward path
			int32_t x = v1[IDX(VSIZE,j)];
			int32_t y = x-j;
			if (x-y != u-v || x < u) continue;

			// found middle match...add the start offsets
			DPRINTF2("REVERSE MATCH: pathlen=%"INT32" "
				"x=%"INT32" y=%"INT32" u=%"INT32" v=%"INT32"\n", 
				2*d, x, y, u, v);

			DPRINTF2("pathlen=%"INT32" midX=%"INT32" midY=%"INT32" midLen=%"INT32"\n",
				2*d,*midX, *midY, *midLen);

			// return length of SES
			return 2*d;
		}
	}
	// Error..no edit path exists
	return -1;

}
// This will return the length of the int16_test edit path between
// xml1(start1, n1 nodes) and xml2(start2, n2 nodes), 
// return the middle "snake" (matching nodes) in mid[XYUV] if one exists
int32_t editPath(char *seq1, char *seq2,
	      const DiffOpt *opt,
	      const int32_t start1, const int32_t start2,
	      const int32_t n1, const int32_t n2,
	      int32_t *midX, int32_t *midY, int32_t *midLen){
	
	int32_t v1[VSIZE];
	int32_t v2[VSIZE];
	
	int32_t delta = n1-n2;
	// the algorithm terminates on a reverse iteration if the difference 
	// in length is even
	bool reverseFinish =  (delta % 2 == 0);

	int32_t maxD = n1+n2;
	// round up
	if (maxD % 2) maxD++;
	maxD /= 2;
	if (maxD>MAXD) maxD = MAXD;
	
	v1[1] = 0;
	v2[1] = n1;
	*midLen = 0;
	//DPRINTF("xmlEditPath: maxD:%"INT32" delta:%"INT32" s1:%"INT32" s2:%"INT32" n1:%"INT32" n2:%"INT32"\n", 
 	DPRINTF2("xmlEditPath: maxD:%"INT32" delta:%"INT32" "
 		"[%"INT32" - %"INT32"] (%"INT32"),  "
 		"[%"INT32" - %"INT32"] (%"INT32")\n", 
 		maxD, delta, 
 		start1, start1+n1-1, n1, 
 		start2, start2+n2-1, n2);
	for (int32_t d=0 ; d <= maxD ; d++ ){
		DPRINTF2("D: %"INT32" forward\n", d);
		//find forward path
		for (int32_t k = -d ; k <= d ; k += 2) {
			DPRINTF2("\tK: %"INT32"\n", k);
			// get best endpoints of last iteration
			int32_t a = v1[IDX(VSIZE, k-1)];
			int32_t b = v1[IDX(VSIZE, k+1)];

			int32_t x;
			// select best path along diagonal k
			if (k == -d || k != d && a < b)	x = b;
			else x = a+1;

			int32_t y = x-k;
			DPRINTF2("\t\tx=%"INT32" y=%"INT32"\n",x,y);
			int32_t startX = x;
			int32_t startY = y;
			// follow any matched nodes
			while (x >=0 && x < n1 && 
			       y >= 0 && y < n2 && 
			       !opt->m_compare(seq1+opt->m_eltSize*(x+start1), 
					       seq2+opt->m_eltSize*(y+start2))){
				x++;
				y++;
//				DPRINTF2("\t\tx=%"INT32" y=%"INT32"\n",x,y);
			}
			// store best endpoint for this iteration
			v1[IDX(VSIZE,k)] = x;
			// store last match
			if (startX != x){
				*midX   = startX + start1;
				*midY   = startY + start2;
				*midLen = x - startX; 
			} 
			// check if odd
			//if (delta % 2 == 0) continue;
			if (reverseFinish) continue;
			// out of range for overlap
			if (k < delta-(d-1) || k > delta+(d-1)) continue;
			// check for overlap with reverse path
			int32_t j = -k+delta;
			int32_t u = v2[IDX(VSIZE,j)];
			int32_t v = u + j - delta;
			if (x-y != u-v || x < u) continue;

			// found middle match...add the start offsets
			DPRINTF2("FORWARD MATCH: x=%"INT32" y=%"INT32" u=%"INT32" v=%"INT32"\n", 
				x, y, u, v);
			DPRINTF2("pathlen=%"INT32" midX=%"INT32" midY=%"INT32" midLen=%"INT32"\n",
				2*d-1,*midX, *midY, *midLen);
			
			// return length of SES
			return 2*d-1;
		}
		DPRINTF2("D: %"INT32" reverse\n",d);
		// find reverse path
		for (int32_t k = -d ; k <= d ; k += 2) {
			DPRINTF2("\tK: %"INT32"\n", k);
			int32_t a = v2[IDX(VSIZE, k-1)];
			int32_t b = v2[IDX(VSIZE, k+1)];

			int32_t u;
			// select best path along diagonal k
			if (k == -d || k != d && a > b)  u = b;
			else                             u = a-1;

			int32_t v = u - delta + k;

			DPRINTF2("\t\tu=%"INT32" v=%"INT32"\n",u,v);
			// follow any matched nodes
			int32_t startU = u;
			//int32_t startV = v;
			while (u > 0 && v > 0 && 
			       !opt->m_compare(seq1+
					       opt->m_eltSize*(u-1+start1),
					       seq2+
					       opt->m_eltSize*(v-1+start2))){
				u--;
				v--;
				DPRINTF2("\t\tu=%"INT32" v=%"INT32"\n",u,v);
			}
			v2[IDX(VSIZE,k)] = u;
			// save last match
			if (u != startU){
				*midX   = u + start1;
				*midY   = v + start2;
				*midLen = startU - u;
			} 
			// only check if even
			//if (delta % 2) continue; 
			if (!reverseFinish) continue;
			int32_t j=-k+delta;
			// out of range for overlap
			if (j < -d || j > d) continue;
			// check for overlap with forward path
			int32_t x = v1[IDX(VSIZE,j)];
			int32_t y = x-j;
			if (x-y != u-v || x < u) continue;

			// found middle match...add the start offsets
			DPRINTF2("REVERSE MATCH: pathlen=%"INT32" "
				"x=%"INT32" y=%"INT32" u=%"INT32" v=%"INT32"\n", 
				2*d, x, y, u, v);

			DPRINTF2("pathlen=%"INT32" midX=%"INT32" midY=%"INT32" midLen=%"INT32"\n",
				2*d,*midX, *midY, *midLen);

			// return length of SES
			return 2*d;
		}
	}
	// Error..no edit path exists
	return -1;

}


int xmlNodeCompare(Xml *xml1, const int32_t index1, 
		   Xml *xml2, const int32_t index2, 
		   const DiffOpt *opt){
	// compare tag ids
	const int16_t id1 = xml1->getNodeId(index1);
	const int16_t id2 = xml2->getNodeId(index2);
	if (id1 != id2) return id2 - id1;

	// compare tag hashes 
	const int32_t h1 = (int32_t) xml1->getNodeHash(index1);
	const int32_t h2 = (int32_t) xml2->getNodeHash(index2);
	
	if (h1 != h2 || opt->m_tagOnly) 
		return h2 - h1;

	const int32_t len1 = xml1->getNodeLen(index1);
	const int32_t len2 = xml2->getNodeLen(index2);
	if (len1 != len2) return len2 - len1;
	
	const char *str1 = xml1->getNode(index1);
	const char *str2 = xml2->getNode(index2);

	// just compare the bytes for now...
	// this won't work if the content is identical but charset is different
	return memcmp(str1,str2, len1);

	//if (xml1->isUnicode() && xml2->isUnicode()){
	//	int32_t len = a >> 1;
	//	return ucStrCmp((UChar*)str1, len, (UChar*)str2, len);
	//}
}

int xmlTagCompare(void *elt1, void *elt2){

	XmlNode *node1 = (XmlNode*)elt1;
	XmlNode *node2 = (XmlNode*)elt2;
	// compare tag ids
	const int16_t id1 = node1->getNodeId();
	const int16_t id2 = node2->getNodeId();
	if (id1 != id2) return id2 - id1;
	
	// compare tag hashes 
	const int64_t h1 = node1->getNodeHash();
	const int64_t h2 = node2->getNodeHash();
	return h2 - h1;
}

int xmlTagCompare2(void *elt1, void *elt2){

	XmlNode *node1 = (XmlNode*)elt1;
	XmlNode *node2 = (XmlNode*)elt2;
	// compare tag ids
	const int16_t id1 = node1->getNodeId();
	const int16_t id2 = node2->getNodeId();
	if (id1 != id2) return id2 - id1;
	
	if (id1 == 0){ // text node
		// skip whitespace nodes...all are equivalent
		// This should probably be extended to match all text nodes
		// while ignoring whitespace, but we'll try this for now
		bool isWS1 = true;
		for (char *c = node1->m_node ;
		     isWS1 && c < node1->m_node+node1->m_nodeLen ; c++)
			if (*c && *c!=' ' && *c!='\t' && *c!='\n' && *c!='\r')
				isWS1=false;
		bool isWS2 = true;
		for (char *c = node2->m_node ;
		     isWS2 && c < node2->m_node+node2->m_nodeLen ; c++)
			if (*c && *c!=' ' && *c!='\t' && *c!='\n' && *c!='\r')
				isWS2=false;
		if (isWS1 && isWS2) return 0;

	}
	// compare tag hashes 
	const int64_t h1 = node1->getNodeHash();
	const int64_t h2 = node2->getNodeHash();
	if (h1 != h2) return h2 - h1;

	const int32_t len1 = node1->getNodeLen();
	const int32_t len2 = node2->getNodeLen();
	if (len1 != len2) return len2-len1;

	return memcmp(node1->getNode(), node2->getNode(), node1->getNodeLen());
}

// is this an empty text node?
bool xmlWhitespaceNode(XmlNode *node){
	if (node->isTag()) return false;
	// should be ok for utf16, since the other byte in the pair is null
	for (char *c = node->m_node; c < node->m_node+node->m_nodeLen; c++)
		if (*c && *c!=' ' && *c!='\t' && *c!='\n' && *c!='\r')
			return false;
	return true;
}
