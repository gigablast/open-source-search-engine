// Matt Wells, copyright Aug 2005

#ifndef _POPS_H_
#define _POPS_H_

#define POPS_BUF_SIZE (10*1024)

// the max popularity score a word can have
//#define MAX_POP 10000
#define MAX_POP 0x7fff

// the popularity vector for the Words class, 1-1 with those words
class Pops {

 public:

	Pops();
	~Pops();

	// . set m_pops to the popularity of each word in "words"
	// . m_pops[] is 1-1 with the words in "words"
	// . must have computed the word ids (words->m_wordIds must be valid)
	bool set ( class Words *words, int32_t a, int32_t b );

	// from 1 (min) to 1000 (max popularity)
	int32_t *getPops ( ) { return m_pops; };

	// from 0.0 to 1.0
	float getNormalizedPop ( int32_t i ) { 
		return (float)m_pops[i]/(float)MAX_POP; };

	int32_t *m_pops;
	int32_t  m_popsSize; // in bytes
	char  m_localBuf [ POPS_BUF_SIZE ];
};

#endif
