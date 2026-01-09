#include "gb-include.h"

#include "Msg37.h"

static void gotTermFreqWrapper ( void *state ) ;

void gotTermFreqWrapper ( void *state ) {
	Msg36 *msg36 = (Msg36 *) state;
	Msg37 *THIS  = (Msg37 *) msg36->m_this;
	// it returns false if we're still awaiting replies
	if ( ! THIS->gotTermFreq ( msg36 ) ) return;
	// now call callback, we're done
	THIS->m_callback ( THIS->m_state );
}

