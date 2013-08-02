
#include "Mem.h"

#include "Flags.h"
#include "Sanity.h"
#include "Timer.h"

const long Flags::NoMin = 2147483647;
const long Flags::NoMax = -1; 

Flags::Flags() {
	m_flags      = NULL;
	m_numFlags   = 0;
	m_numSet     = 0;
	m_highestSet = NoMax;
	m_lowestSet  = NoMin;
}

Flags::~Flags() {
	if ( m_flags ) {
		mfree( m_flags, m_numFlags*sizeof(char), "Flags" );
		m_flags = NULL;
	}
}

void Flags::reset() {
	if ( m_flags ) memset( m_flags, 0, m_numFlags*sizeof(char) ); 
	m_numSet     = 0;
	m_highestSet = NoMax;
	m_lowestSet  = NoMin;
}

bool Flags::resize( long size ) { 
	if ( size < 0           ) return false;
	if ( size == m_numFlags ) return true;
	
	char *newFlags = (char *)mcalloc( size*sizeof(char), "Flags" );
	if ( ! newFlags ) return false;

	m_numSet     = 0;
	m_highestSet = NoMax;
	m_lowestSet  = NoMin;
	if ( m_flags ) {
		// copy as many of old flags over as possible
		long min = m_numFlags;
		if ( min > size ) min = size;
		memcpy( newFlags, m_flags, min*sizeof(char) );
		mfree( m_flags, m_numFlags*sizeof(char), "Flags" );
		m_flags = NULL;
		// find new values for member variables
		for ( long i = 0; i < min; i++ ) {
			if ( newFlags[i] ) {
				m_numSet++;
				if ( i > m_highestSet ) m_highestSet = i;
				if ( i < m_lowestSet  ) m_lowestSet  = i;
			}
		}
	}
	m_flags      = newFlags;
	m_numFlags   = size;

	return (m_flags != NULL);
}

void Flags::setFlag( long n, char set ) { 
	if ( m_flags[n] == set ) return;

	// set the value of flag[n]
	char prevSet = m_flags[n]; 
	m_flags[n] = set; 
	// if just changing the value, we can stop right here
	if ( prevSet && set ) return;

	if ( set ) {
		m_numSet++;
		//SANITYCHECK( m_numSet <= m_numFlags );
		if ( n < m_lowestSet  ) m_lowestSet  = n;
		if ( n > m_highestSet ) m_highestSet = n;
		//SANITYCHECK( m_highestSet >= m_lowestSet );
	}
	else {
		m_numSet--;

		// recalc m_lowestSet and m_highestSet
		//SANITYCHECK( m_numSet >= 0);
		if ( m_numSet == 0 ) {
			m_lowestSet = NoMin;
			m_highestSet = NoMax;
			return;
		}
		if ( n == m_lowestSet ) {
			for ( ; n <= m_highestSet ; n++ ) 
				if ( m_flags[n] ) break;
			if ( n < m_numFlags ) m_lowestSet = n;
			else    	      m_lowestSet = NoMin;  
		}
		//SANITYCHECK( (m_numSet > 0) ? m_lowestSet != NoMin : true );
		//SANITYCHECK( m_flags[m_lowestSet] );
		if ( n == m_highestSet ) {
			for ( ; n >= m_lowestSet ; n-- )
				if ( m_flags[n] ) break;
			if ( n >= 0 ) m_highestSet = n;
			else          m_highestSet = NoMax;
		}
		//SANITYCHECK( (m_numSet > 0) ? m_highestSet != NoMax : true );
		//SANITYCHECK( m_flags[m_highestSet] );
		//SANITYCHECK( m_highestSet >= m_lowestSet );
	}
}

void Flags::dumpFlags() {
	for ( long i = 0; i < m_numFlags; i++ ) {
		log(LOG_DEBUG, "AWL: Flag %ld:%d", i, (int)m_flags[i]);
	}
}

void testFlags() {
	log(LOG_DEBUG, "AWL: testing Flags...");
	Flags flags;
	log(LOG_DEBUG, "AWL: testing Flags 0...");
	flags.resize( 0 );
	SANITYCHECK( flags.getNumFlags() == 0 );
	SANITYCHECK( flags.getNumSet() == 0 );
	SANITYCHECK( flags.getLowestSet() == flags.NoMin );
	SANITYCHECK( flags.getHighestSet() == flags.NoMax );
	log(LOG_DEBUG, "AWL: testing Flags 1...");
	flags.resize( 1 );
	SANITYCHECK( flags.getNumFlags() == 1 );
	SANITYCHECK( ! flags.getFlag( 0 ) );
	SANITYCHECK( flags.getNumSet() == 0 );
	SANITYCHECK( flags.getLowestSet() == flags.NoMin );
	SANITYCHECK( flags.getHighestSet() == flags.NoMax );
	log(LOG_DEBUG, "AWL: testing Flags 2...");
	flags.setFlag( 0, true );
	SANITYCHECK( flags.getFlag( 0 ) );
	SANITYCHECK( flags.getLowestSet() == flags.getHighestSet() );
	SANITYCHECK( flags.getLowestSet() == 0 );
	SANITYCHECK( flags.getNumSet() == 1 );
	SANITYCHECK( flags.getHighestSet() >= flags.getLowestSet() );
	log(LOG_DEBUG, "AWL: testing Flags 3...");
	flags.setFlag( 0, false );
	SANITYCHECK( ! flags.getFlag( 0 ) );
	SANITYCHECK( flags.getLowestSet() == flags.NoMin );
	SANITYCHECK( flags.getHighestSet() == flags.NoMax );
	SANITYCHECK( flags.getNumSet() == 0 );
	log(LOG_DEBUG, "AWL: testing Flags 4...");
	flags.setFlag( 0, true );
	flags.resize( 5 );
	flags.setFlag( 1, true );
	SANITYCHECK( flags.getNumFlags() == 5 );
	SANITYCHECK( flags.getFlag(0) );
	SANITYCHECK( flags.getFlag(1) );
	SANITYCHECK( flags.getLowestSet() == 0 );
	SANITYCHECK( flags.getHighestSet() == 1 );
	SANITYCHECK( flags.getNumSet() == 2 );
	SANITYCHECK( flags.getHighestSet() >= flags.getLowestSet() );
	log(LOG_DEBUG, "AWL: testing Flags 5...");
	flags.setFlag( 3, true );
	SANITYCHECK( flags.getFlag( 3 ) );
	SANITYCHECK( flags.getLowestSet() == 0 );
	SANITYCHECK( flags.getHighestSet() == 3 );
	SANITYCHECK( flags.getNumSet() == 3 );
	SANITYCHECK( flags.getHighestSet() >= flags.getLowestSet() );
	log(LOG_DEBUG, "AWL: testing Flags 6...");
	flags.setFlag( 0, false );
	SANITYCHECK( ! flags.getFlag( 0 ) );
	SANITYCHECK( flags.getLowestSet() == 1 );
	SANITYCHECK( flags.getHighestSet() == 3 );
	SANITYCHECK( flags.getNumSet() == 2 );
	SANITYCHECK( flags.getHighestSet() >= flags.getLowestSet() );
	log(LOG_DEBUG, "AWL: testing Flags 7...");
	flags.setFlag( 0, true );
	flags.setFlag( 2, true );
	SANITYCHECK( flags.getFlag( 0 ) );
	SANITYCHECK( flags.getFlag( 2 ) );
	SANITYCHECK( flags.getLowestSet() == 0 );
	SANITYCHECK( flags.getHighestSet() == 3 );
	SANITYCHECK( flags.getNumSet() == 4 );
	SANITYCHECK( flags.getHighestSet() >= flags.getLowestSet() );
	log(LOG_DEBUG, "AWL: testing Flags 8...");
	flags.setFlag( 2, false );
	SANITYCHECK( ! flags.getFlag( 2 ) );
	SANITYCHECK( flags.getFlag( 0 ) );
	SANITYCHECK( flags.getLowestSet() == 0 );
	SANITYCHECK( flags.getHighestSet() == 3 );
	SANITYCHECK( flags.getNumSet() == 3 );
	SANITYCHECK( flags.getHighestSet() >= flags.getLowestSet() );
	log(LOG_DEBUG, "AWL: testing Flags 9...");
	flags.setFlag( 2, 5 );
	flags.setFlag( 3, false );
	SANITYCHECK( flags.getFlag( 0 ) );
	SANITYCHECK( 5 == flags.getFlag( 2 ) );
	SANITYCHECK( ! flags.getFlag( 3 ) );
	SANITYCHECK( flags.getLowestSet() == 0 );
	SANITYCHECK( flags.getHighestSet() == 2 );
	SANITYCHECK( flags.getNumSet() == 3 );
	SANITYCHECK( flags.getHighestSet() >= flags.getLowestSet() );
	log(LOG_DEBUG, "AWL: testing Flags 10...");
	flags.reset();
	SANITYCHECK( flags.getNumFlags() == 5 );
	for ( long i = 0; i < flags.getNumFlags(); i++ ) {
		SANITYCHECK( ! flags.getFlag( i ) );
	}
	SANITYCHECK( flags.getLowestSet() == flags.NoMin );
	SANITYCHECK( flags.getHighestSet() == flags.NoMax );
	SANITYCHECK( flags.getNumSet() == 0 );
	log(LOG_DEBUG, "AWL: Testing Flags 11...");
	flags.setFlag( 0, true );
	flags.setFlag( 2, 0x33 );
	flags.setFlag( 4, true );
	SANITYCHECK( flags.getFlag( 0 ) );
	SANITYCHECK( ! flags.getFlag( 1 ) );
	SANITYCHECK( 0x33 == flags.getFlag( 2 ) );
	SANITYCHECK( ! flags.getFlag( 3 ) );
	SANITYCHECK( flags.getFlag( 4 ) );
	SANITYCHECK( flags.getLowestSet() == 0 );
	SANITYCHECK( flags.getHighestSet() == 4 );
	SANITYCHECK( flags.getNumSet() == 3 );
	SANITYCHECK( flags.getHighestSet() >= flags.getLowestSet() );
	log(LOG_DEBUG, "AWL: Testing Flags 12...");
	flags.resize( 3 );
	SANITYCHECK( flags.getNumFlags() == 3 );
	SANITYCHECK( flags.getHighestSet() == 2 );
	SANITYCHECK( flags.getLowestSet() == 0 );
	SANITYCHECK( flags.getNumSet() == 2 );
	SANITYCHECK( flags.getHighestSet() >= flags.getLowestSet() );
	log(LOG_DEBUG, "AWL: Testing Flags 13...");
	flags.resize( 1 );
	SANITYCHECK( flags.getNumFlags() == 1 );
	SANITYCHECK( flags.getHighestSet() == 0 );
	SANITYCHECK( flags.getLowestSet() == 0 );
	SANITYCHECK( flags.getNumSet() == 1 );
	SANITYCHECK( flags.getHighestSet() >= flags.getLowestSet() );
	log(LOG_DEBUG, "AWL: Testing Flags 14...");
	flags.reset();
	SANITYCHECK( flags.getNumSet() == 0 );
	SANITYCHECK( flags.getNumFlags() == 1 );
	SANITYCHECK( flags.getLowestSet() == flags.NoMin );
	SANITYCHECK( flags.getHighestSet() == flags.NoMax );
	for ( long i = 0; i < flags.getNumFlags(); i++ ) {
		SANITYCHECK( ! flags.getFlag( i ) );
	}	
	const long TESTCASESIZE = 100000;
	if ( ! flags.resize( TESTCASESIZE ) ) {
		log(LOG_DEBUG, "AWL: Cannot allocate memory to test Flags!");
		return;
	}
	SANITYCHECK( flags.getNumSet() == 0 );
	SANITYCHECK( flags.getNumFlags() == TESTCASESIZE );
	SANITYCHECK( flags.getLowestSet() == flags.NoMin );
	SANITYCHECK( flags.getHighestSet() == flags.NoMax );
	//flags.dumpFlags();
	for ( long i = 0; i < flags.getNumFlags(); i++ ) {
		SANITYCHECK( ! flags.getFlag( i ) );
	}
	log(LOG_DEBUG, "AWL: Testing Flags 15 (test case size:%ld)...", 
	    TESTCASESIZE);
	Timer timer;
	timer.start();
	long cnt = 0;
	long ls = flags.NoMin;
	long hs = flags.NoMax;
	for ( long i = 0; i < TESTCASESIZE; i++ ) {
		long j = random() % TESTCASESIZE;
		char r = random() % 2;
		if ( 0 == r ) {
			r = random() % 256;
			if (r == 0) r = -1;
			char wasSet = flags.getFlag( j );
			if ( ! wasSet ) cnt++;
			flags.setFlag( j, r );
			SANITYCHECK( flags.getFlag( j ) == r );
			//log(LOG_DEBUG, "AWL: set j:%ld wasSet:%d set:%ld, cnt:%ld", 
			//    j, wasSet, flags.getNumSet(), cnt);
			SANITYCHECK( flags.getNumSet() == cnt );
			SANITYCHECK( flags.getNumFlags() == TESTCASESIZE );
			if ( hs < j ) {
				SANITYCHECK( flags.getHighestSet() == j );
				hs = j;
			}
			if ( ls > j ) {
				SANITYCHECK( flags.getLowestSet() == j );
				ls = j;
			}
			SANITYCHECK( flags.getHighestSet() >= flags.getLowestSet() );
		}
		else {
			char wasSet = flags.getFlag( j );
			if ( wasSet ) cnt--;
			flags.setFlag( j, false );
			if ( ls == j ) {
				SANITYCHECK( flags.getLowestSet() == flags.NoMin ||
					     flags.getLowestSet() > j );
				ls = flags.getLowestSet();
			}
			if ( hs == j ) {
				SANITYCHECK( flags.getHighestSet() == flags.NoMax ||
					     flags.getHighestSet() < j );
				hs = flags.getHighestSet();
			}
			//log(LOG_DEBUG, "AWL: unset j:%ld wasSet:%d set:%ld, cnt:%ld", 
			//    j, wasSet, flags.getNumSet(), cnt);
			SANITYCHECK( ! flags.getFlag( j ) );
			SANITYCHECK( flags.getNumSet() == cnt );
			SANITYCHECK( flags.getNumFlags() == TESTCASESIZE );
			if ( cnt > 0 )
				SANITYCHECK( flags.getHighestSet() >= flags.getLowestSet() );
			else {
				ls = flags.NoMin;
				hs = flags.NoMax;
			}
			SANITYCHECK( flags.getLowestSet() == ls );
			SANITYCHECK( flags.getHighestSet() == hs );
		}
	}
	flags.reset();
	SANITYCHECK( flags.getNumFlags() == TESTCASESIZE );
	SANITYCHECK( flags.getHighestSet() == flags.NoMax );
	SANITYCHECK( flags.getLowestSet() == flags.NoMin );
	SANITYCHECK( flags.getNumSet() == 0 );
	for ( long i = 0; i < flags.getNumFlags(); i++ ) {
		SANITYCHECK( ! flags.getFlag( i ) );
	}
	timer.stop();
	log("AWL: Flags %ld tests took %lld ms", TESTCASESIZE, timer.getSpan());
	log(LOG_DEBUG, "AWL: Flags tests passed. :)");
}
