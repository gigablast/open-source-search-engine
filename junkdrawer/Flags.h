
#ifndef __FLAGS_H_
#define __FLAGS_H_

class Flags {
public:
	static const int32_t NoMin;
	static const int32_t NoMax;

	Flags();
	~Flags();
	
	void reset  ();
	bool resize ( int32_t size );
	
	char getFlag ( int32_t n           ) { return m_flags[n]; };
	void setFlag ( int32_t n, char set );

	int32_t getNumFlags () { return m_numFlags; };
	int32_t getNumSet   () { return m_numSet; };
	int32_t getHighestSet () { return m_highestSet; };
	int32_t getLowestSet  () { return m_lowestSet; };
	

	void dumpFlags();

private:
	int32_t  m_numFlags;   
	int32_t  m_numSet;     
	int32_t  m_highestSet; 
	int32_t  m_lowestSet;  

	char *m_flags;
};

void testFlags();

#endif // __FLAGS_H_
