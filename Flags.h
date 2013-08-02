
#ifndef __FLAGS_H_
#define __FLAGS_H_

class Flags {
public:
	static const long NoMin;
	static const long NoMax;

	Flags();
	~Flags();
	
	void reset  ();
	bool resize ( long size );
	
	char getFlag ( long n           ) { return m_flags[n]; };
	void setFlag ( long n, char set );

	long getNumFlags () { return m_numFlags; };
	long getNumSet   () { return m_numSet; };
	long getHighestSet () { return m_highestSet; };
	long getLowestSet  () { return m_lowestSet; };
	

	void dumpFlags();

private:
	long  m_numFlags;   
	long  m_numSet;     
	long  m_highestSet; 
	long  m_lowestSet;  

	char *m_flags;
};

void testFlags();

#endif // __FLAGS_H_
