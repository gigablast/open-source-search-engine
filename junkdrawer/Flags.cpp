
#include "Mem.h"

#include "Flags.h"
#include "Sanity.h"
#include "Timer.h"

const int32_t Flags::NoMin = 2147483647;
const int32_t Flags::NoMax = -1; 

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

bool Flags::resize( int32_t size ) { 
	if ( size < 0           ) return false;
	if ( size == m_numFlags ) return true;
	
	char *newFlags = (char *)mcalloc( size*sizeof(char), "Flags" );
	if ( ! newFlags ) return false;

	m_numSet     = 0;
	m_highestSet = NoMax;
	m_lowestSet  = NoMin;
	if ( m_flags ) {
		// copy as many of old flags over as possible
		int32_t min = m_numFlags;
		if ( min > size ) min = size;
		gbmemcpy( newFlags, m_flags, min*sizeof(char) );
		mfree( m_flags, m_numFlags*sizeof(char), "Flags" );
		m_flags = NULL;
		// find new values for member variables
		for ( int32_t i = 0; i < min; i++ ) {
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

void Flags::setFlag( int32_t n, char set ) { 
	if ( m_flags[n] == set ) return;

	// set the value of flag[n]
	char prevSet = m_flags[n]; 
	m_flags[n] = set; 
	// if just changing the value, we can stop right here
	if ( prevSet && set ) return;

	if ( set ) {
		m_numSet++;
		//GBASSERT( m_numSet <= m_numFlags );
		if ( n < m_lowestSet  ) m_lowestSet  = n;
		if ( n > m_highestSet ) m_highestSet = n;
		//GBASSERT( m_highestSet >= m_lowestSet );
	}
	else {
		m_numSet--;

		// recalc m_lowestSet and m_highestSet
		//GBASSERT( m_numSet >= 0);
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
		//GBASSERT( (m_numSet > 0) ? m_lowestSet != NoMin : true );
		//GBASSERT( m_flags[m_lowestSet] );
		if ( n == m_highestSet ) {
			for ( ; n >= m_lowestSet ; n-- )
				if ( m_flags[n] ) break;
			if ( n >= 0 ) m_highestSet = n;
			else          m_highestSet = NoMax;
		}
		//GBASSERT( (m_numSet > 0) ? m_highestSet != NoMax : true );
		//GBASSERT( m_flags[m_highestSet] );
		//GBASSERT( m_highestSet >= m_lowestSet );
	}
}

void Flags::dumpFlags() {
	for ( int32_t i = 0; i < m_numFlags; i++ ) {
		log(LOG_DEBUG, "AWL: Flag %"INT32":%d", i, (int)m_flags[i]);
	}
}

void testFlags() {
	log(LOG_DEBUG, "AWL: testing Flags...");
	Flags flags;
	log(LOG_DEBUG, "AWL: testing Flags 0...");
	flags.resize( 0 );
	GBASSERT( flags.getNumFlags() == 0 );
	GBASSERT( flags.getNumSet() == 0 );
	GBASSERT( flags.getLowestSet() == flags.NoMin );
	GBASSERT( flags.getHighestSet() == flags.NoMax );
	log(LOG_DEBUG, "AWL: testing Flags 1...");
	flags.resize( 1 );
	GBASSERT( flags.getNumFlags() == 1 );
	GBASSERT( ! flags.getFlag( 0 ) );
	GBASSERT( flags.getNumSet() == 0 );
	GBASSERT( flags.getLowestSet() == flags.NoMin );
	GBASSERT( flags.getHighestSet() == flags.NoMax );
	log(LOG_DEBUG, "AWL: testing Flags 2...");
	flags.setFlag( 0, true );
	GBASSERT( flags.getFlag( 0 ) );
	GBASSERT( flags.getLowestSet() == flags.getHighestSet() );
	GBASSERT( flags.getLowestSet() == 0 );
	GBASSERT( flags.getNumSet() == 1 );
	GBASSERT( flags.getHighestSet() >= flags.getLowestSet() );
	log(LOG_DEBUG, "AWL: testing Flags 3...");
	flags.setFlag( 0, false );
	GBASSERT( ! flags.getFlag( 0 ) );
	GBASSERT( flags.getLowestSet() == flags.NoMin );
	GBASSERT( flags.getHighestSet() == flags.NoMax );
	GBASSERT( flags.getNumSet() == 0 );
	log(LOG_DEBUG, "AWL: testing Flags 4...");
	flags.setFlag( 0, true );
	flags.resize( 5 );
	flags.setFlag( 1, true );
	GBASSERT( flags.getNumFlags() == 5 );
	GBASSERT( flags.getFlag(0) );
	GBASSERT( flags.getFlag(1) );
	GBASSERT( flags.getLowestSet() == 0 );
	GBASSERT( flags.getHighestSet() == 1 );
	GBASSERT( flags.getNumSet() == 2 );
	GBASSERT( flags.getHighestSet() >= flags.getLowestSet() );
	log(LOG_DEBUG, "AWL: testing Flags 5...");
	flags.setFlag( 3, true );
	GBASSERT( flags.getFlag( 3 ) );
	GBASSERT( flags.getLowestSet() == 0 );
	GBASSERT( flags.getHighestSet() == 3 );
	GBASSERT( flags.getNumSet() == 3 );
	GBASSERT( flags.getHighestSet() >= flags.getLowestSet() );
	log(LOG_DEBUG, "AWL: testing Flags 6...");
	flags.setFlag( 0, false );
	GBASSERT( ! flags.getFlag( 0 ) );
	GBASSERT( flags.getLowestSet() == 1 );
	GBASSERT( flags.getHighestSet() == 3 );
	GBASSERT( flags.getNumSet() == 2 );
	GBASSERT( flags.getHighestSet() >= flags.getLowestSet() );
	log(LOG_DEBUG, "AWL: testing Flags 7...");
	flags.setFlag( 0, true );
	flags.setFlag( 2, true );
	GBASSERT( flags.getFlag( 0 ) );
	GBASSERT( flags.getFlag( 2 ) );
	GBASSERT( flags.getLowestSet() == 0 );
	GBASSERT( flags.getHighestSet() == 3 );
	GBASSERT( flags.getNumSet() == 4 );
	GBASSERT( flags.getHighestSet() >= flags.getLowestSet() );
	log(LOG_DEBUG, "AWL: testing Flags 8...");
	flags.setFlag( 2, false );
	GBASSERT( ! flags.getFlag( 2 ) );
	GBASSERT( flags.getFlag( 0 ) );
	GBASSERT( flags.getLowestSet() == 0 );
	GBASSERT( flags.getHighestSet() == 3 );
	GBASSERT( flags.getNumSet() == 3 );
	GBASSERT( flags.getHighestSet() >= flags.getLowestSet() );
	log(LOG_DEBUG, "AWL: testing Flags 9...");
	flags.setFlag( 2, 5 );
	flags.setFlag( 3, false );
	GBASSERT( flags.getFlag( 0 ) );
	GBASSERT( 5 == flags.getFlag( 2 ) );
	GBASSERT( ! flags.getFlag( 3 ) );
	GBASSERT( flags.getLowestSet() == 0 );
	GBASSERT( flags.getHighestSet() == 2 );
	GBASSERT( flags.getNumSet() == 3 );
	GBASSERT( flags.getHighestSet() >= flags.getLowestSet() );
	log(LOG_DEBUG, "AWL: testing Flags 10...");
	flags.reset();
	GBASSERT( flags.getNumFlags() == 5 );
	for ( int32_t i = 0; i < flags.getNumFlags(); i++ ) {
		GBASSERT( ! flags.getFlag( i ) );
	}
	GBASSERT( flags.getLowestSet() == flags.NoMin );
	GBASSERT( flags.getHighestSet() == flags.NoMax );
	GBASSERT( flags.getNumSet() == 0 );
	log(LOG_DEBUG, "AWL: Testing Flags 11...");
	flags.setFlag( 0, true );
	flags.setFlag( 2, 0x33 );
	flags.setFlag( 4, true );
	GBASSERT( flags.getFlag( 0 ) );
	GBASSERT( ! flags.getFlag( 1 ) );
	GBASSERT( 0x33 == flags.getFlag( 2 ) );
	GBASSERT( ! flags.getFlag( 3 ) );
	GBASSERT( flags.getFlag( 4 ) );
	GBASSERT( flags.getLowestSet() == 0 );
	GBASSERT( flags.getHighestSet() == 4 );
	GBASSERT( flags.getNumSet() == 3 );
	GBASSERT( flags.getHighestSet() >= flags.getLowestSet() );
	log(LOG_DEBUG, "AWL: Testing Flags 12...");
	flags.resize( 3 );
	GBASSERT( flags.getNumFlags() == 3 );
	GBASSERT( flags.getHighestSet() == 2 );
	GBASSERT( flags.getLowestSet() == 0 );
	GBASSERT( flags.getNumSet() == 2 );
	GBASSERT( flags.getHighestSet() >= flags.getLowestSet() );
	log(LOG_DEBUG, "AWL: Testing Flags 13...");
	flags.resize( 1 );
	GBASSERT( flags.getNumFlags() == 1 );
	GBASSERT( flags.getHighestSet() == 0 );
	GBASSERT( flags.getLowestSet() == 0 );
	GBASSERT( flags.getNumSet() == 1 );
	GBASSERT( flags.getHighestSet() >= flags.getLowestSet() );
	log(LOG_DEBUG, "AWL: Testing Flags 14...");
	flags.reset();
	GBASSERT( flags.getNumSet() == 0 );
	GBASSERT( flags.getNumFlags() == 1 );
	GBASSERT( flags.getLowestSet() == flags.NoMin );
	GBASSERT( flags.getHighestSet() == flags.NoMax );
	for ( int32_t i = 0; i < flags.getNumFlags(); i++ ) {
		GBASSERT( ! flags.getFlag( i ) );
	}	
	const int32_t TESTCASESIZE = 100000;
	if ( ! flags.resize( TESTCASESIZE ) ) {
		log(LOG_DEBUG, "AWL: Cannot allocate memory to test Flags!");
		return;
	}
	GBASSERT( flags.getNumSet() == 0 );
	GBASSERT( flags.getNumFlags() == TESTCASESIZE );
	GBASSERT( flags.getLowestSet() == flags.NoMin );
	GBASSERT( flags.getHighestSet() == flags.NoMax );
	//flags.dumpFlags();
	for ( int32_t i = 0; i < flags.getNumFlags(); i++ ) {
		GBASSERT( ! flags.getFlag( i ) );
	}
	log(LOG_DEBUG, "AWL: Testing Flags 15 (test case size:%"INT32")...", 
	    TESTCASESIZE);
	Timer timer;
	timer.start();
	int32_t cnt = 0;
	int32_t ls = flags.NoMin;
	int32_t hs = flags.NoMax;
	for ( int32_t i = 0; i < TESTCASESIZE; i++ ) {
		int32_t j = random() % TESTCASESIZE;
		char r = random() % 2;
		if ( 0 == r ) {
			r = random() % 256;
			if (r == 0) r = -1;
			char wasSet = flags.getFlag( j );
			if ( ! wasSet ) cnt++;
			flags.setFlag( j, r );
			GBASSERT( flags.getFlag( j ) == r );
			//log(LOG_DEBUG, "AWL: set j:%"INT32" wasSet:%d set:%"INT32", cnt:%"INT32"", 
			//    j, wasSet, flags.getNumSet(), cnt);
			GBASSERT( flags.getNumSet() == cnt );
			GBASSERT( flags.getNumFlags() == TESTCASESIZE );
			if ( hs < j ) {
				GBASSERT( flags.getHighestSet() == j );
				hs = j;
			}
			if ( ls > j ) {
				GBASSERT( flags.getLowestSet() == j );
				ls = j;
			}
			GBASSERT( flags.getHighestSet() >= flags.getLowestSet() );
		}
		else {
			char wasSet = flags.getFlag( j );
			if ( wasSet ) cnt--;
			flags.setFlag( j, false );
			if ( ls == j ) {
				GBASSERT( flags.getLowestSet() == flags.NoMin ||
					     flags.getLowestSet() > j );
				ls = flags.getLowestSet();
			}
			if ( hs == j ) {
				GBASSERT( flags.getHighestSet() == flags.NoMax ||
					     flags.getHighestSet() < j );
				hs = flags.getHighestSet();
			}
			//log(LOG_DEBUG, "AWL: unset j:%"INT32" wasSet:%d set:%"INT32", cnt:%"INT32"", 
			//    j, wasSet, flags.getNumSet(), cnt);
			GBASSERT( ! flags.getFlag( j ) );
			GBASSERT( flags.getNumSet() == cnt );
			GBASSERT( flags.getNumFlags() == TESTCASESIZE );
			if ( cnt > 0 )
				GBASSERT( flags.getHighestSet() >= flags.getLowestSet() );
			else {
				ls = flags.NoMin;
				hs = flags.NoMax;
			}
			GBASSERT( flags.getLowestSet() == ls );
			GBASSERT( flags.getHighestSet() == hs );
		}
	}
	flags.reset();
	GBASSERT( flags.getNumFlags() == TESTCASESIZE );
	GBASSERT( flags.getHighestSet() == flags.NoMax );
	GBASSERT( flags.getLowestSet() == flags.NoMin );
	GBASSERT( flags.getNumSet() == 0 );
	for ( int32_t i = 0; i < flags.getNumFlags(); i++ ) {
		GBASSERT( ! flags.getFlag( i ) );
	}
	timer.stop();
	log("AWL: Flags %"INT32" tests took %lld ms", TESTCASESIZE, timer.getSpan());
	log(LOG_DEBUG, "AWL: Flags tests passed. :)");
}
