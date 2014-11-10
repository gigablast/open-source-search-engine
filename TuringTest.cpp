#include "TuringTest.h"

TuringTest g_turingTest;

#include "SafeBuf.h"

static char *s_map[] = {
"X",
"#" ,
"#" ,
"# ###" ,
"      ###" ,
"      #  ###" ,
"      #       ##" ,
"      #    ########" ,
"      ######## " ,
"#  ######## " ,
"#######" ,
"###" ,
"#" ,
"X",
"#                 #" ,
"###################" ,
"###################" ,
"#         #       #" ,
"#         #       #" ,
"#         ##      #" ,
"##        ##      #" ,
"###     ## ########" ,
" #########  ##### " ,
"    ###" ,
"X",
"       #####" ,
"   #############" ,
"  #####      #### " ,
"###              ##" ,
"##                #" ,
"#                 #" ,
"#                 #" ,
"  #            ####" ,
"    #" ,
"X",
"#                 #" ,
"###################" ,
"###################" ,
"#                 #" ,
"#                 #" ,
"#                 #" ,
"##               ##" ,
" #####       #####" ,
"   #############" ,
"      #######" ,
"X",
"#                 #" ,
"###################" ,
"###################" ,
"#         #       #" ,
"#         #       #" ,
"#         #       #" ,
"#        ####     #" ,
"#                 #" ,
"##               ##" ,
"                   " ,
"X",
"#                 #" ,
"###################" ,
"###################" ,
"#         #       #" ,
"          #       #" ,
"          #       #" ,
"         ####     #" ,
"                  #" ,
"                 ##" ,
"                   " ,
"X",
"       #####" ,
"   #############" ,
"  #####      #### " ,
"###              ##" ,
"##                #" ,
"#                 #" ,
"##       #        #" ,
" #########     ####" ,
"   #######" ,
"X",
"#                 #" ,
"###################" ,
"###################" ,
"#         #       #" ,
"          # " ,
"          # " ,
"          # " ,
"###################" ,
"###################" ,
"#                 #" ,
"X",
"#                 #" ,
"###################" ,
"###################" ,
"#                 #" ,
"X",
"   # " ,
" #### " ,
"# ###" ,
"#" ,
"#                 #" ,
" ##################" ,
"  #################" ,
"                  #" ,
"X",
"#                 #" ,
"###################" ,
"###################" ,
"#        ###      #" ,
"       ######" ,
"   #######    #" ,
"#######        ## #" ,
"####             ##" ,
"##                #" ,
"#" ,
"X",
"#                 #" ,
"###################" ,
"###################" ,
"#                 #" ,
"#" ,
"#" ,
"#" ,
"#" ,
"##" ,
"    " ,
"X",
"#                 #" ,
"###################" ,
"#             #####" ,
"        ###########" ,
"    ##########" ,
" ########" ,
"       ####" ,
"#            ####" ,
"###################" ,
"###################" ,
"#                 #" ,
"X",
"#                 #" ,
"###################" ,
"#               ###" ,
"             ######" ,
"          ###### " ,
"       ###### " ,
"    ###### " ,
" ######           #" ,
"###################" ,
"                  #" ,
"X",
"       #####" ,
"   #############" ,
"  ####       #### " ,
"##               ##" ,
"#                 #" ,
"#                 #" ,
"##               ##" ,
" ####         ####" ,
"   ############# " ,
"      ####### " ,
"X",
"#                 #" ,
"###################" ,
"###################" ,
"#         #       #" ,
"          #       #" ,
"          ##      #" ,
"           #      #" ,
"           ########" ,
"            ##### " ,
"X",
"       #####" ,
"   #############" ,
"  ####       #### " ,
"##               ##" ,
"#                 #" ,
"# #               #" ,
"####             ##" ,
"#####         ####" ,
"## ############# " ,
" #    ####### " ,
"X",
"#                 #" ,
"###################" ,
"###################" ,
"#        ##       #" ,
"       ####       #" ,
"   #########      #" ,
"#######    #      #" ,
"####       ########" ,
"##          ##### " ,
"#" ,
"X",
"####" ,
" ##        ######" ,
"#         ###    # " ,
"#        ###      #" ,
"#        ###      #" ,
"#       ###       #" ,
"##    ####      ## " ,
"  #######      ####" ,
"    ###" ,
"X",
"               ####" ,
"                 ##" ,
"                  #" ,
"#                 #" ,
"###################" ,
"###################" ,
"#                 #" ,
"                  #" ,
"                 ##" ,
"               ####" ,
"X",
"                  #" ,
"     ##############" ,
"  #################" ,
"####              #" ,
"##" ,
"# " ,
"# " ,
" #" ,
"  ##              #" ,
"     ######### ####" ,
"                   " ,
"X",
"                  #" ,
"                  #" ,
"             ######" ,
"        ######### #" ,
"    #########" ,
"#########" ,
"     ###" ,
"          ###" ,
"              ### #" ,
"                  #" ,
"                  #" ,
"X",
"                  #" ,
"             ######" ,
"        ######### #" ,
"    #########" ,
"#########" ,
"     ###" ,
"     ########" ,
"###########" ,
"  ####" ,
"       ####" ,
"            ###   #" ,
"                ###" ,
"                  #" ,
"X",
"#                 #" ,
"#                 #" ,
"###            ####" ,
"    ##      #######" ,
"      ## ######" ,
"      ######" ,
"#  ######   ##" ,
"######         #  #" ,
"####             ##" ,
"#                 #" ,
"                  #" ,
"X",
"                  #" ,
"               ####" ,
"           ########" ,
"#        ######" ,
"############" ,
"#        ###" ,
"             ##   #" ,
"                ###" ,
"                  #" ,
"                   " ,
"X",
"               ####" ,
"##               ##" ,
"######            #" ,
"# #######         #" ,
"#    ######       #" ,
"#       ######    #" ,
"#          ###### #" ,
"#             #####" ,
"##               ##" ,
"                   " ,
"Q"
};

TuringTest::TuringTest() {
	m_nextQuestion = 1; //0 is an in-band signal for hashtable.
	m_tinit = false;
}

TuringTest::~TuringTest() {}


bool TuringTest::isHuman( HttpRequest *r) {
	// . they need to answer the turing test question
	// . get answer they gave
	int32_t ansLen;
	char *ans = r->getString("ans",&ansLen,NULL);
	// if no answer, bail
	if ( ansLen <= 0 || ! ans ) return false;

	int32_t qid = r->getLong("qid",-1);
	// convert to all upper
	if ( ansLen > 0 && ans ) to_upper3_a ( ans , ansLen , ans );


	if ( m_answers.getNumSlotsUsed() == 0 ) return false;
	// get answer hash
	int32_t ansHash = hash32 ( ans , ansLen );
	// do not allow zeroes
	if ( ansHash == 0 ) ansHash = 1;
	// . get answer from table
	// . returns 0 if not in there
	int32_t realAns = m_answers.getValue ( qid );
	// 0 means not in table
	if ( realAns !=  ansHash ) return false;
	// remove from table, so they can't use again
	m_answers.removeKey ( qid );
	return true;
}


bool TuringTest::printTest( SafeBuf* sb ) {
	if ( ! m_tinit ) {
		// clear all
		for ( int32_t a = 0 ; a < 26 ; a++ )
			for ( int32_t b = 0 ; b < TMAX_HEIGHT ; b++ )
				for ( int32_t c = 0 ; c < TMAX_WIDTH ; c++ )
					m_buf[a][b][c] = ' '; // space
		// fill it up
		int32_t n = 0;
		for ( int32_t i = 0 ; n < 26 && s_map[i][0] != 'Q' ; i++ ) {
			// skip if no letter
			if ( s_map[i][0] != 'X' ) continue;
			// loop over each line for this letter
			for ( int32_t j = i+1 ; s_map[j][0] != 'X' &&
				             s_map[j][0] != 'Q'     ; j++ ) {
				// these strings are actually columns since 
				// banner's output is transposed
				char *s = s_map[j];
				// copy line and transpose into column
				int32_t k = 0;
				while ( *s && k <= TMAX_HEIGHT ) {
					m_buf[n][TMAX_HEIGHT-k-1][j-i+1] = *s++;
					k++;
				}
			}
			// next letter
			n++;
		}
		// don't do this again, no need to
		m_tinit = true;
	}
	// preformatted code

	char ans[5];
	ans[0] = 'A' + (rand() % 26);
	ans[1] = 'A' + (rand() % 26);
	ans[2] = 'A' + (rand() % 26);
	ans[3] = 'A' + (rand() % 26);
	ans[4] = '\0';
	int32_t ansLen = 4;
	int32_t ansHash = hash32 ( ans , ansLen );
	sb->safePrintf ( "<center>We suspect you might be a robot!<br>"
			 "<br>"
			 "Please Enter the 4 LARGE letters you see below "
			 "to prove otherwise:\n "
			 "&nbsp; "
			 "<input type=text name=ans size=5>\n"
			 "<input type=hidden name=\"qid\" "
			 "value=\"%"INT32"\">\n" ,
			 m_nextQuestion );
	if(m_answers.getNumSlotsUsed() > 25000) {
		//we're going to lose answers in progress here,
		//hopefully not too many
		log("autoban: clearing anwer table");
		m_answers.clear();
	}

	m_answers.addKey ( m_nextQuestion , ansHash );
	m_nextQuestion++;
	sb->safePrintf ("<pre>\n" );

	// . display letters pre-formatted
	// . loop over starting at top row
	for ( int32_t a = 0 ; a < TMAX_HEIGHT ; a++ ) {
		// loop over each letter
		for ( int32_t i = 0 ; i < ansLen ; i++ ) {
			char c = ans[i] - 'A' ;
			//if ( c < 'A' ) c = 'A';
			//if ( c > 'Z' ) c = 'Z';
			char *s = m_buf[(int)c][a];
			// print his row
			sb->safeMemcpy(s, TMAX_WIDTH);
			//for ( int32_t b = 0 ; b < TMAX_WIDTH ; b++ ) 
			//sb->pushChar(s[b]);

			// print each line -- preformatted
			//sprintf ( p , "%s\n" , s_map[j] );
			//p += gbstrlen ( p );
		}
		// then drop down to next line
		sb->pushChar('\n');
	}

	// preformatted code
	sb->safePrintf ("</pre></center>\n" );

	// if not yippy, done
	if ( ! g_isYippy ) return true;

	// print yippy metric script
	sb->safePrintf(
		      "<script type=\"text/javascript\"> sc_project=3394836;sc_invisible=1;sc_partition=37;sc_security=\"8697821e\";</script>"


//"<script type=\"text/javascript\" src=\"http://www.statcounter.com/counter/counter.js\"></script>"
//"<noscript><div class=\"statcounter\"><a title=\"site stats\" href=\"http://www.statcounter.com/\" target=\"_blank\"><img class=\"statcounter\" src=\"http://c38.statcounter.com/3394836/0/8697821e/1/\" alt=\"site stats\" /></a></div></noscript>"

"<script type=\"text/javascript\">  _qoptions={qacct:\"p-23mw4mzOPpYNI\"};</script><script type=\"text/javascript\" src=\"http://edge.quantserve.com/quant.js\"></script><noscript><img src=\"http://pixel.quantserve.com/pixel/p-23mw4mzOPpYNI.gif\" style=\"display: none;\" border=\"0\" height=\"1\" width=\"1\" alt=\"Quantcast\" /></noscript><script type=\"text/javascript\" src=\"http://edge.quantserve.com/quant.js\"></script><script type=\"text/javascript\">_qacct=\"p-ba3RuZkrv17Fw\";quantserve();</script><noscript><img src=\"http://pixel.quantserve.com/pixel/p-ba3RuZkrv17Fw.gif\" style=\"display: none\" height=\"1\" width=\"1\" alt=\"Quantcast\" /></noscript>");

	/*
	sb->safePrintf("<!-- Index Platform: Pop Under -->\n"
		       "<script type=\"text/javascript\"><!--\n");

	sb->safePrintf("var casaleD=new Date();var casaleR=(casaleD.getTime()%%8673806982)+Math.random();\n");

	sb->safePrintf(
		       "var casaleU=escape(window.location.href);\n"
		       "var casaleHost=' type=\"text/javascript\" src=\"http://as.casalemedia.com/s?s=';\n"
		       "document.write('<scr'+'ipt'+casaleHost+'138069&amp;u=');\n");
	
	sb->safePrintf("document.write(casaleU+'&amp;f=1&amp;id='+casaleR+'\">"
		       "<\\/scr'+'ipt>');\n"
		       "//--></script>\n");
	*/

	// report
	return true;
}


