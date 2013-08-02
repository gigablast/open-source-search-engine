
// INSTRUCTIONS:
// 1. enter any new loans or properties into the arrays below
// 2. enter the bofa interest on bogan and matt's home into the transaction correction table below.
//    Search for "BOFABOGANLOANINTEREST" and "BOFAHOMELOANINTEREST" to find the spot below.
//    Enter in the HOMEPROPTAXES there as well.
// 3. make any corrections between shareholder loan interest and software license interest above that
//    area as well. this controls what part of the money matt received is loan and what is software license
//    interest which is taxable. (see SHARELOAN or MATTSOFTLICINTEREST below)
// 4. download all the new tax forms into the xxxx/ subdir where xxxx is the tax year:
//    f1120.pdf  // corporate
//    f4562.pdf  // depreciation
//    f1040.pdf  // matt's individual
//    f8903.pdf  // domestic production
//    f1139.pdf  // carryback gb
//    f1045.pdf  // carryback matt
// 5. fix the fdf field names. use the 'ditzy map' to generate a map.fdf to show all field names.
// 6. run 'ditzy <gb|matt>] <taxyear>'
// 7. let our accountant do the state income tax forms and penalty forms

// TODO: apply the NOL (net operating loss) to previous years taxes as tax credit...
// aka. carryback
// maybe that's for 1045, and 1139, ...???

// TODO: add prius instead of porsche to depreciating list

#include <errno.h>

#ifndef _GNU_SOURCE 
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>      // for opendir()
#include <dirent.h>         // for opendir()
#include <time.h>           // for time()
#include <ctype.h>
#include <sys/socket.h>  // inet_ntoa()
#include <netinet/in.h>  // inet_ntoa()
#include <arpa/inet.h>   // inet_ntoa()
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// year of the accounting
long  g_accyear = 0;
bool  g_ismatt = false;
bool  g_isgbcorp = false;
float g_propStart = 0.0;
float g_propEnd = 0.0;
float g_propDeprStart = 0.0;
float g_propDeprEnd = 0.0;

#define MINYEAR 2008

// array of transactions
class Trans {
public:
	// ptr to line in statement text file
	char *m_ptr;
	// . E=expense D=deposit C=check
	// . "hold" means to like hold for the tax man
	// . "invoice" is virtual like "hold" is. they both refer to planned
	//   transactions either into or out of our account.
	//char  m_type;
	// date posted (04/08)
	char *m_date;
	// date in seconds since epoch (for sorting)
	long  m_secs;
	// the year
	long  m_year;
	// amount
	float m_amount;
	char *m_srcAcct;
	char *m_dstAcct;
	// desc
	char *m_desc;
	long  m_checkNum;
	long long m_refNum;
	// the category
	char *m_cat;
};

// start of 2009:       end of 2009
// 2971: $686.17     -> $26,908.61 (gb llc)
// 7418; $59.90      -> $610.80
// 1284: $573,128.51 -> $219,234.14
// 1271: $250,194.40 -> $199,942.66
// 6550: $5,878.64   -> $171,979.84
// TOTA: $829947.62  -> $618676.05
// diff = -211276.62

// start of 2008:       end of 2008
// 2971: 0.0         -> 686.17
// 7418; 281.65      -> 59.50
// 1284: 0.0         -> 573128.51
// 1271: 0.0         -> 250194.40
// 6550: 168742.00   -> 5878.64
// TOTA: ...
// these don't add up, use cash assets from f1120 at end of 2007
// diff = 

class Prop {
public:
	char *m_desc;
	char *m_datePlacedInService;
	float m_busUse;
	float m_value;
	float m_basis;   // depreciate this
	long  m_schedule;   // 5 or 7 years depreciation? or 39?
	long  m_yearSold; // year when prop was sold
	char  m_ismatt; // matt or gb corp owns?
};


// 5yr depreciation schedule: (200DB/HY)
//  20.00%
//  32.00%
//  19.20%
//  11.52%
//  11.52%
//   5.76%

// TODO: make sure that the amounts we should have
// depreciated were properly reported. if some were
// underreported we can claim it now.
Prop g_prop[] = {

	// . matt house, counts as asset. but not
	//   depreciable unfortunately
	{"MATT HOME"  ,"01/10/2008",  0,1150000,0,0,0,1},

	// . matt 2004.
	// . no, these were moved into gb. was matt compensated?
	{"COMPUTER HW","01/31/2004",100,848   ,0     ,5,0,0},
	{"COMPUTER HW","02/28/2004",100,1721  ,0     ,5,0,0},
	{"COMPUTER HW","04/30/2004",100,1945  ,0     ,5,0,0},
	{"COMPUTER HW","03/31/2004",100,15578 ,0     ,5,0,0},
	{"COMPUTER HW","05/31/2004",100,12929 ,0     ,5,0,0},
	{"COMPUTER HW","06/30/2004",100,5557  ,0     ,5,0,0},
	{"COMPUTER HW","07/31/2004",100,42864 ,0     ,5,0,0},
	{"COMPUTER HW","08/31/2004",100,6841  ,0     ,5,0,0},
	{"COMPUTER HW","08/31/2004",100,60000 ,23141 ,5,0,0},

	// gb corp 2004
	{"OFFICE FURN","09/07/2004",100,453   ,226   ,7,0,0},
	{"COMPUTER HW","09/30/2004",100,7579  ,3789  ,5,0,0},
	{"COMPUTER HW","10/31/2004",100,5182  ,2591  ,5,0,0},
	{"COMPUTER HW","11/30/2004",100,35921 ,17960 ,5,0,0},
	{"SOFTWARE"   ,"12/01/2004",100,1132  ,566   ,3,0,0},
	{"COMPUTER HW","12/31/2004",100,1846  ,923   ,5,0,0},

	// gb corp 2005
	{"2005 AUTO"  ,"05/25/2005", 75,87840 ,65880 ,5,2010,0}, // mark as sold in 2010
	{"COMPUTER HW","07/01/2005",100,68292 ,68292 ,5,0,0},
	{"COMPUTER HW","09/01/2005",100,132169,132169,5,0,0},
	{"COMPUTER HW","09/30/2005",100,5144  ,5144  ,5,0,0},
	{"COMPUTER HW","09/30/2005",100,2050  ,2050  ,5,0,0},
	{"COMPUTER HW","09/30/2005",100,2688  ,2688  ,5,0,0},
	{"COMPUTER HW","10/31/2005",100,132169,132169,5,0,0},
	{"COMPUTER HW","11/30/2005",100,118184,118184,5,0,0},

	// gb corp 2006
	{"COMPUTER HW","01/15/2006",100,87840 ,59731 ,5,0,0},
	{"OFFICE FURN","06/01/2006",100,5515  ,0     ,7,0,0},
	{"SOFTWARE"   ,"06/01/2006",100,1869  ,1869  ,3,0,0},
	{"COMPUTER HW","06/01/2006",100,254237,179861,5,0,0},

	// gb corp 2007
	{"COMPUTER HW","03/31/2007",100,347861,222861,5,0,0},
	// fred reported these on the 2007 form 4562 but did not
	// list them on any worksheet
	{"COMPUTER HW","05/01/2007",100, 20474, 20474,5,0,0},
	// this one too, it's 7 years so assume furniture
	{"OFFICE FURN","05/01/2007",100, 10479, 10479,7,0,0},
	
	// . matt 2009
	// . opened gigablast llc acct it seems on about 12/11/07
	// . first payment went out on 1/10 for commerical loans debit
	// . but when exactly did we buy the bldg?
	// . TODO: need to check records in safe
	{"BOGAN BLDG" ,"01/10/2008",100,572500,572500,39,0,1},
	{"BOGAN LAND" ,"01/10/2008",100,227600,0     ,0 ,0,1},

	// . mdw 5/4/2013. set date from 2011 to 2009. may cause discrepancies.
	{"2008 AUTO"  ,"01/20/2009", 90,29500 ,26550 ,5,0,0}, // mark as bought in 2011, prius

	// . mary expensed this in 2009 on form 4562
	// . mdw 5/4/2013. added. may cause discrepancies
	{"OFFICE FURN","05/01/2009",100, 11613, 0,7,0,0},
	{"COMPUTER HW","05/01/2009",100, 24035, 0,5,0,0},

	// . gb corp 2009
	// . paid 303116.91 on 07/15/09 for unit 2 from joni neutra
	//   but the value was stated as 446200 on the prop tax
	// . signed contract with chana bendov on 05/04/2007 for 430k for prop+interest
	// . i guess we can't depreciate chana's bldg until we actually have
	//   the title in our name, so take this off!
	// . mdw 5/4/2013. took this out. may cause discrepancies
	//{"MADRID BLDG1","05/04/2007",100,430000,430000,39,0,0},
	{"MADRID BLDG2","07/15/2009",100,303116.91,303116.91,39,0,0}

};

class Loan {
public:
	char *m_desc;
	char *m_datePlacedInService;
	// negative means liability, positive means asset
	float m_value;
	char  m_ismatt;
	char *m_cat;
};

// this data is valid as of start of 2009
Loan g_loans[] = {
	{"madrid bldg1 chana loan","05/04/2007",-430000,0,"CHANALOAN"},
	// from 2008 return, shareholder loan from gb corp to matt with his software as collateral
	{"shareholder loan","01/01/2007",-466335,1,"SHARELOAN"},
	{"shareholder loan","01/01/2007", 466335,0,"SHARELOAN"},
	// . gb corp loaned gb llc money to purchase the bogan bldg before start of 2009
	// . also around 27k in 2009 was overpaid by gb corp to gb llc by accident (same in 2010)
	// . and 1k was put into matt wells llc by gb corp to prevent bank acct fees
	{"llc loan","01/01/2008",-184156,1,"LLCLOAN"},
	{"llc loan","01/01/2008", 184156,0,"LLCLOAN"},
	// gb llc loan from bofa for bogan bldg
	{"bogan bofa loan","01/10/2008",-800100,1,"BOFABOGANLOAN"},
	// . gb bought off part of matt's home loan in 2009 so matt's interest payments went to it
	// . but this is in our 2009 transaction list so no need to insert value here
	// . but we do need the loan account shell...
	{"matt gb home loan","01/01/2009",0,1,"GBHOMELOAN"},
	{"matt gb home loan","01/01/2009",0,0,"GBHOMELOAN"},
	// . at the beginning of 2009 what did we owe in the bofa home loan?
	// . when gb bought part of it, how do we handle that? just add a transaction from
	//   gb corp to matt, then use that to payoff loan principal.
	{"bofa home loan","01/10/2008",-1000000,1,"BOFAHOMELOAN"},
	// gb owes matt money for using matt's hardware (160k in 2004) and software license.
	// it is basically paying just interest to matt right now. this was not included
	// in the tables i gave mary for 2009 and 2010 tax returns, but the interest payments
	// were, because i wasn't sure if it was necessary to include it on the tax returns
	// since it was not income for either part since only interest payments were
	// made under the MATTSOFTLICINTEREST label.
	{"matt software loan","01/10/2005",+0,1,"MATTSOFTLIC"},
	{"matt software loan","01/10/2005",-0,0,"MATTSOFTLIC"}
	
};


// payments made in [year1,year2]
float getPayments ( Trans *t , long nt , long year1, long year2 , bool ismatt , char *cat ) {
	float paid = 0.0;
	for ( long j = 0 ; j < nt ; j++ ) {
		Trans *s = &t[j];
		// skip if beyond our year
		if ( s->m_year > year2 ) continue;
		if ( s->m_year < year1 ) continue;
		// skip if not our loan
		if ( strcmp(s->m_cat,cat) != 0 ) continue;
		// we matched, record it
		paid += s->m_amount;
	}
	return paid;
}

float getLoanPayments ( Trans *t , long nt , long year1 , long year2 , bool ismatt , Loan *x ) {
	return getPayments ( t , nt , year1, year2 , ismatt , x->m_cat );
}

float getInterestPayments ( Trans *t , long nt , long year1, long year2 , bool ismatt , Loan *x ) {
	char cat[128];
	sprintf(cat,"%sINTEREST",x->m_cat);
	float paid = getPayments ( t , nt , year1, year2 , ismatt , cat );
	// why is this negative? we should be making money
	return paid;
}

float getLoanAmt ( char *cat , Trans *t , long nt , long year , bool ismatt ) {
	// get loan ptr
	long n = sizeof(g_loans)/sizeof(Loan);
	Loan *x = NULL;
	long i; for ( i = 0 ; i < n ; i++ ) {
		x = &g_loans[i];
		if ( ismatt != x->m_ismatt ) continue;
		if ( strcmp(cat,x->m_cat) ) continue;
		break;
	}
	// loan must be there
	if ( i >= n ) { char *xx=NULL;*xx=0; }

	// software loan is +500k per year
	if ( strcmp(x->m_cat,"MATTSOFTLIC") == 0 ) {
		// 500k per year for 2005 through end of 2010, 
		// s0 2.5M is owed at end of 2010
		long y1 = year;
		if ( y1 > 2010 ) y1 = 2010;
		float amt = 500000 * (y1-2005);
		// then 100k per year for 2011 and onwards
		long y2 = year - 2010;
		if ( y2 < 0 ) y2 = 0;
		amt += 100000 * y2;
		// gb owes this to matt
		if ( ! ismatt ) amt *= -1.0;
		return amt;
	}

	// get start of MINYEAR amount
	float amt = x->m_value;

	float paid = getLoanPayments ( t , nt , 2000, year , ismatt , x );
	// turn loss of cash into positive asset
	paid = -1 * paid;
	// enahnce with transactions
	amt += paid;
	return amt;
}

//float getLoanInterest ( char *cat , Trans *t , long nt , long year , bool ismatt ) {
//	float amt = getLoanAmt ( cat , t , nt , year , ismatt );
//	// now the interest at 6% for year "year"
//	return amt * .06;
//}

// . if "from" then get loans from us to someone else
float getLoans2 ( Trans *t , long nt , long year , bool ismatt , bool from ) {
	long n = sizeof(g_loans)/sizeof(Loan);
	Loan *x = NULL;
	float total = 0.0;
	long i; for ( i = 0 ; i < n ; i++ ) {
		x = &g_loans[i];
		if ( ismatt != x->m_ismatt ) continue;
		// init to MINYEAR value
		//float amt = x->m_value;
		// this is dynamic man!
		float net = getLoanAmt ( x->m_cat, t, nt , year , ismatt );
		// add more loans of this type then
		//float payments = getLoanPayments (t,nt,MINYEAR,year,ismatt,x);
		// flip sign since the loss of cash is now a loan asset
		//payments *= -1;
		// compute the next
		//float net = amt + payments;

		// skip if not from us (i.e. an asset for us)
		if ( from && net < 0.0        ) continue;
		// gbhomeloan starts at 0.0, so we miss it!
		else if ( ! from && net > 0.0 ) continue;

		// add that in
		total += net;

	}
	return total;
}

void printLoans ( Trans *t , long nt , long year , bool ismatt ) {

	// header
	fprintf(stdout,"LOAN DESC                 DATE         ACCOUNT        ");
	// each year
	for ( long y = MINYEAR ; y <= year+1 ; y++ )
		fprintf(stdout, "  01/%li balance ",y);
	fprintf(stdout,"\n");

	// get loan ptr
	long n = sizeof(g_loans)/sizeof(Loan);
	Loan *x = NULL;
	for ( long i = 0 ; i < n ; i++ ) {
		x = &g_loans[i];
		if ( ismatt != x->m_ismatt ) continue;
		// print name
		fprintf(stdout,
			"%23s |"
			" %s |"
			" %13s |"
			,
			x->m_desc,
			x->m_datePlacedInService,
			x->m_cat);
		// print out for each year
		for ( long y = MINYEAR ; y<= year+1 ; y++ ) {
			// total for all of prev year
			float amt = getLoanAmt(x->m_cat,t,nt,y-1,ismatt);
			fprintf(stdout, " % 16.02f ", amt);
		}
		fprintf(stdout,"\n");
	}
	/*
	// print software license loan
	fprintf(stdout,
		"%23s |"
		" %s |"
		" %13s |"
		,
		"matt softw loan", // x->m_desc
		"01/01/2002", // x->m_datePlacedInService,
		"SOFTLOAN" ); // x->m_cat);
	// then the software license debt
	// print out for each year
	for ( long y = MINYEAR ; y<= year+1 ; y++ ) {
		// total for all of prev year
		// it was 2,500,000 at start of 2010, so if its
		// 500,000 per year then it started at the start of
		// 2005
		float amt = (y - 2006)*500000.00;
		// gb owes to matt
		if ( !ismatt ) amt *= -1.0;
		fprintf(stdout, " % 16.02f ", amt);
	}
	fprintf(stdout,"\n");
	*/
	fprintf(stdout,"\n");
	fprintf(stdout,"\n");
}

void printLoanInterest ( Trans *t , long nt , long year , bool ismatt ) {

	// header
	fprintf(stdout,"LOAN DESC                 DATE         ACCOUNT        ");
	// each year
	for ( long y = MINYEAR ; y <= year ; y++ )
		fprintf(stdout, " %li interest ",y);
	fprintf(stdout,"\n");

	// get loan ptr
	long n = sizeof(g_loans)/sizeof(Loan);
	Loan *x = NULL;
	for ( long i = 0 ; i < n ; i++ ) {
		x = &g_loans[i];
		if ( ismatt != x->m_ismatt ) continue;
		// print name
		fprintf(stdout,
			"%23s |"
			" %s |"
			" %13s |"
			,
			x->m_desc,
			x->m_datePlacedInService,
			x->m_cat);
		// print out for each year
		for ( long y = MINYEAR ; y<= year ; y++ ) {
			// total for all of prev year
			float amt = getInterestPayments(t,nt,y,y,ismatt,x);
			fprintf(stdout, " % 13.02f ", amt);
		}
		fprintf(stdout,"\n");
	}
	fprintf(stdout,"\n");
	fprintf(stdout,"\n");
}

float getDepreciation ( Prop *p , long year ) {

	// year placed in service
	long propyear = atoi(p->m_datePlacedInService+6);
	// age in years
	long age = year - propyear;

	// if not born yet, print spaces
	if ( age < 0 ) return 0.0;

	// was it sold?
	if ( year > p->m_yearSold && p->m_yearSold >= 2000 ) 
		return 0.0;

	// compute rate for this year
	float rate = 0.0;
	// for computer hardware, software, autos
	if ( p->m_schedule == 5 ) {
		// special for auto, fix it
		if ( ! strcmp(p->m_desc,"2005 AUTO") ) return 2138.00;
		if ( age == 0 ) rate = .20;
		if ( age == 1 ) rate = .32;
		if ( age == 2 ) rate = .192;
		if ( age == 3 ) rate = .1152;
		if ( age == 4 ) rate = .1152;
		if ( age == 5 ) rate = .0576;
		if ( age  > 5 ) rate = .0;
	}
	// for furniture
	else if ( p->m_schedule == 7 ) {
		// core until we fix the 7 year schedule
		if ( age == 0 ) rate = .143;
		if ( age == 1 ) rate = .245;
		if ( age == 2 ) rate = .175;
		if ( age == 3 ) rate = .125;
		if ( age == 4 ) rate = .089;
		if ( age == 5 ) rate = .089;
		if ( age == 6 ) rate = .089;
		if ( age == 7 ) rate = .045;
		if ( age  > 7 ) rate = .0;
	}
	else if ( p->m_schedule == 3 ) {
		if ( age == 0 ) rate = .333;
		if ( age == 1 ) rate = .333;
		if ( age == 2 ) rate = .333;
		if ( age >  2 ) rate = 0.0;
	}
	// a straight 39 year depreciation for bldgs
	else if ( p->m_schedule == 39 ) {
		rate = 1.0 / 39.0 ;
	}
	// 0 means undepreciable, BOGAN LAND
	else if ( p->m_schedule == 0 ) {
		rate = 0.0;
	}
	else {
		char *xx=NULL;*xx=0; 
	}
	// otherwise get basis for depreciation
	float basis = p->m_basis;
	// the deduction
	float lost = basis * rate;
	return lost;
}

char  gbuf[500000];
char *gbufPtr = gbuf;

// according to the 2008 worksheet:
// total deprec for 2004-2007 is 541442, 
// 2003- 27770  (according to 2004 worksheet)
// 2004  27770  (on f1120, does not include expense part)
// 2004  111445 (based on 2005 worksheet)
// 2005  261707 (based on 2005 worksheet)
// 2006  174148  (based on 2007 worksheet prior column)
// 2007  218720
// 2008  183472
// sec 179 expense deduction was 355492
// then for 09 and 10 we depreciated 133834 and 83821 respectively
// which leaves us with 217655 left over for some reason... for 2011
// . $317564.00 was claimed on 2006 f1120 in depreciation
long printDepreciationTable ( FILE *fd, bool ismatt , Trans *t , long nt ) {
	long n = sizeof(g_prop)/sizeof(Prop);

	float totalValue = 0.0;
	float totalBasis = 0.0;
	float totalYear[30];
	for ( long k = 0 ; k < 30 ; k++ ) totalYear[k] = 0.0;


	//long startyear = 2008;
	// this might throw some shit off, but we need the total 
	// accumulate depreciation! we started getting stuff in 2004.
	long startyear = 2004;

	// print headers
	fprintf(fd," description       inservice    use%%   value      basis      db   done ");
	// each year
	for ( long y = startyear ; y <= g_accyear ; y++ ) fprintf(fd,"    %li ",y);
	fprintf(fd,"\n");

	// reset buffer for holding descriptions
	gbufPtr = gbuf;

	// scan boxes
	long i; for ( i = 0 ; i < n ; i++ ) {
		// shortcut
		Prop *p = &g_prop[i];
		// must match
		if ( p->m_ismatt != ismatt ) continue;
		// year placed in service
		long year = atoi(p->m_datePlacedInService+6);
		// skip if after request year
		if ( year > g_accyear ) continue;
		// special
		char sbuf[5];
		if ( p->m_yearSold == 0 ) sprintf(sbuf,"----");
		else                      sprintf(sbuf,"%li",p->m_yearSold);
		// add up
		totalValue += p->m_value;
		totalBasis += p->m_basis;
		// print name
		fprintf(fd,
			"%16s | %s | %3.0f%% | $%7.0f | "
			"$%7.0f | %02li | %s ",
			p->m_desc,
			p->m_datePlacedInService,
			p->m_busUse,
			p->m_value,
			p->m_basis,
			p->m_schedule,
			sbuf );
		// tally thesw
		// use basis not actual value. because the non-basis portion was expensed!
		// plus, otherwise we can't depreciate all the way down to 0.
		if ( year < g_accyear    ) g_propStart += p->m_basis;
		if ( year < g_accyear +1 ) g_propEnd   += p->m_basis;
		//if ( year < g_accyear    ) g_propStart += p->m_value;
		//if ( year < g_accyear +1 ) g_propEnd   += p->m_value;
		// sanityy
		if ( year < 2000 || year > 2050 ) { char *xx=NULL;*xx=0; }
		// now each of the years
		for ( long y = startyear ; y <= g_accyear ; y++ ) {
			// get depr for year y
			float lost = getDepreciation ( p , y );
			// skip printing if lost is 0
			if ( lost == 0.0 ) {
				fprintf(fd,"|        ");
				continue;
			}
			// print that then
			fprintf(fd,"| %6.0f ",lost);
			// add up
			totalYear[y-startyear] += lost;
			// insert transactions to show depreciation
			t[nt].m_date = "12/31";
			t[nt].m_year = y;
			long len = sprintf (gbufPtr,"%s %s depreciation",
					    p->m_datePlacedInService,p->m_desc) + 1;
			t[nt].m_desc = gbufPtr;
			gbufPtr += len+1;
			t[nt].m_amount = -1 * lost;
			t[nt].m_cat = "DEPRECIATION";
			nt++;

			// before year
			if ( y < g_accyear   ) g_propDeprStart -= lost;
			// right up to before next year
			if ( y < g_accyear+1 ) g_propDeprEnd -= lost;
		}
		fprintf(fd,"\n");
	}

	// print total
	fprintf(fd,"                                       $%7.0f   $%7.0f             ",
		totalValue,totalBasis);
	// each year
	for ( long y = startyear ; y <= g_accyear ; y++ ) 
		fprintf(fd,"  %6.0f ",totalYear[y-startyear]);
	fprintf(fd,"\n");

	return nt;
}

Prop *getProp ( char *propname ) {
	long n = sizeof(g_prop)/sizeof(Prop);
	// scan boxes
	long i; for ( i = 0 ; i < n ; i++ ) {
		// shortcut
		Prop *p = &g_prop[i];
		// must match
		if ( strcmp(p->m_desc,propname) ) continue;
		// got it
		return p;
	}
	char *xx=NULL;*xx=0;
	return NULL;
}


void runScript ( char *script , long accyear , char *pdf ) ;
void filloutForm4562 ( bool ismatt ) ;

char *g_sp = NULL;
char  g_script [100000];
long  g_form;

/*
class Box {
public:
	char *m_field;
	long  m_page;
	float m_xpos;
	float m_ypos;
	// the x coordinate of the vertical line that divides the pennies column
	float m_decimalLine;
	char *m_fdfname;
};

// map each tax form field name to a page number and an x and y position for entering
// in the value using pdfedit.
const Box g_boxes[] = {
	{ "name"        , 1,  6.90, 24.70,0,NULL},
	{ "street"      , 1,  6.90, 23.84,0,NULL},
	{ "city"        , 1,  6.90, 23.00,0,NULL},
	{ "ein"         , 1, 16.58, 24.70,0,NULL},
	{ "incdate"     , 1, 16.58, 23.84,0,NULL},
	{ "totalassets" , 1, 16.58, 23.00,19.52,NULL},
	{ "grosssales"  , 1, 05.40, 22.00,7.58,NULL},

	{ "1c"          , 1, 17.70, 22.00,19.52,NULL},
	{ "5interest"   , 1, 17.70, 20.35,19.52,NULL},
	{"11totalincome", 1, 17.70, 17.80,19.52,NULL},
	{"12officercomp", 1, 17.70, 17.35,19.52,NULL},
	{"13wages"      , 1, 17.70, 17.00,19.52,NULL},
	{"14repairs"    , 1, 17.70, 16.50,19.52,NULL},
	{"16rents"      , 1, 17.70, 15.70,19.52,NULL},
	{"17taxes"      , 1, 17.70, 15.25,19.52,NULL},
	{"18interest"   , 1, 17.70, 14.80,19.52,NULL},
	{"20depreciate" , 1, 17.70, 14.00,19.52,NULL},
	{"22advertise"  , 1, 17.70, 13.10,19.52,NULL},
	{"24employeeben", 1, 17.70, 12.25,19.52,NULL},
	{"25domestic"   , 1, 17.70, 11.83,19.52,NULL},
	{"26otherdeduct", 1, 17.70, 11.42,19.52,NULL},
	{"27totaldeduct", 1, 17.70, 11.00,19.52,NULL},
	{"28taxable"    , 1, 17.70, 10.60,19.52,NULL},
	{"30taxable"    , 1, 17.70, 09.30,19.52,NULL},
	{"31totaltax"   , 1, 17.70, 08.90,19.52,NULL},
	{"32aoverpay"   , 1, 09.28, 08.46,11.18,NULL},
	{"32hoverpay"   , 1, 17.70, 06.38,19.52,NULL},
	{"35overpay"    , 1, 17.70, 05.11,19.52,NULL},
	{"34owed"       , 1, 17.70, 05.50,19.52,NULL},
	{"ceotitle"     , 1, 12.17, 03.52,0,NULL},
	// f1120 - page 2
	{"officer1"      , 2, 02.11, 05.11,0,NULL},
	{"officer1ssn"   , 2, 07.93, 05.11,0,NULL},
	{"officer1percnt", 2, 10.97, 05.11,0,NULL},
	{"officer1common", 2, 12.98, 05.11,0,NULL},
	{"officer1comp"  , 2, 16.75, 05.11,0,NULL},
	{"2totaloffcomp" , 2, 16.75, 03.00,0,NULL},
	{"4totaloffcomp" , 2, 16.75, 02.15,0,NULL},
	// f1120 - page 3 - schedule j
	{"j2incometax"   , 3, 17.70, 25.00,19.52,NULL},
	{"j4incometax"   , 3, 17.70, 24.13,19.52,NULL},
	{"j7incometax"   , 3, 17.70, 21.20,19.52,NULL},
	{"j10incometax"  , 3, 17.70, 19.54,19.52,NULL},
	// f1120 - page 3 - schedule k
	{"k1acash"       , 3, 06.42, 18.73,0,NULL},
	{"k2abizcode"    , 3, 06.63, 17.84,0,NULL},
	{"k2bactivity"   , 3, 06.63, 17.39,0,NULL},
	{"k2cservice"    , 3, 06.63, 16.96,0,NULL},
	{"k3no"          , 3, 19.72, 16.57,0,NULL},
	{"k4ano"         , 3, 19.72, 13.96,0,NULL},
	{"k4byes"        , 3, 18.90, 13.12,0,NULL},
	{"k5ano"         , 3, 19.72, 12.06,0,NULL},
	// f1120 - page 4 - schedule k
	{"k5bno"         , 4, 19.72, 25.04,0,NULL},
	{"k6no"          , 4, 19.72, 17.95,0,NULL},
	{"k7no"          , 4, 19.72, 16.12,0,NULL},
	{"k10numholders" , 4, 13.12, 12.76,0,NULL},
	{"k13no"         , 4, 19.72, 10.20,0,NULL},
	// f1120 - page 5 - schedule L
	{"cashstart"     , 5, 11.88, 25.00 ,0,NULL},
	{"cashend"       , 5, 17.92, 25.00 ,0,NULL},
	{"loanfromstart" , 5, 11.88, 22.00 ,0,NULL},
	{"loanfromend"   , 5, 17.92, 22.00 ,0,NULL},
	{"propstart"     , 5, 08.90, 20.80 ,0,NULL},
	{"propend"       , 5, 15.00, 20.80 ,0,NULL},
	{"propdeprstart" , 5, 08.90, 20.35 ,0,NULL},
	{"propstart2"    , 5, 11.88, 20.35 ,0,NULL},
	{"propdeprend"   , 5, 15.00, 20.35 ,0,NULL},
	{"propend2"      , 5, 17.92, 20.35 ,0,NULL},
	{"assetsstart"   , 5, 11.88, 17.39 ,0,NULL},
	{"assetsend"     , 5, 17.92, 17.39 ,0,NULL},
	// liabilities
	{"commonstock1"  , 5, 08.90, 13.58 ,0,NULL},
	{"commonstock2"  , 5, 11.88, 13.58 ,0,NULL},
	{"commonstock3"  , 5, 15.00, 13.58 ,0,NULL},
	{"commonstock4"  , 5, 17.92, 13.58 ,0,NULL},
	{"investmentstart",5, 11.88, 13.15 ,0,NULL},
	{"investmentend" , 5, 17.92, 13.15 ,0,NULL},
	{"liablestart"   , 5, 11.88, 11.04 ,0,NULL},
	{"liableend"     , 5, 17.92, 11.04 ,0,NULL},
	// f1120 - page 5 - schedule m-1
	{"netincomebks"  , 5, 08.90, 09.77, 0,NULL},
	{"taxperbooks"   , 5, 08.90, 09.31, 0,NULL},
	{"m1n6sum"       , 5, 08.90, 04.69, 0,NULL},
	{"m1n8dom"       , 5, 17.92, 05.53, 0,NULL},
	{"m1n9sum"       , 5, 17.92, 05.11, 0,NULL},
	{"m1n10sum"      , 5, 17.92, 04.65, 0,NULL},
	// f1120 - page 5 - schedule m-2
	{"m2n2netincome" , 5, 08.90, 03.38, 0,NULL},
	{"m2n4sum"       , 5, 08.90, 01.72, 0,NULL},
	{"m2n8sum"       , 5, 17.92, 01.72, 0,NULL},


	//
	// form 4562
	//
	{"4562name"     , 1, 02.15, 24.23, 0, "1"},
	{"4562activity" , 1, 09.52, 24.23, 0, "2"},
	{"4562ein"      , 1, 17.32, 24.23, 0, "3"},
	//{"4562box1"     , 1, , , 0,NULL},
	{"4562box2"     , 1, 18.00, 22.47, 0,NULL},
	{"4562box5"     , 1, 18.00, 20.77, 0,NULL},
	{"4562box6arow1", 1, 02.04, 19.89, 0,NULL},
	{"4562box6brow1", 1, 10.37, 19.89, 0,NULL},
	{"4562box6crow1", 1, 14.11, 19.89, 0,NULL},
	{"4562box8"     , 1, 18.00, 18.66, 0,NULL},
	{"4562box9"     , 1, 18.00, 18.20, 0,NULL},
	{"4562box11"    , 1, 18.00, 17.39, 0,NULL},
	{"4562box12"    , 1, 18.00, 16.93, 0,NULL},
	{"4562box17"    , 1, 18.00, 12.73, 0,NULL},

	{"4562box19ac" , 1, 07.12, 10.15, 0,NULL},
	{"4562box19ad" , 1, 10.05, 10.15, 0,NULL},
	{"4562box19ae" , 1, 12.00, 10.15, 0,NULL},
	{"4562box19af" , 1, 14.20, 10.15, 0,NULL},
	{"4562box19ag" , 1, 17.40, 10.15, 0,NULL},

	{"4562box19bc" , 1, 07.12, 09.73, 0,NULL},
	{"4562box19bd" , 1, 10.05, 09.73, 0,NULL},
	{"4562box19be" , 1, 12.00, 09.73, 0,NULL},
	{"4562box19bf" , 1, 14.20, 09.73, 0,NULL},
	{"4562box19bg" , 1, 17.40, 09.73, 0,NULL},

	{"4562box19cc" , 1, 07.12, 09.31, 0,NULL},
	{"4562box19cd" , 1, 10.05, 09.31, 0,NULL},
	{"4562box19ce" , 1, 12.00, 09.31, 0,NULL},
	{"4562box19cf" , 1, 14.20, 09.31, 0,NULL},
	{"4562box19cg" , 1, 17.40, 09.31, 0,NULL},

	{"4562box19ib" , 1, 05.15, 05.92, 0,NULL},
	{"4562box19ic" , 1, 07.12, 05.92, 0,NULL},
	{"4562box19id" , 1, 10.05, 05.92, 0,NULL},
	{"4562box19ie" , 1, 12.00, 05.92, 0,NULL},
	{"4562box19if" , 1, 14.20, 05.92, 0,NULL},
	{"4562box19ig" , 1, 17.40, 05.92, 0,NULL},

	{"4562box21"      , 1, 18.00, 03.42, 0,NULL},
	{"4562box22total" , 1, 18.00, 02.64, 0,NULL},
	// page 2
	{"4562box24ayes" , 2, 10.93, 23.77, 0,NULL},
	{"4562box24bno"  , 2, 19.29, 23.77, 0,NULL},
	{"4562box26col1" , 2, 01.51, 20.74, 0,NULL},
	{"4562box26col2" , 2, 04.33, 20.74, 0,NULL},
	{"4562box26col3" , 2, 06.35, 20.74, 0,NULL},
	{"4562box26col4" , 2, 07.93, 20.74, 0,NULL},
	{"4562box26col5" , 2, 10.54, 20.74, 0,NULL},
	{"4562box26col6" , 2, 13.12, 20.74, 0,NULL},
	{"4562box26col7" , 2, 14.85, 20.74, 0,NULL},
	{"4562box26col8" , 2, 16.86, 20.74, 0,NULL},
	{"4562box26col9" , 2, 18.80, 20.74, 0,NULL},
	{"4562box28sum"  , 2, 16.72 ,17.84, 0,NULL}
};

long g_currentPage = 1;
//bool g_firstTime = true;
void addText ( char *field , char *value ) {
	// sanity check on the accounting year
	if ( g_accyear == 0 ) { char *xx=NULL;*xx=0; }

	long nb = sizeof(g_boxes)/sizeof(Box);
	// scan boxes
	long i; for ( i = 0 ; i < nb ; i++ ) 
		if ( ! strcmp ( g_boxes[i].m_field , field ) ) break;
	// not found?
	if ( i >= nb ) {
		fprintf(stdout,"field \"%s\" not found",field);
		exit(-1);
	}

	// get the page and x and y pos
	long  page = g_boxes[i].m_page;
	float x = g_boxes[i].m_xpos;
	float y = g_boxes[i].m_ypos;

	float decimalLine = g_boxes[i].m_decimalLine;

	// convert from cm to pixel units i guess
	// 100px->3.527cm
	if ( x <= 30.0 ) x = x/3.527 * 100.0;
	if ( y <= 30.0 ) y = y/3.527 * 100.0;

	if ( decimalLine > 0.0 ) {
		// skip into the box a little
		decimalLine += .20;
		// then convert into pixels
		decimalLine = decimalLine/3.527 * 100.0;
	}

	// for tax year 2009 the first page is a haiti relief page so skip that
	if ( g_accyear == 2009 && g_form==1120 ) p++;

	bool skippedPage = (g_currentPage != p);

	// skip to the right page
	for ( ; g_currentPage < p ; g_currentPage++ ) 
		g_sp += sprintf ( g_sp , "PageSpace.nextPage()\n" );
	for ( ; g_currentPage > p ; g_currentPage-- ) 
		g_sp += sprintf ( g_sp , "PageSpace.prevPage()\n" );

	if ( g_sp == g_script || skippedPage ) 
		g_sp += sprintf ( g_sp ,
				  "thepage=document.getPage(%li);\n"
				  "var fname = getEditText( \"fontface\" );\n"
				  "var fid=thepage.getFontId( fname );\n"
				  "if (fid.isEmpty()) {\n"
				  " thepage.addSystemType1Font( fname );\n"
				  " fid = thepage.getFontId( fname );\n"
				  "}\n"
				  "var fs=getNumber( \"fontsize\" );\n"
				  "var ctm = getDetransformationMatrix( thepage );\n",
				  g_currentPage
				  );

	//g_firstTime = false;

	// for default, just use str1
	char *str1 = value;
	char *str2 = NULL;

	// . break prices up into two strings.
	// . one is the base and the other the decimal
	// . because on some fields we have to put the pennies in the pennies column
	if ( decimalLine > 0.0 ) {
		// length
		long vlen = strlen(value);
		// get decimal. must be there.
		if ( value[vlen-3] != '.' ) { char *xx=NULL;*xx=0; }
		// fix it
		str1[vlen-3] = '\0';
		// point to pennies part
		str2 = value + vlen - 2;
	}

	// print the cmd
	g_sp += sprintf ( g_sp , 
			  //"drawLine(167,431,412,535,true,609,651)\n"
			  "operatorAddTextLine(\"%s\",%f,%f,fid,fs,createOperator_transformationMatrix( ctm ), getColor(\"fg\"));\n"
			  //"go();\n"
			  //"addAnnotation(thepage,100,100,50,50);\n"
			  //"var q = createCompositeOperator(\"q\",\"Q\");\n"
			  //"var BT=createCompositeOperator(\"BT\",\"ET\");\n"
			  //"q.pushBack ( BT,q);\n"
			  //"putfont(BT,\"Helvetica\",12);\n"
			  //"puttextrelpos(BT,10,10);\n"
			  //"puttext(BT,\"hey you %s\");\n"
			  //"putendtext(BT);\n"
			  //"putendq(q);\n"
			  //"addText ( \"%s\" , %li, %li )\n"
			  ,
			  //g_boxes[i].m_page,
			  str1 ,
			  x ,
			  y );

	// print pennies? return now if not
	if ( decimalLine == 0.0 ) return;

	// print the pennies into the pennies column
	g_sp += sprintf ( g_sp , 
			  "operatorAddTextLine(\"%s\",%f,%f,fid,fs,createOperator_"
			  "transformationMatrix( ctm ), getColor(\"fg\"));\n"
			  ,
			  str2,
			  decimalLine ,
			  y );
}
*/


// letter is c for checkbox, f for text field	
void addText ( long page , char *field, char *value ) {

	// make an fdf
	if ( g_sp == g_script ) 
		// fdf header
		g_sp += sprintf(g_sp,
				"%%FDF-1.2\n"
				"1 0 obj<</FDF<< /Fields[\n" );

	// for making fdf file
	char full[200];
	sprintf ( full, "topmostSubform[0].Page%li[0].%s",page,field);

	// print form field value
	g_sp += sprintf(g_sp,"<</T(%s)/V(%s)>>\n",full,value );

	return;
}

void addTextSimple  ( long page , long fnum , char *value ) {
	char field[100];
	sprintf(field,"f%li_%li_0_[0]",page,fnum);
	addText ( page , field , value );
}

// add without prepending the topmostSubForm crap
void addCheck0 ( char *field , char *value ) {
	// make an fdf
	if ( g_sp == g_script ) 
		// fdf header
		g_sp += sprintf(g_sp,
				"%%FDF-1.2\n"
				"1 0 obj<</FDF<< /Fields[\n" );
	// print form field value
	g_sp += sprintf(g_sp,"<</T(%s)/V(%s)>>\n",field,value );
	return;
}

// offset is 0 or 1 usually
void addCheck ( long page , char *field , char *value ) {

	// for making fdf file
	char full[200];
	sprintf ( full, "topmostSubform[0].Page%li[0].%s",page,field);
	addCheck0 ( full , value );
}

void addPrice ( long page, char *field , float price ) {
	// convert to string
	char buf[100];
	sprintf ( buf , "%.02f", price );
	addText ( page , field , buf );
}

void addPriceSimple  ( long page , long fnum , float price ) {
	char field[100];
	sprintf(field,"f%li_%li_0_[0]",page,fnum);
	addPrice ( page , field , price );
}

// put "dollars" into "field" and "pennies" into "field2"
void addPrice3 ( long page, char *field , float price , char *field2 ) {
	// convert to string
	char buf[100];
	long dollars = (long)(price *100 + 0.5);
	dollars /= 100;
	sprintf ( buf , "%li", dollars );
	addText ( page , field , buf );
	// get pennies and put into adjacent field following us
	long pennies = (long) (price * 100.0 + 0.5);
	if ( pennies < 0 ) pennies *= -1;
	pennies %= 100;
	sprintf ( buf , "%02li", pennies );
	addText ( page , field2, buf );
}

void addPrice2 ( long page, char *field , float price ) {
	// get digits afer the letter "f" in field
	char *p = strstr(field,"f");
	if ( ! p ) { char *xx=NULL;*xx=0; }
	// skip until right f
	for ( ; p[2] !='_' ; p = strstr(p+1,"f") );
	// sanity
	if ( ! p ) { char *xx=NULL;*xx=0;}
	// skip "fN_"
	p += 3;
	// store that as prefix
	char field2[128];
	char *fp2 = field2;
	long plen = p - field;
	memcpy ( fp2, field , plen );
	fp2 += plen;
	// record the field number
	long n = atoi(p);
	// count digits in the field number for padding with 0's
	long width = 0;
	for ( ; isdigit(*p); p++ ) width++;
	// print the pennies fieldname
	if ( width == 1 )
		sprintf(fp2,"%01li_0_[0]",n+1);
	else if ( width == 2 )
		sprintf(fp2,"%02li_0_[0]",n+1);
	else if ( width == 3 )
		sprintf(fp2,"%03li_0_[0]",n+1);
	else { char *xx=NULL;*xx=0; }

	addPrice3 ( page , field , price , field2 );
}

#define MAX_TRANS 80000

char *checks[] = {
	// january 2008 from eStmt
	//"6550 5507 "



	// january 2009 from bofa pdf
	"1284 2005 pnm",
	"1284 2011 martha",
	"1284 2012 jezebel inc",
	"1284 2013 martha",
	"1284 2014 jezebel inc",
	"1284 2015 susan",
	"1284 2016 the tile guys",
	"1284 2018 inforelay",
	"1284 2020 eduardo rojo",
	"1284 2021 city of abq permit fee",
	"1284 2022 martha",
	"1284 2023 city of abq permit",
	"1284 2026 martha",
	"1284 2028 eduardo rojo",
	"1284 5011 gilkey and stephenson",
	"1284 5012 smith and cook",
	"1284 5015 united healthcare insurance",
	// 02_27_2009.pdf
	"1284 2025 gourav",
	"1284 2027 futa taxes",
	"1284 2029 judy",
	"1284 2030 jezebel inc",
	"1284 2031 martha",
	"1284 2033 jezebel inc",
	"1284 2034 eduardo rojo",
	"1284 2035 martha",
	"1284 2036 martha",
	"1284 2037 jezebel inc",
	"1284 2038 jezebel inc rent",
	"1284 2039 martha",
	"1284 5018 MVD motor vehicles",
	"1284 5019 sylvain segal",
	"1284 5020 cna insurance",
	"1284 5021 heights lock and key",
	"1284 5022 TAS security",
	"1284 5025 lobo internet",
	"1284 5026 summit electric",
	"1284 5027 marks plumbing",
	"1284 5028 smith and cook",
	"1284 5029 wilmerhale",
	"1284 5030 dan hansen",
	// 03_31_2009.pdf
	"1284 2040 jezebel inc",
	"1284 2041 martha",
	"1284 2043 jezebel inc",
	"1284 2045 martha",
	"1284 2046 martha",
	"1284 2047 martha",
	"1284 5037 lobo internet",
	"1284 5038 united healthcare insurance",
	"1284 5039 cna insurance",
	"1284 5042 sylvain segal",
	"1284 5044 transamerica retirement",
	"1284 5045 western disposal",
	"1284 5046 city of abq false alarm fee",
	"1284 5047 citylink fiber",
	"1284 5048 citylink fiber",
	"1284 5049 guiding light electrician",
	"1284 5052 smith and cook",
	"1284 5053 gilkey and stephenson",
	"1284 5056 western disposal",
	"1284 5057 new mexico dept of workforce",
	"1284 5058 dallas county tax assesor",
	"1284 5059 abq fire dept inspection",
	// 04_30_2009.pdf
	"1284 2048 martha",
	"1284 2049 jezebel inc",
	"1284 2050 martha",
	"1284 2051 martha",
	"1284 2061 martha",
	"1284 5060 cna insurance",
	"1284 5064 sylvain segal",
	"1284 5066 united healthcare insurance",
	"1284 5067 united healthcare insurance",
	"1284 5068 gilkey and stephenson",
	"1284 5069 lobo internet",
	"1284 5072 smith and cook",
	"1284 5076 western disposal",
	"1284 5077 guiding light electrician",
	// 05_29_2009.pdf
	"1284 2052 martha",
	"1284 2053 martha",
	"1284 2054 jezebel inc",
	"1284 2055 martha",
	"1284 2060 jezebel inc",
	"1284 2062 martha",
	"1284 2063 fitness superstore",
	"1284 2064 martha",
	"1284 5071 sylvain segal",
	"1284 5080 robert goodman marcus reimbursement",
	"1284 5081 town of bernalillo",
	"1284 5082 new mexico gas",
	"1284 5084 TAS security",
	"1284 5086 lobo internet",
	"1284 5088 united healthcare insurance",
	"1284 5089 cna insurance",
	"1284 5090 mvd motor vehicles fee",
	"1284 5093 PNM",
	"1284 5094 sylvain segal",
	"1284 5099 gilkey and stephenson",
	"1284 5100 smith and cook",
	"1284 5101 western disposal",
	"1284 5102 rudolph friedmann alex case",
	// 06_30_2009.pdf
	"1284 2056 martha",
	"1284 2057 jezebel inc",
	"1284 2059 jezebel inc",
	"1284 2065 uspto",
	"1284 2066 garcia honda",
	"1284 2067 martha",
	"1284 2068 martha",
	"1284 2069 martha",
	"1284 2070 jezebel inc",
	"1284 2071 jezebel inc",
	"1284 5104 citylink fiber",
	"1284 5105 summit electric",
	"1284 5106 citylink fiber",
	"1284 5107 citylink fiber",
	"1284 5108 rudolph friedman alex case",
	"1284 5113 transamerica retirement",
	"1284 5114 united healthcare insurance",
	"1284 5115 cna insurance",
	"1284 5116 kim dincel",
	"1284 5117 kim dincel",
	"1284 5118 gilkey and stephenson",
	"1284 5120 PNM",
	"1284 5121 PNM",
	"1284 5123 sylvain segal",
	"1284 5124 smith and cook",
	"1284 5125 citylink fiber",
	// 07_31_2009.pdf
	"1284 2072 martha",
	"1284 2073 jezebel inc",
	"1284 2074 jezebel inc",
	"1284 2075 martha",
	"1284 2076 terminix",
	"1284 2077 martha",
	"1284 2079 martha",
	"1284 2083 martha",
	"1284 2084 premier flooring llc",
	"1284 5129 cna insurance",
	"1284 5130 kim dincel",
	"1284 5131 lobo internet",
	"1284 5133 transamerica retirement",
	"1284 5134 united healthcare insurance",
	"1284 5135 thompson ac",
	"1284 5139 gilkey and stephenson",
	"1284 5141 pnm",
	"1284 5142 pnm",
	"1284 5144 smith and cook", // kim dincel",
	"1284 5146 western disposal",
	"1284 5147 wilmerhale",
	// 08_31_2009.pdf
	"1284 2078 jezebel inc",
	"1284 2085 martha",
	"1284 2086 blind express",
	"1284 2100 martha",
	"1284 5148 citylink fiber",
	"1284 5149 cna insurance",
	"1284 5151 lobo internet",
	"1284 5153 pnm",
	"1284 5154 tas security",
	"1284 5156 united healthcase insurance",
	"1284 5157 smith and cook",
	"1284 5159 wilmerhale",
	"1284 5163 gilkey and stephenson",
	"1284 5165 western disposal",
	"1284 5166 gilkey and stephenson",
	"1284 5171 pnm",
	"1284 5172 pnm",
	// 09_39_2009.pdf
	"1284 2087 martha",
	"1284 2088 jezebel inc",
	"1284 2089 martha",
	"1284 2104 martha",
	"1284 5167 kevin elhers",
	"1284 5168 citylink fiber",
	"1284 5169 kim dincel",
	"1284 5173 united healthcare insurance",
	"1284 5174 smith and cook",
	"1284 5175 smith and cook",
	"1284 5176 wilmerhale",
	"1284 5178 pnm",
	"1284 5179 pnm",
	"1284 5182 kevin elhers",
	"1284 5185 pnm",
	"1284 5186 lobo internet",
	"1284 5187 citylink fiber",
	// 10_30_2009.pdf
	"1284 2105 jezebel inc",
	"1284 2106 premier flooring",
	"1284 5188 kim dincel",
	"1284 5192 lobo internet",
	"1284 5193 pnm",
	"1284 5194 pnm",
	"1284 5199 gilkey and stephenson",
	"1284 5200 dan hansen",
	"1284 5203 smith and cook",
	"1284 5204 tas security",
	"1284 5205 thompson ac",
	"1284 5206 western disposal",
	"1284 5208 transamerica retirement",
	"1284 5209 tlc plumbing",
	"1284 5210 liberty life",
	"1284 5212 kim dincel",
	// 11_30_2009.pdf
	"1284 2107 jezebel inc",
	"1284 2108 mvd express fees",
	"1284 2109 martha",
	"1284 5207 discount blinds & shutters",
	"1284 5211 gourav",
	"1284 5213 bogan prop tax",
	"1284 5214 madrid prop tax",
	"1284 5215 madrid prop tax",
	"1284 5216 lobo internet",
	"1284 5219 tas security",
	"1284 5220 town of bernalillo",
	"1284 5222 united healthcare insurance",
	"1284 5223 citylink fiber",
	"1284 5224 abaca email systems",
	"1284 5225 gilkey and stephenson",
	"1284 5226 kim dincel",
	"1284 5229 smith and cook",
	"1284 5231 tlc plumbing",
	"1284 5235 pnm electric",
	"1284 5236 pnm electric",
	"1284 5239 premier flooring llc",
	// 12_31_2009.pdf
	"1284 2090 land prop tax",
	"1284 2091 jezebel inc",
	"1284 2110 dwights glass & mirror",
	"1284 5228 sylvain segal",
	"1284 5230 tas security",
	"1284 5237 simple grinnel",
	"1284 5240 partap",
	"1284 5241 gilkey and stephenson",
	"1284 5242 kim dincel",
	"1284 5243 lobo internet",
	"1284 5249 summit electric",
	"1284 5251 united healthcare insurance",
	"1284 5252 kevin elhers",
	"1284 5253 zak betz",
	"1284 5254 western disposal",
	// 01_29_2010.pdf
	"1284 2111 jezebel inc",
	"1284 2112 state public regulation commission fee",
	"1284 2113 NM tax & revenue dept",
	"1284 2114 nm department of labor",
	"1284 2115 NM tax & revenue dept",
	"1284 5248 smith and cook",
	"1284 5256 partap",
	"1284 5257 zak",
	"1284 5258 andrew jaffe",
	"1284 5259 matthew rafferty",
	"1284 5265 united healthcare insurance",
	"1284 5266 citylink fiber",
	"1284 5267 matthew rafferty",
	"1284 5268 partap",
	"1284 5270 dwights glass & mirror",
	"1284 5271 gilkey and stephenson",
	"1284 5272 lobo internet",
	"1284 5275 smith and cook",
	"1284 5278 citylink fiber",
	// 02_26_2010.pdf
	"1284 2116 jezebel inc",
	"1284 2118 merit bennet pc",
	"1284 2119 merit bennet pc",
	"1284 5269 zak",
	"1284 5279 matthew rafferty",
	"1284 5280 partap",
	"1284 5281 zak",
	"1284 5282 citylink fiber",
	"1284 5283 matthew rafferty",
	"1284 5284 partap",
	"1284 5286 matthew rafferty",
	"1284 5287 kim dincel",
	"1284 5288 lobo internet",
	"1284 5289 nm gas",
	"1284 5290 pnm",
	"1284 5291 pnm",
	"1284 5294 tas security",
	"1284 5295 thompson ac",
	"1284 5297 gilkey and stephenson",
	"1284 5299 smith and cook",
	"1284 5300 transamerica retirement",
	"1284 5303 wilmerhale",
	"1284 5304 citylink fiber",
	// 03_31_2010.pdf
	"1284 5285 zak",
	"1284 5307 gilkey and stephenson",
	"1284 5309 lobo internet",
	"1284 5312 pnm",
	"1284 5313 pnm",
	"1284 5315 smith and cook",
	"1284 5317 western disposal",
	"1284 5318 wilmerhale",
	// 04_30_2010.pdf
	"1284 2101 uspto",
	"1284 2120 partap",
	"1284 5319 lobo internet",
	"1284 5320 nm gas",
	"1284 5321 pnm",
	"1284 5322 pnm",
	"1284 5324 smith and cook",
	"1284 5325 town of bernalillo",
	"1284 5327 tas security",
	"1284 5328 city of abq false alarm fee",
	"1284 5329 citylink",
	"1284 5330 wilmerhale",
	// 05_28_2010.pdf
	"1284 2092 nm vital records",
	"1284 5338 medical",
	"1284 5339 citylink fiber",
	"1284 5341 lobo internet",
	"1284 5343 smith and cook",
	"1284 5344 tas security",
	"1284 5346 dean neuwirth",
	// 06_30_2010.pdf
	"1284 5347 citylink fiber",
	"1284 5348 american express",
	"1284 5349 armed response team",
	"1284 5353 lobo internet",
	"1284 5361 final bill of something", // 10k
	"1284 5362 heights lock and key",
	"1284 5367 sylvain segal",
	"1284 5368 thompson ac",
	"1284 5369 wilmerhale",
	// 07_30_2010.pdf
	"1284 2093 sky high tech",
	"1284 5370 citylink fiber",
	"1284 5381 wilmerhale",
	// 08_31_2010.pdf
	"1284 5382 wilmerhale",
	"1284 5386 lobo internet",
	"1284 5391 tas security",
	"1284 5395 zak",
	"1284 5397 zak",
	// 09_30_2010.pdf
	"1284 5398 citylink fiber",
	"1284 5400 armed response team",
	"1284 5409 wilmerhale",
	"1284 5410 matthew rafferty",
	"1284 5411 partap",
	"1284 5414 rebekah",
	// 10_29_2019.pdf
	"1284 5412 zak",
	"1284 5413 zak",
	"1284 5415 dean neuwirth",
	"1284 5416 rebekah",
	"1284 5417 zak",
	"1284 5418 citylink fiber",
	"1284 5419 roadrunner wireless",
	"1284 5423 lobo internet",
	"1284 5428 thompson ac",
	"1284 5431 sheehan & sheehan legal",
	// 11_30_2010.pdf
	"1284 5432 zak",
	"1284 5433 rebekah",
	"1284 5434 citylink fiber",
	"1284 5437 lobo internet",
	"1284 5438 dan hansen",
	"1284 5444 tas security",
	// 12_31_2010.pdf
	"1284 2094 mvd motor vehciles fee",
	"1284 5447 zak",
	"1284 5448 citylink fiber",
	"1284 5449 rebekah",
	"1284 5450 armed response team",
	"1284 5451 lobo internet",
	"1284 5460 bogan prop tax",
	"1284 5461 zak"
};

	

char *getCategory ( Trans *t ) ;
long addQBPayroll ( Trans *t , long nt ) ;
void addQBChecks ( Trans *t , long nt ) ;
float atof2 ( char *p ) ;
void make941s ( Trans **t , long nt ) ;
long hash32 ( char *s ) ;
char *gb_strcasestr ( char *haystack , char *needle ) ;

// independent of case
char *gb_strcasestr ( char *haystack , char *needle ) {
        long needleSize   = strlen(needle);
        long haystackSize = strlen(haystack);
        long n = haystackSize - needleSize ;
        for ( long i = 0 ; i <= n ; i++ ) {
                // keep looping if first chars do not match
                if ( tolower(haystack[i]) != tolower(needle[0]) ) continue;
                // if needle was only 1 char it's a match
                if ( ! needle[1] ) return &haystack[i];
                // compare the whole strings now
                if ( strncasecmp ( &haystack[i] , needle , needleSize ) == 0 )
                        return &haystack[i];
        }
        return NULL;
}

bool isPriceFormat ( char *p ) {
	// mus tbe number
	if ( ! isdigit(*p) && *p != '-' && *p != '+' ) return false;
	long pcount = 0;
	if ( *p == '-' ) p++;
	if ( *p == '+' ) p++;
	if ( ! isdigit(*p) ) return false;
	// scan
	for ( ; *p ; p++ ) {
		if ( *p == '.' ) { pcount++; continue;}
		if ( isdigit(*p) ) continue;
		if ( *p == ',' ) continue;
		if ( *p == ' ' ) break;
		if ( *p == '\n' ) break;
		if ( *p == '\t' ) break;
		if ( isalnum(*p) ) break;
		// for mat''s 0265 acct, a - or + follows the price
		if ( *p=='+' || *p=='-' ) break;
		return false;
	}
	if ( pcount <= 0 ) return false;
	return true;
}
		

char *loadTransactions ( bool newFormat , char *bufPtr , char *bufEnd ) ;

// just make these global i guess
Trans t[MAX_TRANS];
long nt = 0;

int main ( int argc , char *argv[] ) {


	// print dummy fdf for making maps 
	if ( argc == 2 && strcmp(argv[1],"map")==0 ) {
		FILE *fd = fopen ("./2009/map.fdf","w");
		if (  ! fd ) {
			fprintf(stdout,"could not open ./map.fdf for writing\n");
			return 0;
		}
		fprintf(fd,
			"%%FDF-1.2\n"
			"1 0 obj<</FDF<< /Fields[\n" );
		for ( long page = 1 ; page < 8 ; page++ ) {
			// print out
			char fullname[1024];
			for  ( long i = 0 ; i < 200 ; i++ ) {
				// construct field name
				sprintf ( fullname,"topmostSubform[0].Page%li[0].f%li_%li_0_[0]",page,page,i);
				// print form field value
				fprintf(fd,"<</T(%s)/V(f%li)>>\n",fullname,i);
				// padded?
				if ( i < 10 ) {
					sprintf ( fullname,
						  "topmostSubform[0].Page%li[0].f%li_%02li_0_[0]",page,page,i);
					// print form field value
					fprintf(fd,"<</T(%s)/V(g%li)>>\n",fullname,i);
				}
				if ( i < 100 ) {
					sprintf ( fullname,
						  "topmostSubform[0].Page%li[0].f%li_%03li_0_[0]",page,page,i);
					// print form field value
					fprintf(fd,"<</T(%s)/V(W%li)>>\n",fullname,i);
				}
				// for form 1040
				sprintf ( fullname,
					  "topmostSubform[0].Page%li[0].p%li-t%li[0]",page,page,i);
				fprintf(fd,"<</T(%s)/V(t%li)>>\n",fullname,i);
				
			}
			// headpage junk for 1120
			for  ( long i = 0 ; i < 10 ; i++ ) {
				// construct field name
			       sprintf(fullname,"topmostSubform[0].Page%li[0].Headpage%li[0].f%li_%02li_0_[0]",
					  page,page,page,i);
				// print form field value
				fprintf(fd,"<</T(%s)/V(H%li)>>\n",fullname,i);
			}
			// address junk for 1120 as well
			for  ( long i = 0 ; i < 30 ; i++ ) {
				// construct field name
				sprintf(fullname,"topmostSubform[0].Page%li[0].Adress%li[0].f%li_%02li_0_[0]",
					  page,page,page,i);
				// print form field value
				fprintf(fd,"<</T(%s)/V(A%li)>>\n",fullname,i);
			}
		}
		// wrap it up
		fprintf ( fd,
			  "] >> >>\n"
			  "endobj\n"
			  "trailer\n"
			  "<</Root 1 0 R>>\n"
			  "%%%%EOF\n" );
		fclose ( fd );
		return 1;
	}

	// first arg is the directory of log* files
	if ( argc < 3 ) {
	usage:
		fprintf(stdout,"Usage:\tditzy <matt|gb> <yearOfStatements> [taxformNum]\n");
		fprintf(stdout,"Must contain subdirs in current directory of the form \"/xxxx/\", "
			"where xxx is the last 4 digits of the bank acct and each of those "
			"subdirs has the statements downloaded from bofa.com\n");
		//fprintf(stdout,"Can also contain\nfiles starting with "
		//	"\"invoice\" which will be assumed to be invoices "
		//	"we sent out\nusing invoicer.cpp. ");
		//fprintf(stdout,"Format of invoice files must be:\n"
		//	"date +/-/B<amount> acct# category entityName "
		//	"description.\n");
		return -1;
	}

	// from irs.gov:
	// Generally, when an LLC has only one member, the fact that it is an LLC is ignored  for the 
	// purpose of filing a federal tax return. Remember, this is only a mechanism for tax purposes. 
	// It does not change the fact that the business is legally a Limited Liability Company.
	// If the only member of the LLC is an individual, the LLC income and expenses are reported on 
	// Form 1040, Schedule C, E, or F.disregardedor 
	//bool isgbllc = false;
	long ismatt = 0;
	long isgbcorp = 0;
	if ( strcmp(argv[1],"matt") == 0 ) ismatt = 1;
	if ( strcmp(argv[1],"gb") == 0 ) isgbcorp = 1;

	if ( ! ismatt && ! isgbcorp ) goto usage;

	long form = 0;
	if ( argc == 4 ) form = atoi(argv[3]);
	if ( form != 0    &&
	     form != 1040 &&
	     form != 1120 &&
	     form != 4562 ) {
		fprintf(stdout,"Form %li is unsupported\n",form);
		return -1;
	}
	g_form = form;


	// year should be last
	long accyear = atol(argv[2]);
	if ( accyear < 2008 || accyear > 2020 ) {
		fprintf(stdout,"ditzy: bad year of %li\n",accyear);
		goto usage;
	}
	// set globally
	g_accyear = accyear;
	g_ismatt = ismatt;
	g_isgbcorp = isgbcorp;

	// note it
	fprintf(stdout,"processing for year %li\n",accyear);
	// truncate off year
	//argc--;

	// reset all
	for ( long i = 0 ; i < MAX_TRANS ; i++ ) {
		t[i].m_checkNum = -1;
		t[i].m_srcAcct = NULL;
		t[i].m_dstAcct = NULL;
		t[i].m_refNum  = 0;
		t[i].m_ptr     = NULL;
		t[i].m_cat     = NULL;
	}


	//long nd = 0;

	char *buf = (char *)malloc(3000000);
	char *bufPtr = buf;
	char *bufEnd = buf + 3000000;
	//char *nextStatement = buf;

	bufPtr = loadTransactions( false , buf , bufEnd );

	bufPtr = loadTransactions ( true , bufPtr , bufEnd );


	///////////////////////////////
	//
	// . now guess the "type" of transaction
	// . the types are basically tax categories
	// WAGES
	// WITHHOLD1 -- withheld from employee
	// WITHHOLD2 -- withheld from company
	// TAXES
	// FOOD
	// DATACTR
	// PROFESSIONAL
	// OFFCSUPPLIES
	// INSURANCE
	// RENT
	// VEHICLE
	// LOAN
	// LOANTOSH
	// DATASRVC (PHONE,etc)
	// HOLD
	// REPAIR
	// LEGAL
	// BANKFEES
	// ASSETS
	// COMPUTERASSETS
	// CAPITALASSETS
	//
	///////////////////////////////
	for ( long i = 0 ; i < nt ; i++ ) {
		// if alreadyy assigned in printDepreciationTable() skip
		if ( t[i].m_cat ) continue;
		// set the category
		t[i].m_cat = getCategory ( &t[i] );
	}


	//////
	//
	// . in 2009 gb bought part of matt's home loan from bogan basically
	// . matt's account needs to reflect the gbhomeloan
	// . gbcorp has this as "Rls - Debit" for about 450k or so but not matt's acct
	//
	//////
	if ( ismatt ) {
		float principal = 426830.28;
		t[nt].m_date = "08/31";
		t[nt].m_year = 2009;
		t[nt].m_desc = "gb home loan buyout";
		t[nt].m_cat  = "GBHOMELOAN";
		t[nt].m_amount = principal;
		nt++;
		// and reduce bofa home loan
		t[nt].m_date = "08/31";
		t[nt].m_year = 2009;
		t[nt].m_desc = "gb home loan buyout";
		t[nt].m_cat  = "BOFAHOMELOAN";
		t[nt].m_amount = -1*principal;
		nt++;
	}

	// use some of SHARELOAN for MATTSOFTLICINTEREST for 2009
	if ( ismatt ) {
		t[nt].m_date = "12/31";
		t[nt].m_year = 2009;
		t[nt].m_desc = "shareholder loan";
		t[nt].m_cat  = "SHARELOAN";
		t[nt].m_amount = -20000;
		nt++;
		t[nt].m_date = "12/31";
		t[nt].m_year = 2009;
		t[nt].m_desc = "matt software license";
		t[nt].m_cat  = "MATTSOFTLICINTEREST";
		t[nt].m_amount = +20000;
		nt++;
		// 2010
		t[nt].m_date = "12/31";
		t[nt].m_year = 2010;
		t[nt].m_desc = "shareholder loan";
		t[nt].m_cat  = "SHARELOAN";
		t[nt].m_amount = -58000;
		nt++;
		t[nt].m_date = "12/31";
		t[nt].m_year = 2010;
		t[nt].m_desc = "matt software license";
		t[nt].m_cat  = "MATTSOFTLICINTEREST";
		t[nt].m_amount = +58000;
		nt++;
	}
	// and same for gbcorp
	if ( isgbcorp ) {
		t[nt].m_date = "12/31";
		t[nt].m_year = 2009;
		t[nt].m_desc = "shareholder loan";
		t[nt].m_cat  = "SHARELOAN";
		t[nt].m_amount = +20000;
		nt++;
		t[nt].m_date = "12/31";
		t[nt].m_year = 2009;
		t[nt].m_desc = "matt software license";
		t[nt].m_cat  = "MATTSOFTLICINTEREST";
		t[nt].m_amount = -20000;
		nt++;
		// 2010
		t[nt].m_date = "12/31";
		t[nt].m_year = 2010;
		t[nt].m_desc = "shareholder loan";
		t[nt].m_cat  = "SHARELOAN";
		t[nt].m_amount = +58000;
		nt++;
		t[nt].m_date = "12/31";
		t[nt].m_year = 2010;
		t[nt].m_desc = "matt software license";
		t[nt].m_cat  = "MATTSOFTLICINTEREST";
		t[nt].m_amount = -58000;
		nt++;
	}


	// split out the bofahomeloan interest from principal for each year
	if ( ismatt ) {
		// . for 2009 matt paid $8318.59 interest on 2ndary loan
		// . for 2009 matt paid $54261 interest on primary loan
		// . for 2009 matt paid $10103.71 in prop taxes
		// add prop taxes
		t[nt].m_date = "12/31";
		t[nt].m_year = 2009;
		t[nt].m_desc = "home prop taxes";
		t[nt].m_amount = -10103.71;
		t[nt].m_cat = "HOMEPROPTAXES";
		nt++;
		// add loan interest
		t[nt].m_date = "12/31";
		t[nt].m_year = 2009;
		t[nt].m_desc = "bofa home loan interest";
		t[nt].m_amount = -54261.00 - 8318.59;
		t[nt].m_cat = "BOFAHOMELOANINTEREST";
		nt++;
		// adjust principal
		t[nt].m_date = "12/31";
		t[nt].m_year = 2009;
		t[nt].m_desc = "bofa home loan principal correction";
		t[nt].m_amount = +10103.71 + 54261.00 + 8318.59;
		t[nt].m_cat = "BOFAHOMELOAN";
		nt++;
		// . for 2010, gb took over the 2ndary and part of the primary therefore
		//   matt pays interest to gb in GBHOMELOANINTEREST below
		// . but to bofa paid 44146.22 in interest and 11400.88 in prop taxes
		t[nt].m_date = "12/31";
		t[nt].m_year = 2010;
		t[nt].m_desc = "home prop taxes";
		t[nt].m_amount = -11400.88;
		t[nt].m_cat = "HOMEPROPTAXES";
		nt++;
		// add loan interest
		t[nt].m_date = "12/31";
		t[nt].m_year = 2010;
		t[nt].m_desc = "bofa home loan interest";
		t[nt].m_amount = -44146.22;
		t[nt].m_cat = "BOFAHOMELOANINTEREST";
		nt++;
		// adjust principal
		t[nt].m_date = "12/31";
		t[nt].m_year = 2010;
		t[nt].m_desc = "bofa home loan principal correction";
		t[nt].m_amount = 11400.88 + 44146.22;
		t[nt].m_cat = "BOFAHOMELOAN";
		nt++;
		//
		// for 2011, let's see.
		//
		// i need to verify this is correct. i just took it
		// from the previous year because i don't know it yet
		float propTax = 10103.71;
		float loanInterest = 44146.22;
		t[nt].m_date = "12/31";
		t[nt].m_year = 2011;
		t[nt].m_desc = "home prop taxes";
		t[nt].m_amount = -1*propTax;
		t[nt].m_cat = "HOMEPROPTAXES";
		nt++;
		// add loan interest
		t[nt].m_date = "12/31";
		t[nt].m_year = 2011;
		t[nt].m_desc = "bofa home loan interest";
		t[nt].m_amount = -1*loanInterest;
		t[nt].m_cat = "BOFAHOMELOANINTEREST";
		nt++;
		// cancel out the part of the expenses due to the
		// bofa home loan interest, otherwise, it is seen
		// as an ordinary expense.
		t[nt].m_date = "12/31";
		t[nt].m_year = 2011;
		t[nt].m_desc = "bofa home loan principal correction";
		t[nt].m_amount = propTax + loanInterest;
		t[nt].m_cat = "BOFAHOMELOAN";
		nt++;
		//
		// 2012
		//
		propTax = 8981.38;
		loanInterest = 42760.52;
		t[nt].m_date = "12/31";
		t[nt].m_year = 2012;
		t[nt].m_desc = "home prop taxes";
		t[nt].m_amount = -1*propTax;
		t[nt].m_cat = "HOMEPROPTAXES";
		nt++;
		// add loan interest
		t[nt].m_date = "12/31";
		t[nt].m_year = 2012;
		t[nt].m_desc = "bofa home loan interest";
		t[nt].m_amount = -1*loanInterest;
		t[nt].m_cat = "BOFAHOMELOANINTEREST";
		nt++;
		// cancel out the part of the expenses due to the
		// bofa home loan interest, otherwise, it is seen
		// as an ordinary expense.
		t[nt].m_date = "12/31";
		t[nt].m_year = 2012;
		t[nt].m_desc = "bofa home loan principal correction";
		t[nt].m_amount = propTax + loanInterest;
		t[nt].m_cat = "BOFAHOMELOAN";
		nt++;
	}

	// . similarly split up the bofa bogan loan payments
	// . paid 47769.94 interest in 2009
	// . paid 46458.86 interest in 2010
	// . paid 45053.42 interest in 2011
	if ( ismatt ) {
		// . for 2009 matt paid $8318.59 interest on 2ndary loan
		// . for 2009 matt paid $54261 interest on primary loan
		// . for 2009 matt paid $10103.71 in prop taxes
		// add loan interest
		t[nt].m_date = "12/31";
		t[nt].m_year = 2009;
		t[nt].m_desc = "bofa bogan loan interest";
		t[nt].m_amount = -47769.94;
		t[nt].m_cat = "BOFABOGANLOANINTEREST";
		nt++;
		// adjust principal
		t[nt].m_date = "12/31";
		t[nt].m_year = 2009;
		t[nt].m_desc = "bofa bogan loan principal correction";
		t[nt].m_amount = +47769.94;
		t[nt].m_cat = "BOFABOGANLOAN";
		nt++;
		//
		// 2010 bogan loan
		//
		t[nt].m_date = "12/31";
		t[nt].m_year = 2010;
		t[nt].m_desc = "bofa bogan loan interest";
		t[nt].m_amount = -46458.86;
		t[nt].m_cat = "BOFABOGANLOANINTEREST";
		nt++;
		// adjust principal
		t[nt].m_date = "12/31";
		t[nt].m_year = 2010;
		t[nt].m_desc = "bofa bogan loan principal correction";
		t[nt].m_amount = +46458.86;
		t[nt].m_cat = "BOFABOGANLOAN";
		nt++;
		// 
		// 2011 bogan loan
		//
		t[nt].m_date = "12/31";
		t[nt].m_year = 2011;
		t[nt].m_desc = "bofa bogan loan interest";
		t[nt].m_amount = -45053.42;
		t[nt].m_cat = "BOFABOGANLOANINTEREST";
		nt++;
		// adjust principal since interest is now in separate category
		t[nt].m_date = "12/31";
		t[nt].m_year = 2011;
		t[nt].m_desc = "bofa bogan loan principal correction";
		t[nt].m_amount = +45053.42;
		t[nt].m_cat = "BOFABOGANLOAN";
		nt++;
		// 
		// 2012 bogan loan
		//
		t[nt].m_date = "12/31";
		t[nt].m_year = 2012;
		t[nt].m_desc = "bofa bogan loan interest";
		t[nt].m_amount = -43673.88;
		t[nt].m_cat = "BOFABOGANLOANINTEREST";
		nt++;
		// adjust principal since interest is now in separate category
		t[nt].m_date = "12/31";
		t[nt].m_year = 2012;
		t[nt].m_desc = "bofa bogan loan principal correction";
		t[nt].m_amount = +43673.88;
		t[nt].m_cat = "BOFABOGANLOAN";
		nt++;

	}


	// loss of income in madrid.
	// bought 7/15/09. 4 months rent at 2500 per month in 2009.
	// NOTE: now jezebel claims the loss and we basically get her rent
	// and pay it to chana... see CHANALOANINTEREST stuff
	/*
	for ( long y = 2009 ; isgbcorp && y <= g_accyear ; y++ ) {
		// loss of rent on madrid bldg
		t[nt].m_date = "12/31";
		t[nt].m_year = y;
		t[nt].m_desc = "madrid income loss";
		if ( y == 2009 ) t[nt].m_amount = -2500.0 * 4.0; // last 4 months
		else             t[nt].m_amount = -2500.0 * 12.0;
		// i don't think this will fly
		//t[nt].m_cat    = "MADRIDRENTLOSS";
		//nt++;
	}
	*/

	// matt had some soact stock under his name sold by gb for $39,183.18 so we have to
	// deduct that from the software license payments and relabel it as SOACTSTOCK.
	// the stock was sold in 2009.
	// we deposited into gb's bofa accounts in 2010.
	// the soact stock sold was under matt's SSN even though gb corp took the money.
	// so remove that stock sale price of 29k from matt's software license fee
	// and call it soact stock
	if ( ismatt ) {
		// 2009
		t[nt].m_date = "12/31";
		t[nt].m_year = 2009;
		t[nt].m_desc = "soact stock sale";
		t[nt].m_amount = 39183.18;
		t[nt].m_cat = "SOACTSTOCK";
		nt++;
		// 2010
		t[nt].m_date = "12/31";
		t[nt].m_year = 2010;
		t[nt].m_desc = "shareholder loan repayment";
		t[nt].m_amount = -39183.18;
		t[nt].m_cat = "SHARELOAN";
		nt++;
	}
	// matt used the 39k to pay down his pricipal shareholder loan
	if ( isgbcorp ) {
		// subtract to correct
		t[nt].m_date = "12/31";
		t[nt].m_year = 2010;
		t[nt].m_desc = "soact stock sale correction";
		t[nt].m_amount = -39183.18;
		t[nt].m_cat = "SOACTSTOCK";
		nt++;
		// then add back as loan repayment
		t[nt].m_date = "12/31";
		t[nt].m_year = 2010;
		t[nt].m_desc = "shareholder loan repayment";
		t[nt].m_amount = 39183.18;
		t[nt].m_cat = "SHARELOAN";
		nt++;
	}

	// likewise gb deposited matt's refund on his 1099-g for about 4k from tax 2008 pit from new mexico
	if ( ismatt ) {
		t[nt].m_date = "12/31";
		t[nt].m_year = 2009;
		t[nt].m_desc = "1099-g state refund correction";
		t[nt].m_amount = 4126.00;
		t[nt].m_cat = "MATTTAXREFUND";
		nt++;
		// repay loan with it
		t[nt].m_date = "12/31";
		t[nt].m_year = 2009;
		t[nt].m_desc = "shareholder loan repayment";
		t[nt].m_amount = -4126.00;
		t[nt].m_cat = "SHARELOAN";
		nt++;
	}
	if ( isgbcorp ) {
		t[nt].m_date = "12/31";
		t[nt].m_year = 2009;
		t[nt].m_desc = "1099-g state refund correction";
		t[nt].m_amount = -4126.00;
		t[nt].m_cat = "MATTTAXREFUND";
		nt++;
		// add back as loan repayment in 2010 to make cash balance
		t[nt].m_date = "12/31";
		t[nt].m_year = 2009;
		t[nt].m_desc = "shareholder loan repayment";
		t[nt].m_amount = 4126.00;
		t[nt].m_cat = "SHARELOAN";
		nt++;
	
	}		

	// we have been treating the bank's error of double paying
	// gigablast inc's bogan rent to gigablast llc as a loan but 
	// that is costing us! so make up for their error by categorizing
	// those excess payments made in error as "IGNORE" from 2011
	// going forward since they were already classified as LLCLOAN
	// from 2010 and 2009 tax returns. but let's also record a special
	// transaction here to undo the loans so the principal appears paid
	// off then, at least the part of the principal that was made
	// in error.
	/*
	if ( isgbcorp ) {
		t[nt].m_date = "1/1";
		t[nt].m_year = 2011;
		t[nt].m_desc = "llc error loan repayment";
		// the initial loan of $184156 is still valid, but
		// the newly owed principal of $328124.75 reflects
		// that double rent payment. that was the principal
		// balance at the end of 2010 as recorded in the
		// tax returns i believe.
		t[nt].m_amount = 328124.75 - 184156.00; // $143968.75 
		t[nt].m_cat = "LLCLOAN";
		nt++;
	}
	*/


	// the minimum require loan interest (AFR or something)
#define LOANINTEREST .045

	/////////////////////////////
	//
	// now add in the books-only transactions
	//
	/////////////////////////////
	for ( long y = MINYEAR ; y <= g_accyear ; y++ ) {
		
		float loanamt;
		float loaninterest;

		// . matt needs to pay off interest on his shareholder loan
		// . this balances the software license fees matt recvs from
		//   gb inc. for use of his code WITH the interest payments
		//   matt makes on his shareholder loan of $466,335. an
		//   on-demand (pay back on demand) interest-only loan.
		
		// . how much is remaining on the principal of the loan?
		// . adds transactions into the loan account from t[] that match "SHARELOAN"
		loanamt       = getLoanAmt ("SHARELOAN", t, nt , y , ismatt );
		loaninterest  = loanamt * LOANINTEREST;
		// shareholder loan
		t[nt].m_date = "12/31";
		t[nt].m_year = y;
		t[nt].m_desc = "shareholder loan interest";
		t[nt].m_cat  = "SHARELOANINTEREST";
		if ( ismatt ) t[nt].m_amount = +1 * loaninterest;
		else          t[nt].m_amount = +1 * loaninterest;
		nt++;
		// software license interest balances the share loan interest
		t[nt].m_date = "12/31";
		t[nt].m_year = y;
		t[nt].m_desc = "matt software license";
		t[nt].m_cat  = "MATTSOFTLICINTEREST";
		if ( ismatt ) t[nt].m_amount = -1 * loaninterest;
		else          t[nt].m_amount = -1 * loaninterest;
		nt++;

		// . gb also has paid down $426,830.28 of matt's home loan so
		//   that it would collect the interest intstead. matt also
		//   covers the interest payments to gb with his software
		//   license fees to gb.
		loanamt       = getLoanAmt ("GBHOMELOAN", t, nt , y , ismatt);
		loaninterest  = loanamt * LOANINTEREST;

		t[nt].m_date = "12/31";
		t[nt].m_year = y;
		t[nt].m_desc = "gb home loan interest";
		t[nt].m_cat  = "GBHOMELOANINTEREST";
		if ( ismatt ) t[nt].m_amount = +1 * loaninterest;
		else          t[nt].m_amount = +1 * loaninterest;
		nt++;

		// software license
		t[nt].m_date = "12/31";
		t[nt].m_year = y;
		t[nt].m_desc = "matt software license";
		t[nt].m_cat  = "MATTSOFTLICINTEREST";
		if ( ismatt ) t[nt].m_amount = -1 * loaninterest;
		else          t[nt].m_amount = -1 * loaninterest;
		nt++;

		// . bonus interest payment for 2012
		// . 85k of the shareloan is actually softlic interest
		//   so make the fix like this
		// . will give matt extra income so he can use his NOL
		//   from previous years, etc.
		if ( y == 2012 ) {
			// we have to keep the cash balanced
			// so replace a shareloan transaction of 85000.00
			// with a loan interest transaction of 85000
			loaninterest = 85000.00;
			t[nt].m_date = "12/31";
			t[nt].m_year = y;
			t[nt].m_desc = "matt software license";
			t[nt].m_cat  = "MATTSOFTLICINTEREST";
			if ( ismatt ) t[nt].m_amount =  1 * loaninterest;
			else          t[nt].m_amount = -1 * loaninterest;
			nt++;
			// balance cash with this then
			t[nt].m_date = "12/31";
			t[nt].m_year = y;
			t[nt].m_desc = "share loan correction";
			t[nt].m_cat  = "SHARELOAN";
			if ( ismatt ) t[nt].m_amount = -1 * loaninterest;
			else          t[nt].m_amount =  1 * loaninterest;
			nt++;
			// 
		}

		// bonus interest payment in 2011
		if ( y == 2011 ) {
			// we have to keep the cash balanced
			// so replace a shareloan transaction of 85000.00
			// with a loan interest transaction of 85000
			loaninterest = 30000.00;
			t[nt].m_date = "12/31";
			t[nt].m_year = y;
			t[nt].m_desc = "matt software license";
			t[nt].m_cat  = "MATTSOFTLICINTEREST";
			if ( ismatt ) t[nt].m_amount =  1 * loaninterest;
			else          t[nt].m_amount = -1 * loaninterest;
			nt++;
			// balance cash with this then
			t[nt].m_date = "12/31";
			t[nt].m_year = y;
			t[nt].m_desc = "share loan correction";
			t[nt].m_cat  = "SHARELOAN";
			if ( ismatt ) t[nt].m_amount = -1 * loaninterest;
			else          t[nt].m_amount =  1 * loaninterest;
			nt++;
			// 
		}


		// . gb corp also gets interest for gb llc's loan that it 
		//   bought bogan with
		loanamt      = getLoanAmt ( "LLCLOAN" , t , nt , y, ismatt);
		loaninterest = loanamt * LOANINTEREST;
		t[nt].m_date = "12/31";
		t[nt].m_year = y;
		t[nt].m_desc = "llc loan interest";
		t[nt].m_cat  = "LLCLOANINTEREST";
		if ( ismatt ) t[nt].m_amount = +1 * loaninterest;
		else          t[nt].m_amount = +1 * loaninterest;
		nt++;
		// . bogan rent that gb corp pays to gb llc
		// . so we raise rent by the interest amount so that
		//   gb llc can use that to pay back to gb inc and it should
		//   balance out perfectly
		t[nt].m_date = "12/31";
		t[nt].m_year = y;
		t[nt].m_desc = "bogan rent";
		t[nt].m_cat  = "BOGANRENT";
		if ( ismatt ) t[nt].m_amount = -1 * loaninterest;
		else          t[nt].m_amount = -1 * loaninterest;
		nt++;

		// jezebel pays the chana loan for us, so count her
		// payments as rental income then we deduct for
		// all itnerest payments to chana
		if ( isgbcorp ) {
			t[nt].m_date = "12/31";
			t[nt].m_year = y;
			t[nt].m_desc = "chana loan";
			t[nt].m_cat  = "CHANALOANINTEREST";
			// loan payments (interest only) for whole year
			//t[nt].m_amount = -1 * 3300.00 * 12.0; 
			// jezebel claimed 35615.20 on the 1099-misc as rent
			// for 2011.
			t[nt].m_amount = -1 * 35615.20 ;
			nt++;
			// and the rental income from jezebel
			t[nt].m_date = "12/31";
			t[nt].m_year = y;
			t[nt].m_desc = "chana loan";
			t[nt].m_cat  = "MADRIDBLDG1RENT";
			// loan payments (interest only) for whole year
			t[nt].m_amount = 35615.20 ;
			nt++;
		}
	}


	// print out depreciation worksheet
	fprintf(stdout,"\n");
	// . returns total value as of year end g_accyear
	// . should also insert depreciation transactions into array
	nt = printDepreciationTable ( stdout, ismatt , t , nt );
	fprintf(stdout,"\n");

	// store payroll.txt into the Transaction array
	//nt = addQBPayroll ( t , nt );

	// now read in qbchecks.txt and try to match them to what we have and 
	// augment our descriptions!
	addQBChecks ( t , nt );

	// now set m_secs
	for ( long i = 0 ; i < nt ; i++ ) {
		// get the date in seconds
		struct tm ts1;
                memset(&ts1, 0, sizeof(tm));
		char *dd = t[i].m_date;
		long month = atoi(dd);
		ts1.tm_mon  = month - 1;
		long day = atoi(dd+3);
                ts1.tm_mday = day;
                ts1.tm_year = t[i].m_year - 1900;
                // make the time
                t[i].m_secs =  mktime(&ts1);
	}

	// point to them
	Trans *pt[MAX_TRANS];
	for ( long i = 0 ; i < nt ; i++ )
		pt[i] = &t[i];

	// now sort the ptrs by m_secs
	char flag = 1;
	while ( flag ) {
		// assume no flips
		flag = 0;
		// compare
		for ( long i = 0 ; i < nt - 1 ; i++ ) {
			if ( pt[i]->m_secs <= pt[i+1]->m_secs ) continue;
			// flip them
			flag = 1;
			Trans *tmp = pt[i];
			pt[i]   = pt[i+1];
			pt[i+1] = tmp;
		}
	}


	// generate a 941 for every quarter since 2008
	// do not do this now
	//make941s ( pt , nt );

	long ht[1024];
	char *ptr[1024];
	float sum[1024];
	float expense[1024];
	for ( long i = 0; i < 1024 ; i++ ) ht[i] = 0;
	float total1 = 0;
	float total2 = 0;
	float untaxableIncome = 0;

	fprintf(stdout,"\n");

	// print header
	fprintf(stdout,
		"date     "
		//"ref#            "
		"amt        "
		"src "
		//"        "
		//"dstact      "
		"category           "
		"balance "
		"  desc"
		"\n");
		
	float balance = 0.0;
	// set to intial cash in MINYEAR
	float gbcash;
	float mattcash;
	/*
	if ( MINYEAR == 2009 ) {
		// f1120 from year 2008 reports 859264 as the year ending cash!
		// so this is off by 30k...
		//gbcash = 829261.450; // = (59.90 + 573128.51 + 250194.40 + 5878.64);
		// ok, use what f1120 from 2008 reported
		gbcash = 859264;
		mattcash = 686.17 ;
	}
	else */
	if ( MINYEAR == 2008 ) {
		// took this from ending cash of year 2007 on f1120 for 2007
		gbcash = 1016407;
		// make this one up
		mattcash = 0;
	}
	else {
		char *xx=NULL;*xx=0;
	}
	// subtract 10,000 because i *think* there was an account we did not account 
	// for until later and it was not recorded on the 2010 return. the 2010
	// return listed $424,921 ending cash assets according to tracy stoddart
	if ( accyear >= 2009 ) {
		gbcash -= 10000.00;
	}


	if ( ismatt ) balance = mattcash;
	else          balance = gbcash;

	// now display the transactions
	for ( long i = 0 ; i < nt ; i++ ) {
		Trans *pp = pt[i];
		// skip if after us
		if ( pp->m_year > accyear ) continue;
		// start at 2009
		if ( pp->m_year < MINYEAR ) continue;
		// update balance
		bool cashTrans = true;
		if ( !strcmp(pp->m_cat,"DEPRECIATION"  ) ) cashTrans = false;
		if ( !strcmp(t->m_cat,"MADRIDRENTLOSS") ) cashTrans = false;
		if ( cashTrans ) balance += pp->m_amount;
		// skip if not our year
		if ( pp->m_year != accyear ) continue;
		// copy into buf
		char ddbuf[7];
		memcpy ( ddbuf, pp->m_date, 5);
		ddbuf[5] = '\0';
		// null term the date
		//pp->m_date[5] = '\0';
		// get the year
		char ybuf[10];
		sprintf(ybuf,"%li",pp->m_year);

		// get the src account
		char *src = pp->m_srcAcct;
		// fix NULL src'es
		if ( ! src ) src = "xxxxxxxxxxxxxxxx";
		// space pad
		char sbuf[17];
		// get len
		long slen = strlen(src);
		// truncate
		if ( slen >16 ) slen = 16;
		// fill with spaces
		memset ( sbuf , ' ', 16 );
		// copy it into the space-filled buf, remove spaces as we got
		char *dd = sbuf;
		for ( long k = 0 ; k < slen ; k++ ) {
			// skip if space
			if ( !isalnum(src[k]) ) continue;
			*dd++ = tolower(src[k]);
		}
		// null term
		sbuf[16-4] = '\0';
		char *send = sbuf+16-4-4;

		// get the dst account
		char *dst = pp->m_dstAcct;
		// fix NULL src'es
		if ( ! dst ) dst = "xxxxxxxxxxxxxxxx";
		// space pad
		char dbuf[17];
		// get len
		long dlen = strlen(dst);
		// truncate
		if ( dlen >16 ) dlen = 16;
		// fill with spaces
		memset ( dbuf , ' ', 16 );
		// copy it into the space-filled buf, remove spaces as we got
		dd = dbuf;
		for ( long k = 0 ; k < dlen ; k++ ) {
			// skip if space
			if ( !isalnum(dst[k]) ) continue;
			*dd++ = tolower(dst[k]);
		}
		// null term
		dbuf[16] = '\0';

		if ( pp->m_refNum > 999999999999999LL ) { char *xx=NULL;*xx=0; }

		char *desc = pp->m_desc;

		// add checknum to desc
		char desc2[2048];
		if ( pp->m_checkNum >= 0 ) {
			sprintf(desc2,"check #%li %s",pp->m_checkNum,pp->m_desc);
			desc = desc2;
		}

		long width = 15;
		// up to 8 chars of cat
		char *cat = pp->m_cat;
		char  cat2[31];
		// space pad
		if  ( pp->m_amount < 0 ) 
			memcpy ( cat2 , "UNKNOWN        ", width );
		//memset ( cat2 , ' ' , 11 );
		// null term
		if ( cat ) {
			memset ( cat2 , ' ' , width );
			long clen = strlen(cat);
			if ( clen > width ) clen = width;
			memcpy ( cat2 , cat , clen );
		}
		cat2[width] = '\0';
		// print it out
		fprintf ( stdout,
			  "%s/%s "      // date
			  //"%015llu "      // reference number
			  "%+-10.2f "   // amount
			  "%s "         // src acct
			  //"TO "         // directional indicator
			  //"%s ",        // dst acct
			  ,
			  ddbuf,//pp->m_date,
			  ybuf+2, // last 2 digits of year
			  //pp->m_refNum,
			  pp->m_amount,
			  send ); // last 4 digits
			  //dbuf );
		fprintf ( stdout, 
			  "%s "         // category
			  "%11.2f "   // balance
			  "%s\n"        // desc
			  ,
			  cat2,
			  balance,
			  desc );
		// keep stats based on category
		long h = hash32 ( cat2 );
		if ( h == 0 ) { char *xx=NULL;*xx=0; }
		unsigned long n = (unsigned long)h %1024;
		while ( ht[n] && ht[n]!=h )
			if ( ++n >= 1024 ) n = 0;
		if ( ! ht[n] ) { 
			ptr[n] = cat; ht[n] = h; sum[n] =pp->m_amount; 
			continue;
		}
		sum[n] += pp->m_amount;
	}

	fprintf(stdout,"\n");


	// print out general info for CPA/accountant to use
	if ( isgbcorp ) {
		fprintf(stdout,
			"\n"
			"Gigablast, Inc. Stock Ownership Table\n"
			"\n"
			"Percent | Owner           | entity  | ein/ssn\n"
			"-------------------------------------------------\n"
			"~10%%    | Hereuare, Inc.  | C Corp  | 020-57-5232\n"
			"~90%%    | Matt Wells      |         | 287-82-4276\n"
			"\n"
			);
	}

	// print out category descriptions
	fprintf(stdout,"Category           Description\n");
	fprintf(stdout,"-----------------------------------------------------------------------------------\n");
	fprintf(stdout,"%-15s\tHome loan from BofA to matt for his house. The interest\n"
		"\t\tpayments are labelled as BOFAHOMELOANINTEREST.\n","BOFAHOMELOAN");
	fprintf(stdout,"%-15s\tHome loan from Gigablast, Inc. to Matt which he used exclusively\n"
		"\t\tto buy down BOFAHOMELOAN. The idea being: why pay the interest to BofA\n"
		"\t\twhen it can be paid to Gigablast. The interest payments are\n"
		"\t\tlabelled as GBHOMELOANINTEREST.\n","GBHOMELOAN");
	//fprintf(stdout,"%-15s\tShareholder loan from Gigablast, Inc. to Matt used in part to pay\n"
	//	"\t\thome loan. Only the interest on the part of it used to pay the home loan down\n"
	//	"\t\tfrom BOFA was deducted as an expense. The interest payments are labelled as\n"
	//	"\t\tSHARELOANINTEREST.\n","SHARELOAN");
	fprintf(stdout,"%-15s\tShareholder loan from Gigablast, Inc. to Matt.\n"
		"\t\tThe interest payments are labelled as SHARELOANINTEREST.\n","SHARELOAN");
	fprintf(stdout,"%-15s\tLoan from BofA to Gigablast LLC (basically Matt Wells for tax\n"
		"\t\tpurposes) to buy the commercial Bogan bldg which is rented by\n"
		"\t\tGigablast, Inc. The interest payments are labelled as\n"
		"\t\tBOFABOGANLOANINTEREST.\n","BOFABOGANLOAN");
	fprintf(stdout,"%-15s\tSoftware license fees Gigablast, Inc. must pay Matt for his software.\n"
		"\t\tThis loan was not recorded as an asset or a liability in the 2009 and 2010 tax\n"
		"\t\treturns because no principal payments were made and it was not a cash loan.\n"
		"\t\tHowever, the appropriate interest payments were recorded by both parties.\n"
		"\t\tThey were and are labelled as MATTSOFTLICINTEREST.\n","MATTSOFTLIC");
	fprintf(stdout,"%-15s\tLoan from Gigablast, Inc. to Gigablast LLC (basically Matt Wells for\n"
		"\t\ttax purposes) for downpayment of the Bogan bldg rented by Gigablast, Inc. The\n"
		"\t\t2009 and 2010 datasheets had showed this loan increasing from year to year,\n"
		"\t\tbut that was because of a rent overpayment error. The BofA lady had setup the\n"
		"\t\tautomatic payments about twice as much as it should have been so the excess\n"
		"\t\trent was categorized as a loan and the rent payment was adjusted to cover the\n"
		"\t\tinterest of the loan. This error was finally fixed in 2013. The interest is\n"
		"\t\tlabelled as LLCLOANINTEREST.\n","LLCLOAN");
	fprintf(stdout,"%-15s\tRent paid by Gigablast, Inc. to Gigablast LLC (basically Matt Wells\n"
		"\t\tfor tax purposes) for Bogan bldg use.\n","BOGANRENT");
	fprintf(stdout,"%-15s\tRent paid by Jezebel, Inc. to Gigablast, Inc. for building use.\n"
		"\t\tGigablast, Inc. uses that to pay off the CHANALOAN which is interest\n"
		"\t\tonly right now.\n"
		//"\t\tThere might have been an error in the 2009 and 2010 forms claiming a\n"
		//"\t\tdepreciation deduction for this building of $11026.00 for each year that\n"
		//"\t\tmight need to be corrected because Gigablast, Inc. does not have the\n"
		//"\t\tproperty's title because it is a real estate contract.\n",
		,"MADRIDBLDG1RENT");
	fprintf(stdout,"%-15s\tInterest only (for now) loan paid by Gigablast, Inc. to Chana Ben-Dov\n"
		"\t\tfor Madrid bldg 1. Basically, though, Jezebel, Inc., the current tenant, pays this\n"
		"\t\tto Chana directly.\n","CHANALOAN");
	fprintf(stdout,"\n");

	float totaldeduct = 0.0;
	float totalundeduct = 0.0;
	//float totalassets = 0.0;


	// jenavieve and ana
	//if ( ismatt ) {
	//	fprintf(stdout,
	//		"Dependent Name   DOB         SSN\n"
	//		"----------------------------------------\n"
	//		"Jenavieve Wells  12/19/2008  648-50-4544\n"
	//		"Anastasia Wells  03/04/2010  648-54-8063\n"
	//		"\n");
	//}


	// headers
	fprintf(stdout,"category                         amt             deduct      undeduct       taxrate\n");
	fprintf(stdout,"-----------------------------------------------------------------------------------\n");


	// 20% of the shareholder loan was used for personal use, therefor that portion of the
	// interest cannot be deducted
#define SHAREPERSONAL .20

	char bigbuf[100000];
	char *bxp = bigbuf;
	float otherTotal = 0.0; // other deductions

	// print out categories
	for ( long i = 0 ; i < 1024 ; i++ ) {
		if ( ht[i] == 0 ) continue;
		char *cat = ptr[i];
		//char  cat2b[31];
		// space pad
		if ( ! cat ) cat = "UNKNOWN";
		//memset ( cat2b , ' ' , 30 );
		//long clen = strlen(cat);
		//if ( clen > 20 ) clen = 20;
		//memcpy ( cat2b , cat , clen );
		//cat2b[20] = '\0';

		// do not print if zero and IGNORE
		if ( strstr(cat,"IGNORE") && sum[i] == 0.0 ) continue;

		// shortcut
		float pp = sum[i];

		// assets are not expenses
		bool isasset = false;
		if ( !strcmp(cat,"MADRIDPROPERTY") ) isasset=true;
		if ( !strcmp(cat,"STOCKBACK") ) isasset=true;
		// gb corp bought part of matt's home loan from bofa, like $426830.28 worth
		if ( !strcmp(cat,"GBHOMELOAN") ) isasset=true;
		// nwo these are just principal payments since we split them up above
		if ( !strcmp(cat,"BOFAHOMELOAN") ) isasset=true;
		if ( !strcmp(cat,"BOFABOGANLOAN") ) isasset=true;
		// this is loan from gb corp to matt wells, shareholder loan
		if ( !strcmp(cat,"SHARELOAN") ) isasset=true;
		// this is the gb corp loan to matt/gbllc/mattwellsllc for purchasing bogan,
		// but also for an OVERPAYMENTERROR and putting 1k into matt wells llc account
		// to prevent bank fees.
		if ( !strcmp(cat,"LLCLOAN") ) isasset = true;

		float taxrate2 = 42.0;
		if ( !strcmp(cat,"GBSTOCKSALE") ) taxrate2 = 22.0;
		if ( isasset ) taxrate2 = 0.0;

		// how much is deductible?
		float deduct = pp;
		if ( !strcmp(cat,"FOOD") ) deduct *= 0.5;
		if ( !strcmp(cat,"ENTERTAINMENT") ) deduct *= 0.5;
		// part of the car loan is interest and we can deduct that
		if ( !strcmp(cat,"CAREXPENSE") ) deduct *= 0.3;
		// income tax is not tax deductible
		if ( !strcmp(cat,"IRSINCOMETAX") ) deduct = 0.0;
		// personal entertainment
		if ( ismatt && !strcmp(cat,"ENTERTAINMENT") ) deduct = 0.0;
		if ( ismatt && !strcmp(cat,"PERSONAL") ) deduct = 0.0;
		if ( ismatt && !strcmp(cat,"FOOD") ) deduct = 0.0;
		if ( ismatt && !strcmp(cat,"CREDITCARD") ) deduct = 0.0;
		if ( ismatt && !strcmp(cat,"UTILITIES") ) deduct = 0.0;
		if ( ismatt && !strcmp(cat,"CARINSURANCE") ) deduct = pp;
		if ( ismatt && !strcmp(cat,"SUPPLIES") ) deduct = 0.0;
		if ( ismatt && !strcmp(cat,"NEGUNKNOWN") ) deduct = 0.0;
		if ( ismatt && !strcmp(cat,"PETTYCASH") ) deduct = 0.0;
		// 80% of the shareholder loan was used to pay for primary residence
		if ( ismatt && !strcmp(cat,"SHARELOANINTEREST") ) deduct *= (1.0-SHAREPERSONAL);
		if ( pp > 0.0 ) deduct = 0.0;
		if ( isasset      ) deduct = 0.0;
		// accumulate our deductions
		totaldeduct += deduct;
		// store for use below
		expense[i] = deduct;
		// how much is undeductible
		float undeduct = pp - deduct;
		// must be at least negative
		if ( pp > 0.0 ) undeduct = 0.0;
		// assets are not expenses
		if ( isasset ) undeduct = 0.0;


		// . the undeductible portion of expenses
		// . giving out a loan is not an undeductible expense, it is still an asset
		// . by undeductible we mean non-loan non-property "expenses"
		totalundeduct += undeduct;

		fprintf(stdout,"%-30s\t%+-10.02f\t",cat,pp);

		// deductible amt
		if ( deduct != 0.0 ) fprintf(stdout,"%+-10.02f   ",deduct);
		else                 fprintf(stdout,"             ");
		// undeductible amt
		if ( undeduct != 0.0 ) fprintf(stdout,"%+-10.02f",undeduct);
		else                   fprintf(stdout,"          ");
		// tax rate
		if ( pp > 0.0 ) fprintf(stdout,"%10.02f%%",taxrate2);


		// for pasting into attachment for line 26 form 1120
		if ( deduct < 0.0 &&
		     // do not print out interests
		     ! strstr(cat,"INTEREST") &&
		     //nor rents
		     ! strstr(cat,"BOGANRENT") &&
		     // taxes are seprate too
		     ! strstr(cat,"TAXES") &&
		     // nor depreciation
		     ! strstr(cat,"DEPRECI") ) {
			// print into buf
			bxp += sprintf(bxp,"%-30s\t%10.02f\n",cat,-1*deduct);
			otherTotal += -1*deduct;
		}

		// end line
		fprintf(stdout,"\n");

		// add up untaxable income
		if ( taxrate2 == 0.0 && pp > 0.0 )
			untaxableIncome += pp;

		// totals
		if ( pp > 0.0 ) total1 += pp;
		else            total2 += pp;
	}

	// matt pays the prius company car insurance on his home policy
	// so we will take the deduction for that here since matt can't
	// take it.
	// TODO: pro-rate this amount on home insurance as deductible..
	/*
	if ( isgbcorp ) {
		float numMonths = 12;
		// matt started paying the last 5 months in 2009
		if ( accyear == 2009 ) numMonths = 5;
		// gb corp was paying 158.17 with cna, so assume that matt
		// is paying about that with liberty mutual
		float insurance = numMonths * -158.17;
		fprintf(stdout,"CARINSURANCE2           %+-10.02f\t%+-10.02f\n",
			insurance,insurance);
		total2      += insurance;
		totaldeduct += insurance;
	}
	*/

	/*
	// print out categories
	for ( long i = 0 ; i < 1024 ; i++ ) {
		if ( ht[i] == 0 ) continue;
		char *cat = ptr[i];
		char  cat2[31];
		if ( ! cat ) cat = "UNKNOWN";
		long clen = strlen(cat);
		if ( clen > 20 ) clen = 20;
		cat2[clen] = '\0';
		fprintf(stdout,"%s\n",cat);
	}

	// print out values
	for ( long i = 0 ; i < 1024 ; i++ ) {
		if ( ht[i] == 0 ) continue;
		fprintf(stdout,"%+-10.02f\n",sum[i]);
	}
	*/

	fprintf(stdout,"\n");
	fprintf(stdout,"Positive     Total: %+-10.02f\n",total1);
	fprintf(stdout,"Untaxable    Total: %+-10.02f\n",untaxableIncome);
	fprintf(stdout,"Negative     Total: %+-10.02f\n",total2);
	fprintf(stdout,"Deductible   Total: %+-10.02f\n",totaldeduct);
	fprintf(stdout,"Undeductible Total: %+-10.02f\n",totalundeduct);
	//fprintf(stdout,"Assets       Total: %+-10.02f\n",totalassets);
	float taxable = total1 - untaxableIncome + totaldeduct;
	fprintf(stdout,"Taxable      Total: %+-10.02f\n",taxable);
	fprintf(stdout,"\n");
	//fprintf(stdout,"Profit: %+-10.02f\n",total1+total2);
	//fprintf(stdout,"*assumes assets are expenses\n");

	// for attachment
	if ( isgbcorp )
		fprintf(stdout,"\nForm 1120 Line 26\n"
			"Gigablast, Inc. 20-1574374 Year %li\n"
			"Other Deductions\n"
			"================\n"
			"%s\n"
			"TOTAL = %.02f"
			"\n\n\n"
			,g_accyear
			,bigbuf
			,otherTotal
			);

	// sanity, no, loans and property purchases are not expenses!
	// so they will be represented in "total2" but not in 
	// totaldeduct nor totalundeduct...
	//if ( totaldeduct + totalundeduct != total2 )
	//	fprintf(stdout,"Error: %.02f != %.02f\n",totaldeduct+totalundeduct,total2);


	/*
	// from 2008 return, shareholder loan from gb corp to matt with his software as collateral
	float shareloan = 466335.0; 
	// . gb bought off part of matt's home loan in 2009 so matt's interest payments went to it
	// . but this is in our 2009 transaction list so no need to mention it here
	float gbhomeloan = 0.0; // 426830.28; 
	// gb corp loaned gb llc (matt) this much to purchase the bogan prop i guess in 2008 january
	// so we need to mention it here...
	float llcloan = 184156.0;
	// gb owes matt money for using matt's hardware (160k in 2004) and software license.
	// it is basically paying just interest to matt right now.
	float softloan = 0.0; 
	// . madrid loan from chana bendov for unit 1 bldg in madrid
	// . this loan is TO gb corp
	float madridloan = 0.0; if ( isgbcorp ) madridloan = 430000.0;
	*/

	// . property we bought, real estate, computers, ... just the BASIS for ALL years
	if ( g_propStart == 0.0 ) { char *xx=NULL;*xx=0; }

	// select whose account we need
	float cash;
	if ( ismatt ) cash = mattcash;
	else          cash = gbcash;


	// show the loans in a table
	printLoans ( t,nt,g_accyear,ismatt );

	fprintf(stdout,"\n");
	printLoanInterest ( t,nt,g_accyear,ismatt );

	// get total loans from
	float loanFromStart = getLoans2 ( t,nt,g_accyear-1,ismatt,true  );
	float loanFromEnd   = getLoans2 ( t,nt,g_accyear  ,ismatt,true  );
	float loanToStart   = getLoans2 ( t,nt,g_accyear-1,ismatt,false );
	float loanToEnd     = getLoans2 ( t,nt,g_accyear  ,ismatt,false );

	/*
	// total loan amt
	float loanFromTotal = 0.0;
	float loanToTotal   = 0.0;
	if ( isgbcorp ) {
		loanFromTotal = shareloan + gbhomeloan + llcloan ;
		loanToTotal   = softloan + madridloan;
	}
	if ( ismatt   ) {
		loanToTotal   = shareloan + gbhomeloan + llcloan ;
		loanFromTotal = softloan;
	}
	float loanFromStart = loanFromTotal;
	float loanFromEnd   = loanFromTotal;
	float loanToStart   = loanToTotal;
	float loanToEnd     = loanToTotal;
	*/

	float cashStart = cash;
	float cashEnd   = cash;
	// a simple flag for setting loanStart
	bool firstTime = true;
	// compute current cash supply now along with loans
	for ( long i = 0 ; i < nt ; i++ ) {
		// get it
		Trans *t = pt[i];
		// get year of trans
		long tyear = t->m_year;
		// otherwise, ignore if before 2009
		if ( tyear < MINYEAR ) continue;
		// stop if we go beyond our year. transactions are sorted by date.
		if ( tyear > g_accyear ) break;
		// first time? then set starting points
		if ( tyear == g_accyear && firstTime ) {
			cashStart = cash;
			//loanFromStart = loanFromTotal;
			//loanToStart   = loanToTotal;
			firstTime = false;
		}
		// cash is king
		bool cashTrans = true;
		// these are not cash losses
		if ( !strcmp(t->m_cat,"DEPRECIATION"  ) ) cashTrans = false;
		if ( !strcmp(t->m_cat,"MADRIDRENTLOSS") ) cashTrans = false;
		if ( cashTrans ) cash += t->m_amount;
		// mark this in case this was the last trans
		cashEnd = cash;
		
	}
	// subtract liability loans
	float assetsStart = cashStart + g_propStart + g_propDeprStart + loanFromStart ;
	float assetsEnd   = cashEnd   + g_propEnd   + g_propDeprEnd   + loanFromEnd   ;
	// do not count software loan to gb, only cash loans
	if ( ismatt ) {
		assetsStart += loanToStart;
		assetsEnd   += loanToEnd;
	}
	// print out
	fprintf(stdout,
		"Assets           start %li   end %li\n"
		"Cash          : % 11.02f  % 11.02f\n"
		"LoansFrom     : % 11.02f  % 11.02f  (owed to us)\n"
		// do not count this software loan, only count cash loans
		//"LoansTo       : % 11.02f  % 11.02f  (we owe this)\n"
		"PropertyBasis : % 11.02f  % 11.02f\n" 
		"Depreciation  : % 11.02f  % 11.02f\n" 
		"                % 11.02f  % 11.02f\n" ,
		g_accyear,
		g_accyear,
		cashStart,cashEnd,
		loanFromStart,loanFromEnd,
		//loanToStart,loanToEnd,
		g_propStart,g_propEnd ,
		g_propDeprStart,g_propDeprEnd ,
		assetsStart,assetsEnd);

	// sanity
	float assetGain = assetsEnd - assetsStart;
	// must equal taxable income
	float error = taxable - ( assetGain + -1 * totalundeduct );
	// note that
	fprintf(stdout,
		"\n"
		"assetGain = $% 11.02f\n"
		"Error     = $% 11.02f (>0 because of software license \"loans\")\n",
		assetGain,
		error);

	//fprintf(stdout,"\nAttempting to generate pdfs.\n\n");




	// if we are matt list the software license debt 


	// typical irs.gov url format:
	// wget "http://www.irs.gov/pub/irs-prior/f1120--2009.pdf"
	// wget "http://www.irs.gov/pub/irs-pdf/f1120.pdf" (current year)
	// tax tables: 
	// http://www.irs.gov/pub/irs-pdf/i1040tt.pdf

	// . use pdfedit to insert text to fill out the forms
	// . see /usr/share/pdfedit/ directory for a potpourri of scripts to use as
	//   examples
	// . but first define the location of each input box we want to
	//   fill out. 


	/*
	g_sp += sprintf ( g_sp ,
			  "thepage=document.getPage(1);\n"
			  "var fname = getEditText( \"fontface\" );\n"
			  "var fid=thepage.getFontId( fname );\n"
			  "if (fid.isEmpty()) {\n"
			  " thepage.addSystemType1Font( fname );\n"
			  " fid = thepage.getFontId( fname );\n"
			  "}\n"
			  "var fs=getNumber( \"fontsize\" );\n"
			  "var ctm = getDetransformationMatrix( thepage );\n"
			  );
	*/

	// positive = true
	float income   = getPayments(t,nt,g_accyear,g_accyear,ismatt,"INCOME");
	// was about 4k in 2009 reported to us on 1099-g from NM. goes on f1040
	float incometaxrefund = getPayments(t,nt,g_accyear,g_accyear,ismatt,"MATTTAXREFUND");
	// get all interest we made. positive = true.
	float genericinterest = getPayments(t,nt,g_accyear,g_accyear,ismatt,"INTEREST"); // generic bank interest
	float shareinterest = getPayments(t,nt,g_accyear,g_accyear,ismatt,"SHARELOANINTEREST");
	float deductibleshareinterest = shareinterest * (1.0-SHAREPERSONAL);
	float llcinterest = getPayments(t,nt,g_accyear,g_accyear,ismatt,"LLCLOANINTEREST");
	float gbhomeinterest = getPayments(t,nt,g_accyear,g_accyear,ismatt,"GBHOMELOANINTEREST");
	float bofaboganinterest = getPayments(t,nt,g_accyear,g_accyear,ismatt,"BOFABOGANLOANINTEREST");
	float bofahomeinterest = getPayments(t,nt,g_accyear,g_accyear,ismatt,"BOFAHOMELOANINTEREST");
	float mattsoftinterest = getPayments(t,nt,g_accyear,g_accyear,ismatt,"MATTSOFTLICINTEREST");
	float homeproptaxes    = getPayments(t,nt,g_accyear,g_accyear,ismatt,"HOMEPROPTAXES");
	float chanainterest    = getPayments(t,nt,g_accyear,g_accyear,ismatt,"CHANALOANINTEREST");
	float boganinsurance  = getPayments(t,nt,g_accyear,g_accyear,ismatt,"BOGANINSURANCE");
	float chanarentincome  = getPayments(t,nt,g_accyear,g_accyear,ismatt,"MADRIDBLDG1RENT");
	// long term cap gains...


	//
	// we deposited check in 2012, but it was in our trading account! doh!! so count
	// for 2011... 
	// ALSO BE SURE TO IGNORE THEM for 2012 i guess...
	//

	// according to the broker's pdf in the Tax/2011 directory:
	// sold $88,919.07 of stock in nov/dec 2011
	// plus $13,616.59 in sep 2011 (NO! this 13k was for matt personally! and was 13585.79
	// after the broker cut)
	// total for 2011 = 102535.66
	// but i think i deposited all these checks into the bofa accounts in 2012,
	// but just mark it here.
	float longtermcapitalincome = 0.0;
	if ( isgbcorp && g_accyear == 2011 )
		longtermcapitalincome = 88919.07;
	if ( isgbcorp && g_accyear == 2012 ) 
		longtermcapitalincome = 186353.41;
	// we are just re-classifying regular income
	income -= longtermcapitalincome;

	// re-classify
	income -= chanarentincome;


	/*
	// add in transactions in our stock account for soact/maxsound
	if ( isgbcorp ) {
		*/


	float interestincome = 0.0;
	// just add the positive ones
	if ( isgbcorp ) interestincome = genericinterest + shareinterest + llcinterest + gbhomeinterest;
	else            interestincome = genericinterest + mattsoftinterest;
	// and interest expense
	float interestexpense = 0.0;
	if ( isgbcorp ) interestexpense =  mattsoftinterest + chanainterest;
	else  interestexpense =  shareinterest + llcinterest + gbhomeinterest + bofaboganinterest + bofahomeinterest;
	// toal income
	float gbtotalincome = income + interestincome + longtermcapitalincome + chanarentincome;
	// rent for bogan
	float boganrent = getPayments (t,nt,g_accyear,g_accyear,ismatt,"BOGANRENT");
	float rentexpense = 0.0;
	if ( isgbcorp ) rentexpense = boganrent;
	// bogan insurance
	//float boganinsurance = getPayments (t,nt,g_accyear,g_accyear,ismatt,"BOGANINSURANCE");
	//float carinsurance = getPayments (t,nt,g_accyear,g_accyear,ismatt,"CARINSURANCE");
	//float datasvc = getPayments (t,nt,g_accyear,g_accyear,ismatt,"DATASVC");
	//float bankfees = getPayments (t,nt,g_accyear,g_accyear,ismatt,"BANKFEES");
	// capital gains
	float capitalgains = 0.0;
	// matt sold soact stock in 2009 although gb corp deposited the check in its account in 2010
	if ( ismatt && g_accyear == 2009 ) capitalgains = 39183.18;
	// sold soact/maxsound stock here in september of 2011
	//float capitalsalescost = 0.0;
	if ( ismatt && g_accyear == 2011 ) {
		capitalgains = 13585.79;
		//capitalsalescost = 13616.59 - 13585.79;
		// reduce generic income by that so it is not classified there
		income -= 13585.79;
	}

	// taxes other than irs income tax
	float taxexpense   = getPayments (t,nt,g_accyear,g_accyear,ismatt,"TAXES");
	float depreciation = getPayments (t,nt,g_accyear,g_accyear,ismatt,"DEPRECIATION");
	float advertising  = getPayments (t,nt,g_accyear,g_accyear,ismatt,"ADVERTISING");
	float retirement   = getPayments (t,nt,g_accyear,g_accyear,ismatt,"RETIREMENT");
	// make sure this at least picks up gourav so we can get our domestic activities deduction
	float wages        = getPayments (t,nt,g_accyear,g_accyear,ismatt,"PAYROLL");
	// a special deduction, domestic activities deduction
	float rate = .06; if ( g_accyear >= 2010 ) rate = .09;
	float domestic1 = taxable * rate; // before domestic activities deduction
	if ( domestic1 < 0 ) domestic1 = 0.0;
	float domestic2 = -1 * wages * .50;
	float domestic  ;
	if ( domestic1 < domestic2 ) domestic = domestic1;
	else                         domestic = domestic2;
	// must be positive
	if ( domestic < 0 ) { char *xx=NULL;*xx=0; }
	// make negative
	domestic *= -1;
	// all other deductions
	float otherdeduct = 
		totaldeduct - 
		rentexpense - 
		taxexpense - 
		depreciation - 
		advertising - 
		retirement -
		domestic -
		interestexpense -
		wages;
	// . net income (loss) per books is basically:
	//   "Revenues - Expenses + Capital Gains - Non-deducted Expenses"
	//   so i guess that excludes that domestic deduction! so do it up here.
	// . totalundeduct is negative i believe
	float netincomeperbooks = taxable + totalundeduct;
	//
	// deduct that here i guess
	//
	taxable += domestic;
	//
	// compute taxes owed
	//
	float taxrate = 0.15;
	float taxadd  = 0.0;
	float limit   = 0.0;
	float tax     = 0.0;
	float overpayment = 0.0;
	//if ( ismatt && taxable > 0.0 ) {  char *xx=NULL;*xx=0; }
	if ( isgbcorp && taxable > 0.0 ) {
		// lookup tax percentage, seems right for 2009 and 2010...
		if ( taxable > 50000 ) {taxrate = 0.25; taxadd = 7500  ; limit=50000;}
		if ( taxable > 75000 ) {taxrate = 0.34; taxadd = 13750 ; limit=75000;}
		if ( taxable > 100000) {taxrate = 0.39; taxadd = 22250 ; limit=100000;}
		if ( taxable > 335000) {taxrate = 0.34; taxadd = 113900; limit=335000;}
		// subtract
		tax = (taxable - limit) * taxrate + taxadd;
	}
	// gb still has that overpayment from 2008
	if ( isgbcorp ) {
		if ( g_accyear == 2008 ) overpayment = 80019.00;
		if ( g_accyear == 2009 ) overpayment = 78519.00;
		//if ( g_accyear == 2010 ) overpayment = 78519.00;
	}
	if ( ismatt ) {
		// TODO fix me
		fprintf(stdout,"FIX ME 2\n");
		tax = taxable * .30;
	}
	// tax owed
	float owed = tax - overpayment;

	//
	// liabilities
	//
	// . include the software license fees here
	// . basically, 500k/year per month owed to matt
	// . the MATTSOFTLICINTEREST is the interest gb pays on this to matt
	float mattdebtstart  = (g_accyear - 2006    ) * 500000.00;
	float mattdebtend    = (g_accyear - 2006 + 1) * 500000.00;
	float otherDebtStart = mattdebtstart;
	float otherDebtEnd   = mattdebtend;


	// how much matt was compensated
	float mattcomp = 0.0;
	if ( g_accyear == 2008 ) mattcomp = 105557.69;


	if ( ismatt ) {
		//float businessincome = 0.0;
		// we get business income from renting bogan to gb
		//businessincome += boganrent;
		// and from gb for software license. is making interest payments
		// on the principal loan.
		//businessincome += mattsoftinterest;
		// ensure negative!
		if ( llcinterest > 0 ) { char *xx=NULL;*xx=0; }
		if ( bofaboganinterest> 0 ) { char *xx=NULL;*xx=0; }
		//businessincome += llcinterest;
		//businessincome += bofaboganinterest;
		//fprintf(stdout,"12) %.02f (business income or loss)\n",
		//businessincome);

		// . our real estate business
		// . this includes depreciation and interest payments for the
		//   bogan building
		float boganincome = 0.0;
		Prop *p;
		p = getProp ( "BOGAN BLDG" );
		float bogandepreciation = p->m_value / 39.0;
		boganincome += boganrent;
		boganincome -= bogandepreciation;
		boganincome += llcinterest; // llcloaninterest
		boganincome += bofaboganinterest;
		boganincome += boganinsurance;
		float subtotal = 
			interestincome + 
			capitalgains + 
			boganincome ;
		// sanity
		if ( income > 0.0 ) {
			fprintf(stderr,"\nWARNINING unclassified income of %.02f\n"
				"should really call it wages, interest, capital gains, etc.\n"
				,income);
		}

		// adjust gross income
		float agi = subtotal;

		// how does this compare to having a negative business income
		// on line 12?
		// . compute our itemized deductions.
		// . basically our house depreciation and interest payments and
		//   property taxes.
		//p = getProp ( "MATT HOME" );
		//float homedepreciation = p->m_value / 39.0;
		float items = 0.0;
		if ( shareinterest > 0.0 ) { char *xx=NULL;*xx=0; }
		if ( gbhomeinterest > 0.0 ){ char *xx=NULL;*xx=0; }
		if ( bofahomeinterest > 0.0 ){ char *xx=NULL;*xx=0; }
		//if ( homedepreciation < 0.0 ){ char *xx=NULL;*xx=0; }
		if ( homeproptaxes > 0.0 ){ char *xx=NULL;*xx=0; }
		if ( deductibleshareinterest > 0.0 ){ char *xx=NULL;*xx=0; }
		items += -gbhomeinterest; // for home
		items += -bofahomeinterest; // for home
		//items +=  homedepreciation; // this is already positive
		items += -homeproptaxes;
		items += -deductibleshareinterest; // % of shareholder loan
		float exemptions = 3700.0 * 3;
		float taxable = agi - items - exemptions;
		float otherincome = 0.0;

		// nol from 2009
		if ( g_accyear == 2012 ) {
			float nol = 52850.00;
			fprintf(stderr,"\n===== NOL carry forward =====\n");
			fprintf(stderr,"Matthew D. Wells 287-82-4276 "
				"Form 1040 %li\n"
				,g_accyear);
			fprintf(stderr,"Applying %.02f of NOL of %.02f "
				"from 2009 1040\n",
				taxable,
				nol);
			otherincome = taxable;
			taxable -= otherincome;
			subtotal -= otherincome;
			agi   -= otherincome;
		}



		fprintf(stdout,"\n\nForm 1040\n");
		fprintf(stdout,"============================\n");
		fprintf(stdout,"Name  : Matthew D. Wells\n");
		fprintf(stdout,"Street: 808 Sandoval Lane\n");
		fprintf(stdout,"City  : Albuquerque, New Mexico 87004\n");
		fprintf(stdout,"Phone : 505-450-3518\n");
		fprintf(stdout,"SSN   : 287-82-4276\n");
		fprintf(stdout,"\n");
		fprintf(stdout," 1) Single (filing status)\n");
		fprintf(stdout," 4) Yes (head of household)\n");
		fprintf(stdout,"6a) Yes\n");
		fprintf(stdout,"6ci)  Jenavieve Wells  born 12/19/2008  648-50-4544 daughter (under 17)\n");
		fprintf(stdout,"6cii) Anastasia Wells  born 03/04/2010  648-54-8063 daughter (under 17)\n");
		fprintf(stdout,"6d) 1 and 2 and sum of 3 in the box\n");
		fprintf(stdout,"\n");
		// INCOME
		fprintf(stdout,"8a) %.02f (interest income)\n",genericinterest+mattsoftinterest);

		fprintf(stdout,"13) %.02f (capital gains)\n",capitalgains);
		fprintf(stdout,"17) %.02f (rent income)\n",boganincome);

		// put negative NOL here
		fprintf(stdout,"21) %.02f (NOL from 2009 1040)\n",
			-otherincome);
		

		fprintf(stdout,"22) %.02f (total income)\n",subtotal);

		fprintf(stdout,"\n");
		fprintf(stdout,"37) %.02f (adjusted gross income)\n",agi);
		fprintf(stdout,"\n");
		// EXPENSES
		fprintf(stdout,"38) %.02f (repeat)\n",agi);


		fprintf(stdout,"40) %.02f (itemized deductions)\n",items);

		fprintf(stdout,"41) %.02f (subtract itemized)\n",agi - items);
		fprintf(stdout,"42) %.02f (exemptions)\n", exemptions );


		fprintf(stdout,"43) %.02f (taxable income)\n", taxable );

		fprintf(stdout,"\n===== SCHEDULE A Itemized Deductions "
			"======\n");
		fprintf(stdout," 6) %.02f (home prop taxes)\n",
			-homeproptaxes);
		fprintf(stdout,"10) %.02f (bofa home interest on 1098)\n",
			-bofahomeinterest);
		// part of the shareholder loan was for the initial downpayment
		// on matts house. the gbhomeinterest is for gigablast
		// paying down an additional $400k or so.
		fprintf(stdout,"11) %.02f (gb payoff home loan interest)\n",
			-gbhomeinterest);
		fprintf(stdout,"14) %.02f (gb downpay home loan interest)\n",
			-deductibleshareinterest);
		float isum = 
			-homeproptaxes    +
			-bofahomeinterest +
			-gbhomeinterest   +
			-deductibleshareinterest;
		float idiff = isum - items;
		if ( idiff < 0 ) idiff *= -1;
		if ( idiff >= .02 ) {
			fprintf(stderr,"%.02f != %.02f (items)",
				isum,items);
			exit(0);
		}
		fprintf(stdout,"15) %.02f (subtotal)\n",isum + homeproptaxes);
		fprintf(stdout,"29) %.02f (sum)\n",isum);

		
		// money from software license to gigablast inc.
		fprintf(stdout,"\n===== SCHEDULE B Interest Dividends "
			"======\n");
		fprintf(stdout,
			"1) Gigablast, Inc ______  %.02f\n",mattsoftinterest);
		// if we have generic interest...
		if ( genericinterest != 0.0 ) {
			fprintf(stdout,"Savings Account Interest _____ "
				"%.02f\n", genericinterest);
		}
		fprintf(stdout,
			"2) %.02f\n",mattsoftinterest+genericinterest);
		fprintf(stdout,
			"4) %.02f\n",mattsoftinterest+genericinterest);

		if ( capitalgains ) {
			fprintf(stdout,"\n===== SCHEDULE D Capital Gains "
				"======\n");
			if ( g_accyear == 2011 ) {
				fprintf(stdout," 8a) 45000 shares MAXD\n");
				fprintf(stdout," 8b) 2/01/2009 (date acquired)\n");
				fprintf(stdout," 8c) 9/23/2011 (date sold)\n");
				fprintf(stdout," 8d) %.02f (sales price)\n",
					capitalgains);//+capitalsalescost);
				fprintf(stdout," 8e) %.02f (sales cost)\n",0.0);
				//capitalsalescost);
				fprintf(stdout," 8f) %.02f (gain)\n",
					capitalgains);
			}
			fprintf(stdout," 9) %.02f (stock total)\n",
				capitalgains);
			fprintf(stdout,"10) %.02f (subtotal)\n",
				capitalgains);
			fprintf(stdout,"15) %.02f (total)\n",
				capitalgains);
			fprintf(stdout,"16) %.02f (total)\n",
				capitalgains);
		}


		// bogan rent income
		fprintf(stdout,"\n===== SCHEDULE E Supplemental Income "
			"======\n");
		fprintf(stdout,"A) No\n");
		fprintf(stdout,"1A) Commercial Rental\n"
			"Albuquerque NM 87109\n" );
		fprintf(stdout,"3A) %.02f (received rents)\n",boganrent);
		fprintf(stdout,"9A) %.02f (insurance)\n",-1*boganinsurance);
		fprintf(stdout,"12A) %.02f (mortgage interest)\n",
			-1*(llcinterest+bofaboganinterest));
		float bogancost = -1 * ( boganinsurance + 
					 llcinterest + 
					 bofaboganinterest);
		fprintf(stdout,"19) %.02f (cost sum)\n",bogancost);
		fprintf(stdout,"20) %.02f (bogan depreciation)\n",
			bogandepreciation);
		float boganexpense = bogancost + bogandepreciation;
		fprintf(stdout,"21) %.02f (total expenses)\n",boganexpense);
		float boganincome2 = boganrent - boganexpense;
		float diff = boganincome2 - boganincome;
		if ( diff < 0 ) diff *= -1;
		if ( diff > .02 ) {
			fprintf(stdout,"%.02f != %.02f\n",
				boganincome , boganincome2 );
			exit(0);
		}
		fprintf(stdout,"22) %.02f (bogan income)\n",boganincome);
			

	}


	
	if ( isgbcorp ) {

		float usednol = 0.0;

		// nol from 2011
		if ( g_accyear == 2012 ) {
			float nol = 149617.89;
			fprintf(stderr,"\n===== NOL carry forward =====\n");
			fprintf(stdout,"\nForm 1120 \n"
				"Gigablast, Inc. 20-1574374 Year %li\n"
				"Other Deductions\n"
				,g_accyear);
			fprintf(stderr,"Applying %.02f of NOL of %.02f "
				"from 2011 1120\n",
				taxable,
				nol);
			usednol = taxable;

		}


		fprintf(stdout,"\n\nForm 1120\n");
		fprintf(stdout,"============================\n");
		fprintf(stdout,"Name  : Gigablast, Inc\n");
		fprintf(stdout,"Street: 4001 Bogan Ave. NE Bldg A\n");
		fprintf(stdout,"City  : Albuquerque, New Mexico 87109\n");
		fprintf(stdout,"\n");
		fprintf(stdout," B) 20-1574374 (ein)\n");
		fprintf(stdout," C) 9/2/2004 (date incorporated)\n");
		fprintf(stdout," D) %.02f (total assets)\n",assetsEnd);

		fprintf(stdout,"\n");

		//fprintf(stdout,"1a) 0  (merchant card sales)\n");
		fprintf(stdout,"1a) %.02f  (gross receipts or sales/income)\n",income);
		fprintf(stdout,"1c) %.02f  (sum of 1a and 1b)\n",income);
		//fprintf(stdout,"2 ) 0  (costs of goods sold)\n");
		fprintf(stdout,"3 ) %.02f (gross profit)\n",income);
		//fprintf(stdout,"4 ) 0  (dividends)\n");
		fprintf(stdout,"5 ) %.02f  (interest income)\n",interestincome);
		fprintf(stdout,"6 ) %.02f  (gross rents)\n",chanarentincome);
		fprintf(stdout,"8 ) %.02f  (capital gain net income [stock])\n",longtermcapitalincome);
		//fprintf(stdout,"9 ) 0  (net gain or loss)\n");
		//fprintf(stdout,"10) 0  (other income)\n");
		fprintf(stdout,"11) %.02f  (total income)\n",gbtotalincome);

		fprintf(stdout,"\n");

		fprintf(stdout,"16) %.02f (rents)\n",rentexpense*-1);
		fprintf(stdout,"17) %.02f (taxes and licenses)\n",taxexpense*-1);
		fprintf(stdout,"18) %.02f (interest)\n",interestexpense*-1);
		fprintf(stdout,"20) %.02f (depreciation)\n",depreciation*-1);
		fprintf(stdout,"22) %.02f (advertising)\n",advertising*-1);
		fprintf(stdout,"26) %.02f (other deductions)\n",otherdeduct*-1);
		fprintf(stdout,"27) %.02f (total deductions)\n",totaldeduct*-1);

		// totaldeduct is negative
		//float taxable = gbtotalincome + totaldeduct;
		fprintf(stdout,"28) %.02f (taxable income)\n", taxable );

		fprintf(stdout,"\n");

		fprintf(stdout,"30) %.02f (taxable income)\n", owed );

		fprintf(stdout,"31) %.02f (total tax)\n", owed );


		fprintf(stdout,"\n\nSchedule K\n");
		fprintf(stdout," 1) cash method\n");
		fprintf(stdout,"2a) 518112 (business activity code)\n");
		fprintf(stdout,"2b) Service (business activity)\n");
		fprintf(stdout,"2c) Internet Services (product or service)\n");
		fprintf(stdout,"12) %.02f (nol)\n",usednol);
		//fprintf(stdout," 3) No (is subsidiary...?)\n");
		//fprintf(stdout,"4a) No (foreign ownership?)\n");
		//fprintf(stdout,"4b) Yes (individual own > 20%)\n");
		//fprintf(stdout,"5a) No\n");
		//fprintf(stdout," 6) No (pay dividends?)\n");
		


		//fprintf(stdout,"\n\nForm 1120, Schedule L, Line 18\n");
		//fprintf(stdout,"Other Current Liabilities  Start of year    End of year\n");
		//fprintf(stdout,"Software License           %11.02f       %11.02f\n",mattdebtstart,mattdebtend);
		//
		// due from shareholders
		//
		// print out loans to matt and gb llc
		fprintf(stdout,"\n\nSchedule L, Line 7\n");
		float loanmatt1End = getLoanAmt ( "SHARELOAN" , t , nt , 
						  g_accyear, ismatt);
		float loanmatt2End = getLoanAmt ( "GBHOMELOAN" , t , nt , 
						  g_accyear, ismatt);
		float loanllcEnd   = getLoanAmt ( "LLCLOAN" , t , nt , 
						  g_accyear, ismatt);
		float loanmatt1Start = getLoanAmt ( "SHARELOAN" , t , nt , 
						  g_accyear-1, ismatt);
		float loanmatt2Start = getLoanAmt ( "GBHOMELOAN" , t , nt , 
						  g_accyear-1, ismatt);
		float loanllcStart   = getLoanAmt ( "LLCLOAN" , t , nt , 
						  g_accyear-1, ismatt);

		float totalEnd = loanmatt1End + loanmatt2End + loanllcEnd;
		float totalStart=loanmatt1Start+loanmatt2Start+loanllcStart;
		//fprintf(stdout,"Description                Amount\n");
		//fprintf(stdout,"Due from Shareholder     %12.02f  "
		//"(%.02f+%.02f)\n",loanmatt1+loanmatt2,loanmatt1,loanmatt2);
		//fprintf(stdout,"Due from Gigablast LLC   %12.02f\n",loanllc);
		//fprintf(stdout,"\n\n");
		// buildings/depreciable assets
		fprintf(stdout," 1) %.02f | %.02f (cash)\n",
			cashStart,cashEnd);

		// make this shit match mary's return. what did she do?
		float maryLoanFix1 = 0.0;
		float maryLoanFix2 = 0.0;
		if ( g_accyear == 2009 ) {
			//maryLoanFix1 = 650491.0 - 849979.00 ;
			//maryLoanFix2 = 1508461.0 - 1425678.50;
		}
		if ( g_accyear == 2010 ) {
			//maryLoanFix1 = 1508461.0 - 1425678.50;
			//maryLoanFix2 = 1652235.00 - 1534480.88;
		}
		if ( g_accyear >  2010 ) {
			//maryLoanFix1 = 1652235.00 - 1534480.88;
			//maryLoanFix2 = 1652235.00 - 1534480.88;
		}

		fprintf(stdout," 7) %.02f | %.02f (shareholder loans)"
			"(maryfix=%.02f)\n",
			totalStart+maryLoanFix1,
			totalEnd+maryLoanFix2,
			maryLoanFix1);

		float maryAssetFix = 0.0;
		float maryDepFix = 0.0;
		if ( g_accyear >= 2011 ) {
			//maryAssetFix = 1519569.0 - 1400713.88;
			//maryDepFix = 1370895 - 988214.75;
		}

		float propStart  = g_propStart + maryAssetFix;
		float propEnd    = g_propEnd   + maryAssetFix;
		fprintf(stdout,"10a) %.02f | %.02f (depreciable assets)"
			"(maryfix=%.02f)\n",
			propStart,
			propEnd,
			maryAssetFix);

		float dep1 = g_propDeprStart*-1 + maryDepFix;
		float dep2 = g_propDeprEnd  *-1 + maryDepFix;
		fprintf(stdout,
			"10bi) %.02f | %.02f (total asset depreciation)"
			"(maryfix=%.02f)\n",
			dep1,
			dep2,
			maryDepFix
			);

		fprintf(stdout,"10bii) %.02f | %.02f (remaining value)\n",
			propStart - dep1,
			propEnd   - dep2
			);


		float subassets1 = 0.0;
		subassets1 += cashStart ;
		subassets1 += totalStart + maryLoanFix1;
		subassets1 += g_propStart + g_propDeprStart;

		float subassets2 = 0.0;
		subassets2 += cashEnd ;
		subassets2 += totalEnd + maryLoanFix2;
		subassets2 += g_propEnd + g_propDeprEnd;

		fprintf(stdout,"15) %.02f | %.02f (total assets)\n",
			subassets1,subassets2);


		// the loan for the software license
		// it's not a cash loan, maybe list under "other liabilities"?
		//float softLoan1 = getLoanAmt ( "MATTSOFTLIC" , t , nt , g_accyear-1, ismatt);
		//float softLoan2 = getLoanAmt ( "MATTSOFTLIC" , t , nt , g_accyear  , ismatt);
		//fprintf(stdout,"19) %.02f | %.02f (loans from shareholders)\n", 
		//	softLoan1, softLoan2 );


		// common stock
		fprintf(stdout,"22) b) 3500 3500 3500 3500 (common stock)\n");

		// ben bought in
		fprintf(stdout,"23) 1,045,794  | 1,045,795 (additional paid-in capital)\n");
		
		// what is this?
		// retained earning - unappropriated
		float rrr1 = subassets1 - 1045794 - 3500;
		float rrr2 = subassets2 - 1045794 - 3500;
		fprintf(stdout,"25) %.02f | %.02f (retained earnings unappropriated)\n",rrr1,rrr2);

		fprintf(stdout,"28) %.02f | %.02f (total liabilities)\n",subassets1,subassets2);



		fprintf(stdout,"\n\nSchedule M-1\n");
		fprintf(stdout,"1) %.02f (net income per books)\n",subassets2-subassets1);
		fprintf(stdout,"5)c) %.02f (travel and entertainment)\n",
			-1 * ((subassets2-subassets1) - taxable) );
		fprintf(stdout,"6) %.02f (sum)\n",
			taxable );
		fprintf(stdout,"10) %.02f (income)\n",
			taxable );



		fprintf(stdout,"\n\nSchedule M-2\n");
		fprintf(stdout,"1) %.02f (beginning book balance)\n",rrr1);
		fprintf(stdout,"2) %.02f (net income)\n",subassets2-subassets1);
		fprintf(stdout,"4) %.02f (sum)\n",rrr2);
	}











	if ( ismatt ) goto form1040;

	if ( form == 4562 ) goto form4562;

	// reset script
	g_sp = g_script;

	//void addText ( long page , char letter , long n, char *value ) {

	// now fill in the values
	if ( g_accyear == 2009 ) {
		addText  ( 1,"Adress1[0].f1_04_0_[0]", "Gigablast, Inc." );
		addText  ( 1,"Adress1[0].f1_05_0_[0]", "4001 Bogan Ave. Bldg. A" );
		addText  ( 1,"Adress1[0].f1_06_0_[0]", "Albuquerque, New Mexico, 87109" );
		addText  ( 1,"Adress1[0].f1_08_0_[0]", "20-1574374" );
		addText  ( 1,"Adress1[0].f1_09_0_[0]", "9/02/2004");
		addPrice2( 1,"Adress1[0].f1_10_0_[0]", assetsEnd ); // D - total assets
	}
	else {
		addText  ( 1,"Headpage1[0].f1_04_0_[0]", "Gigablast, Inc." );
		addText  ( 1,"Headpage1[0].f1_05_0_[0]", "4001 Bogan Ave. Bldg. A" );
		addText  ( 1,"Headpage1[0].f1_06_0_[0]", "Albuquerque, New Mexico, 87109" );
		addText  ( 1,"f1_08_0_[0]", "20-1574374" );
		addText  ( 1,"f1_09_0_[0]", "9/02/2004");
		addPrice2( 1,"f1_10_0_[0]", assetsEnd ); // D - total assets
	}

	addPrice2( 1,"f1_12_0_[0]", income); // 1a - gross sales
	addPrice2( 1,"f1_14_0_[0]", income); // 1c - balance
	addPrice2( 1,"f1_24_0_[0]", interestincome ); // 5 - interest income
	addPrice2( 1,"f1_36_0_[0]", gbtotalincome ); // 11 - total income
	addPrice2( 1,"f1_40_0_[0]", -1*wages); // 13 - wages
	addPrice2( 1,"f1_46_0_[0]", -1*rentexpense ); // 16 - rent expense
	addPrice2( 1,"f1_48_0_[0]", -1*taxexpense ); // 17 - taxes expense
	addPrice2( 1,"f1_50_0_[0]", -1*interestexpense ); // 18 - interest expense
	addPrice2( 1,"f1_58_0_[0]", -1*depreciation ); // 20 -depreciation
	if ( advertising != 0.0 )
	addPrice2( 1,"f1_62_0_[0]", -1*advertising );// 22 - advertising
	addPrice2( 1,"f1_66_0_[0]", -1*retirement ); // 24 - employee benefits
	if ( domestic != 0.0 )
	addPrice2( 1,"f1_68_0_[0]", -1*domestic ); // - 25 - domestic production deduction
	addPrice3( 1,"f1_56_0_[0]", -1*otherdeduct , "f1_55_0_[0]" ); // 26 - other deductions
	addPrice2( 1,"f1_70_0_[0]", -1*totaldeduct ); // 27 - total deductions
	addPrice2( 1,"f1_72_0_[0]", taxable ); // 28 - taxable income before NOL
	addPrice2( 1,"f1_80_0_[0]", taxable ); // 30 - taxable income
	addPrice2( 1,"f1_82_0_[0]", tax ); // 31 - total tax
	if ( overpayment != 0.0 ) {
		addPrice2( 1,"f1_84_0_[0]", overpayment ); // 32a - overpayment from prev year
		addPrice2( 1,"f1_117_0_[0]",overpayment ); // 32h - overpayment
	}
	addPrice2( 1,"f1_102_0_[0]",owed ); // 34 - taxes owed
	addText  ( 1,"f1_109_0_[0]","CEO"); // 109 - Title of officer
	// page 2
	if ( g_accyear == 2009 ) {
		addText  ( 2,"f2_51_0_[0]","Matt Wells" ); // E1a - officer 1
		addText  ( 2,"f2_52_0_[0]","287-82-4276" );// E1b - officer 1 ssn
		addText  ( 2,"f2_53_0_[0]","10" ); // E1c - officer 1 percent worked
		addText  ( 2,"f2_54_0_[0]","90" ); // E1d - officer 1 percent common stock
		addPrice ( 2,"f2_56_0_[0]", mattcomp ); // E1f - officer 1 compensation
		addPrice ( 2,"f2_81_0_[0]", mattcomp ); // E2 - total officer compensation
		addPrice ( 2,"f2_83_0_[0]", mattcomp ); // E4 - total officer compensation less crap
	}
	else {
		addText  ( 2,"SchETable[0].#subform[1].f2_51_0_[0]","Matt Wells" ); // E1a - officer 1
		addText  ( 2,"SchETable[0].#subform[1].f2_52_0_[0]","287-82-4276" );// E1b - officer 1 ssn
		addText  ( 2,"SchETable[0].#subform[1].f2_53_0_[0]","10" ); // E1c - officer 1 percent worked
		addText  ( 2,"SchETable[0].#subform[1].f2_54_0_[0]","90" ); // E1d - officer 1 percent common stock
		addPrice ( 2,"SchETable[0].#subform[1].f2_56_0_[0]", mattcomp ); // E1f - officer 1 compensation
		addPrice ( 2,"SchETable[0].#subform[1].f2_81_0_[0]", mattcomp ); // E2 - total officer compensation
		addPrice ( 2,"SchETable[0].#subform[1].f2_83_0_[0]", mattcomp ); // E4 - total officer compensation less crap
	}
	// f1120 - page 3 - schedule j
	addPrice2( 3,"f3_11_0_[0]",owed); // J2 - income tax
	addPrice2( 3,"f3_15_0_[0]",owed); // J4 - income tax
	addPrice3( 3,"f3_02_0_[0]",owed,"f3_33_0_[0]"); // J7- income tax  (stupid labeling!)
	addPrice2( 3,"f3_38_0_[0]",owed);// J10 - income tax
	// f1120 - page 3 - schedule k
	addCheck ( 3,"c3_09[0]", "Yes" ); // K1a - cash method (c3_09[0])
	addText  ( 3,"f3_41_0_[0]","518112"); // K2a - biz code
	addText  ( 3,"f3_42_0_[0]","SERVICE"); // k2b - biz activity
	addText  ( 3,"f3_43_0_[0]","INTERNET PORTAL"); // k2c - service
	addCheck ( 3,"c3_21_0_[1]", "No" ); // K3 - No
	addCheck ( 3,"c3_26_0_[1]", "No" ); // K4a - No
	addCheck ( 3,"c3_27_0_[0]", "Yes" ); // K4b - Yes
	addCheck ( 3,"c4_01_0_[1]", "No" ); // K5a - No
	// f1120 - page 4 - schedule k
	addCheck ( 4,"c4_02_0_[1]", "No" ); // K5b - No
	addCheck ( 4,"c4_03_0_[1]", "No" ); // K6 - No
	addCheck ( 4,"c4_04_0_[1]", "No" ); // K7 - No
	addText  ( 4,"f4_61_0_[0]","2"); // K10 - 2 shareholders
	addText  ( 4,"c4_05_0_[1]", "No" ); // K13 - No

	// f1120 - page5 - schedule L
	if ( g_accyear == 2009 ) {
		addPrice ( 5,"f5_001_0_[0]",cashStart);
		addPrice ( 5,"f5_002_0_[0]", cashEnd);
		addPrice ( 5,"f5_017_0_[0]",loanFromStart);
		addPrice ( 5,"f5_018_0_[0]",loanFromEnd);
		addPrice ( 5,"f5_023_0_[0]",g_propStart);
		addPrice ( 5,"f5_024_0_[0]",g_propEnd);
		addPrice ( 5,"f5_025_0_[0]",-1*g_propDeprStart);
		addPrice ( 5,"f5_027_0_[0]",-1*g_propDeprEnd);
		addPrice ( 5,"f5_026_0_[0]",g_propStart + g_propDeprStart);
		addPrice ( 5,"f5_028_0_[0]",g_propEnd   + g_propDeprEnd);
		addPrice ( 5,"f5_045_0_[0]",assetsStart);
		addPrice ( 5,"f5_046_0_[0]",assetsEnd);
		// liabilities
		addPrice ( 5,"f5_051_0_[0]",otherDebtStart);// other current liabilities
		addPrice ( 5,"f5_051_0_[0]",otherDebtEnd);
		addText  ( 5,"f5_061_0_[0]","3500"); // capital common stock
		addText  ( 5,"f5_062_0_[0]","3500"); // capital common stock
		addText  ( 5,"f5_063_0_[0]","3500"); // capital common stock
		addText  ( 5,"f5_064_0_[0]","3500"); // capital common stock
		addText  ( 5,"f5_065_0_[0]","1045794.00"); // addition paid-in capital start
		addText  ( 5,"f5_066_0_[0]","1045794.00"); // addition paid-in capital end
		
		addPrice ( 5,"f5_075_0_[0]",otherDebtStart+3500+1045794); // total liable start
		addPrice ( 5,"f5_076_0_[0]",otherDebtEnd+3500+1045794); // total liable end
		
		addPrice ( 5,"f5_077_0_[0]", netincomeperbooks); // M-1 #1 net icnome books
		addPrice ( 5,"f5_078_0_[0]", tax ); // M-1 #2 taxperbooks
		addPrice ( 5,"f5_088_0_[0]", netincomeperbooks + tax ); // M-1 #6 sum
		addPrice ( 5,"f5_097_0_[0]", domestic ); // M-1 #8
		addPrice ( 5,"f5_098_0_[0]", domestic ); // M-1 #9
		addPrice ( 5,"f5_099_0_[0]", netincomeperbooks + tax - domestic ); // M-1 #10
		addPrice ( 5,"f5_101_0_[0]", netincomeperbooks ); // M-2 #2
		addPrice ( 5,"f5_106_0_[0]", netincomeperbooks ); // M-2 #4
		addPrice ( 5,"f5_113_0_[0]", netincomeperbooks ); // M-2 #8
	}
	// 2010+
	else if ( g_accyear >= 2010 ) {
		addPrice ( 5,"SchLTable[0].Line1[0].f5_001_0_[0]",cashStart);
		addPrice ( 5,"SchLTable[0].Line1[0].f5_002_0_[0]", cashEnd);
		addPrice ( 5,"SchLTable[0].Line7[0].f5_017_0_[0]",loanFromStart);
		addPrice ( 5,"SchLTable[0].Line7[0].f5_018_0_[0]",loanFromEnd);
		addPrice ( 5,"SchLTable[0].Line10a[0].f5_023_0_[0]",g_propStart);
		addPrice ( 5,"SchLTable[0].Line10a[0].f5_024_0_[0]",g_propEnd);
		addPrice ( 5,"SchLTable[0].Line10b[0].f5_025_0_[0]",-1*g_propDeprStart);
		addPrice ( 5,"SchLTable[0].Line10b[0].f5_027_0_[0]",-1*g_propDeprEnd);
		addPrice ( 5,"SchLTable[0].Line10b[0].f5_026_0_[0]",g_propStart + g_propDeprStart);
		addPrice ( 5,"SchLTable[0].Line10b[0].f5_028_0_[0]",g_propEnd   + g_propDeprEnd);
		addPrice ( 5,"SchLTable[0].Line15[0].f5_045_0_[0]",assetsStart);
		addPrice ( 5,"SchLTable[0].Line15[0].f5_046_0_[0]",assetsEnd);
		//
		// liabilities
		//
		addPrice ( 5,"SchLTable[0].Line18[0].f5_051_0_[0]",otherDebtStart);// other current liabilities
		addPrice ( 5,"SchLTable[0].Line18[0].f5_051_0_[0]",otherDebtEnd);
		addText  ( 5,"SchLTable[0].Line22b[0].f5_061_0_[0]","3500"); // capital common stock
		addText  ( 5,"SchLTable[0].Line22b[0].f5_062_0_[0]","3500"); // capital common stock
		addText  ( 5,"SchLTable[0].Line22b[0].f5_063_0_[0]","3500"); // capital common stock
		addText  ( 5,"SchLTable[0].Line22b[0].f5_064_0_[0]","3500"); // capital common stock
		addText  ( 5,"SchLTable[0].Line23[0].f5_065_0_[0]","1045794.00"); // addition paid-in capital start
		addText  ( 5,"SchLTable[0].Line23[0].f5_066_0_[0]","1045794.00"); // addition paid-in capital end
		
		addPrice ( 5,"SchLTable[0].Line28[0].f5_075_0_[0]",3500+1045794+otherDebtStart); // total liable start
		addPrice ( 5,"SchLTable[0].Line28[0].f5_076_0_[0]",3500+1045794+otherDebtEnd); // total liable end
		
		addPrice ( 5,"SchM-1_Left[0].f5_077_0_[0]", netincomeperbooks); // M-1 #1 net icnome books
		addPrice ( 5,"SchM-1_Left[0].f5_078_0_[0]", tax ); // M-1 #2 taxperbooks
		addPrice ( 5,"SchM-1_Left[0].f5_088_0_[0]", netincomeperbooks + tax ); // M-1 #6 sum
		addPrice ( 5,"SchM-1_Right[0].f5_097_0_[0]", domestic ); // M-1 #8
		addPrice ( 5,"SchM-1_Right[0].f5_098_0_[0]", domestic ); // M-1 #9
		addPrice ( 5,"SchM-1_Right[0].f5_099_0_[0]", netincomeperbooks + tax - domestic ); // M-1 #10
		addPrice ( 5,"SchM-1_Left[0].f5_101_0_[0]", netincomeperbooks ); // M-2 #2
		addPrice ( 5,"SchM-2_Left[0].f5_106_0_[0]", netincomeperbooks ); // M-2 #4
		addPrice ( 5,"SchM-2_Left[0].f5_113_0_[0]", netincomeperbooks ); // M-2 #8
	}

	///////////
	//
	// 1121 is 1120 accompaniments
	//
	//////////
	//form1121:
	{
		// print into txt file for now
		char fname[245];
		FILE *fd;
		//sprintf(fname,"./%li/f1121--%li.txt",g_accyear,g_accyear);
		sprintf(fname,"./f1121--%li.txt",g_accyear);
		fd = fopen ( fname , "w" );
		if ( ! fd ) { fprintf(stdout,"could not open %s for writing\n",fname); return -1; }
		// print title
		fprintf(fd,"GIGABLAST INC 20-1574374\n");
		fprintf(fd,"Form 1120, Page 1, Line 26, Other Deductions Statement\n");
		float sum = 0;
		//
		// print other expenses
		//
		for ( long i = 0 ; i < 1024 ; i++ ) {
			if ( ht[i] == 0 ) continue;
			if ( expense[i] >= 0.0 ) continue;
			char *cat = ptr[i];
			//if ( !strcmp(cat,"NEGUNKNOWN") ) cat = "PROFESSIONAL";
			if ( !strcmp(cat,"FURNITURE") ) cat = "COMPUTEREQUIP";
			if ( !strcmp(cat,"SOACTSTOCK") ) cat = "SOACTSTOCKTRANSFER";
			if ( !strcmp(cat,"FOOD") ) cat = "FOOD(50%)";
			if ( !strcmp(cat,"ENTERTAINMENT") ) cat = "ENTERTAINMENT(50%)";
			if ( !strcmp(cat,"MATTSOFTLICINTEREST") ) cat = "SOFTWARELICINTEREST";
			fprintf(fd,"%-30s %12.02f\n",cat,expense[i]);
			sum += expense[i];
		}
		fprintf(fd,"%-30s %12.02f\n","TOTAL",sum);
		//
		// other liabilities table
		//
		fprintf(fd,"\n\nForm 1120, Schedule L, Line 18\n");
		fprintf(fd,"Other Current Liabilities  Start of year    End of year\n");
		fprintf(fd,"Software License           %11.02f       %11.02f\n",mattdebtstart,mattdebtend);
		//
		// due from shareholders
		//
		// print out loans to matt and gb llc
		fprintf(fd,"\n\nForm 1120, Schedule L, Line 7\n");
		float loanmatt1 = getLoanAmt ( "SHARELOAN" , t , nt , g_accyear, ismatt);
		float loanmatt2 = getLoanAmt ( "GBHOMELOAN" , t , nt , g_accyear, ismatt);
		float loanllc   = getLoanAmt ( "LLCLOAN" , t , nt , g_accyear, ismatt);
		fprintf(fd,"Description                Amount\n");
		fprintf(fd,"Due from Shareholder     %12.02f\n",loanmatt1+loanmatt2);
		fprintf(fd,"Due from Gigablast LLC   %12.02f\n",loanllc);
		//
		// buildings and other depreciable assets
		//
		fprintf(fd,"\n\nForm 1120, Schedule L, line 10a, Buildings and other depreciable assets\n");
		//fprintf(stdout,"\n\nearly exit!!!!\n");
		//exit(0);
		//printDepreciationTable ( fd , ismatt, t, nt );

		fclose(fd);
	}


	// skip1:


	// do pdfedit on this file for this year
	//runScript ( g_script , accyear , "f1120" );

	////////////
	//
	// form 4562 - depreciation
	//
	////////////
form4562:

	//filloutForm4562 ( ismatt );

	///////////
	//
	// 4563 is 4562 accompaniments
	//
	//////////
	//form4563:
	{
		// print into txt file for now
		//char fname[245];
		//FILE *fd;
		//sprintf(fname,"./f4563--%li.txt",g_accyear);
		//fd = fopen ( fname , "w" );
		//if ( ! fd ) { fprintf(stdout,"could not open %s for writing\n",fname); return -1; }
		// print title
		//fprintf(fd,"GIGABLAST INC 20-1574374\n");
		fprintf(stdout,"\n");
		if ( ismatt )
			fprintf(stdout,"Form 4562, Page 1, Line 20\n");
		else
			fprintf(stdout,"Form 1120, Schedule L, line 10a, "
				"Buildings and other depreciable assets\n");
		//
		// buildings and other depreciable assets
		//
		printDepreciationTable ( stdout , ismatt, t, nt );
		//fclose(fd);
	}










	if ( isgbcorp ) return 0;

 form1040:

	{
		// print into txt file for now
		//char fname[245];
		//FILE *fd;
		//sprintf(fname,"./f1041--%li.txt",g_accyear);
		//fd = fopen ( fname , "w" );
		//if ( ! fd ) { fprintf(stdout,"could not open %s for writing\n",fname); return -1; }
		// print title
		//fprintf(fd,"MATT WELLS 287-82-4276\n");
		//fprintf(fd,"Depreciation Table\n");
		fprintf(stdout,"\n");
		//fprintf(stdout,"MATT WELLS 287-82-4276\n");
		fprintf(stdout,"Depreciation Table\n");
		//
		// buildings and other depreciable assets
		//
		printDepreciationTable ( stdout , ismatt, t, nt );
		//fclose(fd);
	}


	g_sp = g_script;

	long numDependents = 0;
	if ( g_accyear >= 2009 ) numDependents++; // jenavieve was born
	if ( g_accyear >= 2010 ) numDependents++; // anna was born
	// sanities - these should all be negative (stuff we paid)
	if ( shareinterest    > 0 ) { char *xx=NULL;*xx=0;}
	if ( gbhomeinterest   > 0 ) { char *xx=NULL;*xx=0;}
	if ( bofahomeinterest > 0 ) { char *xx=NULL;*xx=0;}
	if ( totaldeduct      > 0 ) { char *xx=NULL;*xx=0;}
	// and these positive
	if ( boganrent        < 0 ) { char *xx=NULL;*xx=0;}
	if ( genericinterest  < 0 ) { char *xx=NULL;*xx=0;}
	// shareholderloans are 80% used to pay off the bank loan
	float matthomeinterestexpense  = (1.0-SHAREPERSONAL) * shareinterest + gbhomeinterest + bofahomeinterest;
	//float mattotherinterestexpense =     SHAREPERSONAL  * shareinterest;
	// separate these expenses out from the business expenses since they are personal deductions
	float mattexpenses             = -1 * (matthomeinterestexpense + homeproptaxes);
	float mattbusinessrevenue      = boganrent + genericinterest + mattsoftinterest ;
	// remove personal expenses from the total expenses to get the business expenses
	float mattbusinessexpense      = totaldeduct  + mattexpenses;
	float mattbusinessincome       = mattbusinessrevenue + mattbusinessexpense;
	float mattincome               = mattbusinessincome + capitalgains + incometaxrefund;
	float matttaxable              = mattincome - mattexpenses;
	float exemptions               = 3650.0 * (float)numDependents;
	float matttaxableafter         = matttaxable - exemptions;
	if ( matttaxableafter < 0.0 ) matttaxableafter = 0.0;
	float matttax                  = .25 * matttaxableafter;
	float childcarecost = 0.0;
	if      ( g_accyear == 2010 ) { childcarecost =  600.00 * 12.0; }
	else if ( g_accyear > 2010  ) { childcarecost = 1200.00 * 12.0; }
	float totalcredits = 0; // childcarecost; 	// we need wages to get this! (earned income)
	float newtax = matttax - totalcredits;
	if ( newtax < 0.0 ) newtax = 0.0;

	/*
	addText   ( 1,"p1-t4[0]","Matthew D.");
	addText   ( 1,"p1-t5[0]","Wells");
	addText   ( 1,"p1-t8[0]","4001 Bogan Ave NE Bldg A");
	addText   ( 1,"p1-t10[0]","Albuquerque, New Mexico  87109");
	addText   ( 1,"p1-t11[0]","287824276");
	if ( g_accyear == 2009 ) addCheck0 ( "c1_04", "HoH" );  // head of household
	else                     addCheck  ( 1,"c1_04", "HoH" );  // head of household
	if ( g_accyear >= 2009 ) {
		addText   ( 1,"Line6cTable[0].#subform[1].p1-t20[0]", "Jenavieve Wells");
		addText   ( 1,"Line6cTable[0].#subform[1].p1-t21[0]", "648504544");
		addText   ( 1,"Line6cTable[0].#subform[1].p1-t24[0]", "daughter");
		addCheck  ( 1,"Line6cTable[0].#subform[1].c1_07[0]", "1");
	}
	if ( g_accyear >= 2010 ) {
		addText   ( 1,"Line6cTable[0].#subform[2].p1-t25[0]", "Anastasia Wells");
		addText   ( 1,"Line6cTable[0].#subform[2].p1-t26[0]", "");
		addText   ( 1,"Line6cTable[0].#subform[2].p1-t30[0]", "daughter");
		addCheck  ( 1,"Line6cTable[0].#subform[2].c1_08[0]", "1");
		addCheck0 ( "c2_27", "Yes" );
	}
	char ds[5];
	sprintf (ds ,"%li",numDependents);
	addText   ( 1,"p1-t19[0]",ds); // num dependents
	addText   ( 1,"p1-t42[0]",ds); // num dependents lived with me
	addText   ( 1,"p1-t45[0]",ds); // num dependents total
	addPrice3 ( 1,"p1-t56[0]",incometaxrefund,"p1-t57[0]"         ); // line 10 - INCOME tax refunds
	addPrice3 ( 1,"p1-t60[0]",mattbusinessincome,"p1-t61[0]"); // line 12 - business income
	if ( capitalgains != 0.0 )
	addPrice3 ( 1,"p1-t62[0]",capitalgains,"p1-t63[0]"      ); // line 13 - capital gains
	addPrice3 ( 1,"p1-t87[0]",mattincome,"p1-t88[0]"        ); // line 22 - total income
	addPrice3 ( 1,"p1-t120[0]", mattincome,"p1-t121[0]"     ); // line 37 - adjusted gross income
	addPrice3 ( 1,"p2-t1[0]"  , mattincome, "p2-t2[0]"      ); // line 38 - adjusted gross income
	addPrice3 ( 2,"p2-t4[0]"  , mattexpenses , "p2-t5[0]" ); // line 40a - itemzied deductions
	addPrice3 ( 2,"p2-t6[0]"  , matttaxable , "p2-t7[0]"    ); // line 41 - taxable income
	// line 42 has a special exception if income is > than 125100... so just in case core
	if ( mattincome > 125100 ) fprintf(stdout,"FIXME!\n");
	if ( exemptions != 0.0 ) addPrice3 ( 2,"p2-t8[0]" , exemptions , "p2-t9[0]" ); // line 42 - exemptions
	addPrice3 ( 2,"p2-t10[0]",matttaxableafter,"p2-t11[0]"  ); // line 43 - tax after exmptions
	addPrice3 ( 2,"p2-t12[0]",matttax,"p2-t13[0]"           ); // line 44 - tax
	addPrice3 ( 2,"p2-t16[0]",matttax,"p2-t17[0]"           ); // line 46 - Alt Min Tax
	//addPrice3 ( 2,"p2-t20[0]", childcarecost, "p2-t21[0]"   ); // line 48
	// TODO: fill in "child tax credit"? up to $1000 per child
	addPrice3 (2,"p2-t37[0]", totalcredits, "p2-t38[0]"     ); // line 54
	addPrice3 (2,"p2-t39[0]",newtax,"p2-t40[0]"             ); // line 55
	addPrice3 (2,"p2-t49[0]",newtax,"p2-t50[0]"             ); // line 60 - total tax
	addPrice3 (2,"p2-t107[0]",newtax,"p2-t108[0]"           );
	addText   (2,"p2-t119[0]","Software Engineer/CEO"       );
	addText   (2,"p2-t121[0]","505-450-3518"                );
	// write f1040.fdf file
	runScript ( g_script , accyear , "f1040" );



	/////////////////
	//
	// 1040 schedula a
	//
	//////////////////

	// reset print buffer for writing to the fdf file
	g_sp = g_script;
	//
	addText   (1,"p1-t1[0]","Matthew D. Wells");
	addText   (1,"p1-t2[0]","287-82-4276");
	addPrice3 (1,"p1-t15[0]",-1*homeproptaxes,"p1-t16[0]" );
	addPrice3 (1,"p1-t23[0]",-1*homeproptaxes,"p1-t24[0]" );
	addPrice3 (1,"p1-t25[0]",-1*bofahomeinterest,"p1-t26[0]" );
	addPrice3 (1,"p1-t29[0]",-1*matthomeinterestexpense-(-1*bofahomeinterest),"p1-t30[0]" );
	addPrice3 (1,"p1-t37[0]",-1*matthomeinterestexpense,"p1-t38[0]" );
	addPrice3 (1,"p1-t71[0]",mattexpenses,"p1-t72[0]" );
	// this sux because it puts a limit on our deductible expenses
	if ( mattincome > 166800 ) { char *xx=NULL;*xx=0; }
	// check the "No" box. we are not over 166800...
	addCheck0 ("cb3","no");
	// write f1040sa.fdf file
	runScript ( g_script , accyear , "f1040sa" );



	/////////////////
	//
	// 1040 schedula c
	//
	//////////////////

	// reset print buffer for writing to the fdf file
	g_sp = g_script;
	//
	addText   (1,"p1-t1[0]","Matthew D. Wells");
	addText   (1,"p1-t2[0]","287-82-4276");
	addText   (1,"p1-t5[0]","RENTAL PROPERTIES");
	addText   (1,"p1-t12[0]","GIGABLAST LLC");
	addText   (1,"p1-t6[0]","531100");
	addText   (1,"p1-t22[0]","4001 Bogan Ave. NE Bldg A");
	addText   (1,"p1-t23[0]","Albuquerque, New Mexico 87109");
	addCheck  (1,"p1-cb1[0]","0"); // cash method of accounting
	if ( g_accyear == 2009 )   addCheck0 ( "cb4","Yes"); // line G - yes we materially participated
	else                       addCheck (1,"p1-cb4[0]","Yes"); 
	addPrice3 (1,"p1-t25[0]",mattbusinessincome,"p1-t32[0]" ); // line 1 - gross receipts
	addPrice3 (1,"p1-t29[0]",mattbusinessincome,"p1-t36[0]" ); // line 5 - gross profit
	addPrice3 (1,"p1-t31[0]",mattbusinessincome,"p1-t38[0]" ); // line 7 - gross income
	addPrice3 (1,"p1-t44[0]",-1*depreciation,"p1-t55[0]" ); // line 13 - bogan depreciation
	// we paid insurance on gigablast inc.s' company car
	addPrice3 (1,"p1-t46[0]",-1*(boganinsurance+carinsurance),"p1-t57[0]" ); 
	addPrice3 (1,"p1-t47[0]",-1*(bofaboganinterest+llcinterest),"p1-t58[0]" );
	addPrice3 (1,"p1-t49[0]",-1*(datasvc+bankfees),"p1-t60[0]" ); // line 17 professional
	addPrice3 (1,"p1-t73[0]",-1*mattbusinessexpense,"p1-t89[0]" ); // line 28 - total expenses
	addPrice3 (1,"p1-t74[0]",matttaxable,"p1-t90[0]"); // line 29 - tentative profile or loss
	addPrice3 (1,"p1-t76[0]",matttaxable,"p1-t92[0]"); // line 31 - net profit or loss
	addCheck  (1,"p1-cb8[0]", "1" ); // line 32a - all investment is at risk
	// sanity - make sure we did not claim an expense that we did not list!
	float expsum = datasvc+bankfees+boganinsurance+carinsurance+bofaboganinterest+llcinterest+depreciation;
	if ( (long)(expsum+0.5) != (long)(mattbusinessexpense+0.5) ) { char *xx=NULL;*xx=0; }
	// write f1040sc.fdf file
	runScript ( g_script , accyear , "f1040sc" );



	/////////////////
	//
	// 1040 schedula d
	//
	//////////////////

	// sold 39k of soact stock in oct/nov 2009
	if ( g_accyear == 2009 ) {
		// reset print buffer for writing to the fdf file
		g_sp = g_script;
		float shorttermcapgains = capitalgains;
		addText   (1,"p1-t1[0]","Matthew D. Wells");
		addText   (1,"p1-t2[0]","287-82-4276");
		addText   (1,"p1-t5[0]","35,000 sh. SOACT");
		addText   (1,"p1-t6[0]","feb 2009");
		addText   (1,"p1-t7[0]","oct 2009");
		addPrice3 (1,"p1-t8[0]" ,shorttermcapgains,"p1-t9[0]"); // line 1d
		addPrice3 (1,"p1-t12[0]",shorttermcapgains,"p1-t13[0]"); // line 1f
		addPrice3 (1,"p1-t50[0]",shorttermcapgains,"p1-t51[0]"); // line 2
		addPrice3 (1,"p1-t52[0]",shorttermcapgains,"p1-t53[0]"); // line 2 ?
		addPrice3 (1,"p1-t54[0]",shorttermcapgains,"p1-t125[0]"); // line 3
		addPrice3 (1,"p1-t61[0]",shorttermcapgains,"p1-t62[0]"); // line 7
		addPrice3 (1,"p2-t1[0]" ,shorttermcapgains,"p2-t2[0]"); // line 16
		addCheck0 (  "cb6","No"); // do you have qualified dividends?
		// write f1040sd.fdf file
		runScript ( g_script , accyear , "f1040sd" );
	}
	*/

	/*
	////////////////////
	//
	// form 2441 day care
	//
	///////////////////
	if ( childcarecost > 0.0 ) {
		// reset print buffer for writing to the fdf file
		g_sp = g_script;
		addText   (1,"f1_01_0_[0]","Matthew D. Wells");
		addText   (1,"f1_02_0_[0]","287-82-4276");
		addText   (1,"Line1Table[0].#subform[1].f1_05_0[0]", "La Petit Academy" );
		addText   (1,"Line1Table[0].#subform[1].#subform[2].f1_06_0[0]", "Bernalillo, NM" );
		addPrice3 (1,
			   "Line1Table[0].#subform[1].f1_08_0[0]", childcarecost,
			   "Line1Table[0].#subform[1].f1_09_0[0]" );
		// jena
		addText   (1,"Line2Table[0].#subform[1].f1_17_0_[0]","Jenavieve");
		addText   (1,"Line2Table[0].#subform[1].f1_18_0_[0]","Wells");
		addText   (1,"Line2Table[0].#subform[1].f1_19_0_[0]","648504544");
		addPrice3 (1,
			   "Line2Table[0].#subform[1].f1_22_0[0]", 600*12,
			   "Line2Table[0].#subform[1].f1_23_0[0]" );
		// anna
		addText   (1,"Line2Table[0].#subform[2].f1_24_0_[0]","Anastasia");
		addText   (1,"Line2Table[0].#subform[2].f1_25_0_[0]","Wells");
		//addText   (1,"Line2Table[0].#subform[2].f1_26_0_[0]","");
		addPrice3 (1,
			   "Line2Table[0].#subform[2].f1_29_0[0]", 600*12,
			   "Line2Table[0].#subform[2].f1_30_0[0]" );
		addPrice3 (1,"f1_31_0_[0]",childcare,"f1_32_0_[0]"); // line 3 - total childcare
		addPrice3 (1,"f1_31_0_[0]",xxx,"f1_32_0_[0]"); // line 4 - earned income

		addPrice3 (1,"f1_39_0_[0]",mattincome,"f1_40_0_[0]"); // line 7 - matt's adjusted gross income
		addText   (1,"f1_42_0_[0]",".20"); // line 8 - maps agi to percent. for us .20.
	}
	*/	


	return 0;
}


void filloutForm4562 ( bool ismatt ) {

	// reset script
	g_sp = g_script;

	// total properties
	long np = sizeof(g_prop)/sizeof(Prop);

	// header info
	addTextSimple ( 1,1, "GIGABLAST, INC.");
	addTextSimple ( 1,2 , "20-1574374" );
	addTextSimple ( 1,3, "Form 1120 Line 20");

	// property 179'd this year. (so much depreciation can be expensed)
	float expensedtotal = 0.0;
	float costtotal     = 0.0;
	for ( long i = 0 ; i < np ; i++ ) {
		// shortcut
		Prop *p = &g_prop[i];
		// must match
		if ( p->m_ismatt != ismatt ) continue;
		// year placed in service
		long propyear = atoi(p->m_datePlacedInService+6);
		// skip if new this year
		if ( propyear != g_accyear ) continue;
		// get 179'd amount
		float expensed = p->m_value - p->m_basis;
		// skip if none expensed! not 179 property then
		if ( expensed == 0.0 ) continue;
		// add that up
		expensedtotal += expensed;
		costtotal     += p->m_value;
	}
	// print total cost of new prop
	if ( costtotal != 0.0 ) {
		//addText  ( "4562box1", "$125,000" );
		addPriceSimple ( 1,4, costtotal ); // #2 - total cost of section 179 prop
		addTextSimple  ( 1,6, "$125,000" ); // #5 - dollar limitation
		// you can only expense 125k of it! so fix g_prop[] table!
		if ( expensedtotal > 125000 ) { char *xx=NULL;*xx=0; }
	}
	// list the 179'd properties
	for ( long i = 0 ; i < np ; i++ ) {
		// shortcut
		Prop *p = &g_prop[i];
		// must match
		if ( p->m_ismatt != ismatt ) continue;
		// year placed in service
		long propyear = atoi(p->m_datePlacedInService+6);
		// skip if new this year
		if ( propyear == g_accyear ) continue;
		// get 179'd amount
		float expensed = p->m_value - p->m_basis;
		// skip if none expensed! not 179 property then
		if ( expensed == 0.0 ) continue;
		// print in table
		addTextSimple  ( 1,7, p->m_desc ); // 6a row1
		addPriceSimple ( 1,8, p->m_value ); // 6b row1
		addPriceSimple ( 1,9, expensed ); // 6c row1
		// right now just do 1
		break;
	}
	// list the total
	if ( expensedtotal > 0.0 ) {
		addPriceSimple ( 1,14, expensedtotal ); // box #8
		addPriceSimple ( 1,15, expensedtotal ); // box #9
		addPriceSimple ( 1,17, expensedtotal ); // box #11
		addPriceSimple ( 1,18, expensedtotal ); // box #12
	}

	// . getDepreciation for equipment/property in service before this year
	// . scan the props
	float lost1 = 0.0;
	for ( long i = 0 ; i < np ; i++ ) {
		// shortcut
		Prop *p = &g_prop[i];
		// must match
		if ( p->m_ismatt != ismatt ) continue;
		// year placed in service
		long propyear = atoi(p->m_datePlacedInService+6);
		// skip if new this yar
		if ( propyear == g_accyear ) continue;
		// get depr
		lost1 += getDepreciation ( p , g_accyear );
	}
	addPriceSimple (1,23, lost1); // box #17

	float deprByMethod [40];
	float basisByMethod[40];
	for ( long i = 0 ; i < 40 ; i++ ) {
		deprByMethod [i] = 0.0;
		basisByMethod[i] = 0.0;
	}

	Prop *prop39 = NULL;
	float lost2 = 0.0;
	// add up all depreciation for new equipment based on method (3yr,5yr,...)
	for ( long i = 0 ; i < np ; i++ ) {
		// shortcut
		Prop *p = &g_prop[i];
		// must match
		if ( p->m_ismatt != ismatt ) continue;
		// year placed in service
		long propyear = atoi(p->m_datePlacedInService+6);
		// must be new this year
		if ( propyear != g_accyear ) continue;
		// get recovery period
		long schedule = p->m_schedule;
		// skip if 0. like matt's house. has no basis.
		if ( schedule <= 0 ) continue;
		// get depr
		float depr = getDepreciation ( p , g_accyear );
		// accumulate
		lost2 += depr;
		// add to correct bin
		deprByMethod[schedule] += depr;
		// accumulate this as well
		basisByMethod[schedule] += p->m_basis;
		// a 39 year proprety needs year/month palced in service
		if ( schedule == 39 && ! prop39 ) prop39 = p;
	}
	// print each one
	for ( long i = 0 ; i < 40 ; i++ ) {
		float depr = deprByMethod[i];
		if ( depr == 0.0 ) continue;
		long start = 25; // start at _f25_
		long off;
		if ( i == 3 ) off = 0;
		else if ( i == 5 ) off = 5;
		else if ( i == 7 ) off = 10;
		else if ( i == 10 ) off = 15;
		else if ( i == 15 ) off = 20;
		else if ( i == 20 ) off = 25;
		else if ( i == 25 ) off = 30;
		else if ( i == 39 ) off = (64-25);
		else { char *xx=NULL;*xx=0; }
		start += off;

		// special 39 year property has the date placed in service field
		if ( i == 39 ) {
			addTextSimple ( 1,63, prop39->m_datePlacedInService);
			// basis for depreciation (19b)
			addPriceSimple ( 1,64, basisByMethod[i] );
			// depreciation deduction
			addPriceSimple ( 1,65, depr );
			continue;
		}
			

		// basis for depreciation (19b)
		addPriceSimple ( 1,start+0, basisByMethod[i] );
		// recovery period
		char yr[32];
		sprintf ( yr , "%li years",i);
		addTextSimple  ( 1,start+1, yr );
		// HY
		addTextSimple  ( 1,start+2, "HY" );
		// 200DB
		addTextSimple  ( 1,start+3, "200DB" );
		// depreciation deduction
		addPriceSimple ( 1,start+4, depr );
	}
	// print total on those
	addPriceSimple ( 1,80, lost2 ); // box #21 - total lost2
	addPriceSimple ( 1,81, lost1 + lost2 ); // box #22 - total total

	// form 4562 - page 2 
	addCheck ( 4,"c2_01", "Yes" ); // 24a - yes
	addCheck ( 4,"c2_02", "No" ); // 24b -  No

	addTextSimple ( 2,2,"2005 AUTO" ); // 26 row1
	addTextSimple ( 2,3,"05/25/05" ); // 26 row1
	addTextSimple ( 2,4,"75.00" ); // 26 row1
	addTextSimple ( 2,5,"87,840." ); // 26 row1
	addTextSimple ( 2,6,"65,880." ); // 26 row1
	addTextSimple ( 2,7,"5.00" ); // 26 row1
	addTextSimple ( 2,8,"200DB/HY" ); // 26 row1
	addTextSimple ( 2,9,"2,138." ); // 26 row1
	addTextSimple ( 2,53,"2,138." ); // box #28 total

	//g_sp += sprintf ( g_sp , "go();\n" );
	//g_sp += sprintf ( g_sp , "func_save()\n");

	// do pdfedit on this file for this year
	runScript ( g_script , g_accyear , "f4562" );
}

void runScript ( char *script, long accyear , char *pdf ) {

	// . now use fdf
	// . close the fdf
	g_sp += sprintf ( g_sp ,
			  "] >> >>\n"
			  "endobj\n"
			  "trailer\n"
			  "<</Root 1 0 R>>\n"
			  "%%%%EOF\n" );
	// output it
	char output[1024];
	sprintf(output,"%li/%s.fdf",accyear,pdf);
	FILE *fd = fopen (output,"w");
	if (  ! fd ) {
		fprintf(stdout,"could not open %s for writing\n",output);
		return;
	}
	fprintf(fd,"%s",script);
	fclose ( fd );
	return;
	/*
	// save script
	FILE *fd = fopen ( "./pscript", "w" );
	if (  ! fd ) {
		fprintf(stdout,"could not open ./pscript for writing\n");
		return;
	}
	fprintf(fd,"%s",script);
	fclose ( fd );

	// the tax form name
	char taxform[1024];
	char output[1024];
	sprintf(taxform,"%li/%s.pdf",accyear,pdf);
	sprintf(output,"%li/%s-filled.pdf",accyear,pdf);
		
	// copy it to "fnnnn-filled.pdf"
	char cmd[1024];
	sprintf(cmd,"cp %s %s",taxform,output);
	fprintf(stdout,"%s\n",cmd);
	system ( cmd );

	// now fill it out
	sprintf(cmd,"pdfedit -script pscript %s",output);
	fprintf(stdout,"%s\n",cmd);
	system ( cmd );
	*/
}



// store qbpayroll.txt into the Transaction array
long addQBPayroll ( Trans *t , long nt ) {

	char *full = "./qb/qbpayroll.txt";
	// get size
	struct stat stats;
	stats.st_size = 0;
	stat ( full , &stats );
	// return the size if the status was ok
	long size = stats.st_size;
	// skip if 0 or error
	if ( size <= 0 ) {
		fprintf(stdout,"skipping %s: %s",full,strerror(errno));
		return nt;
	}
	// read in qbpayroll.txt from cwd
	int fd = open ( full , O_RDONLY );
	// return if not there
	if ( fd < 0 ) {
		fprintf(stdout,"open %s : %s\n",full,strerror(errno));
		return nt;
	}
	// alloc it
	char *buf = (char *)malloc(3000000);
	char *bufPtr = buf;
	//fprintf(stdout,"reading file %s of size %li\n",full,size);
	// read it all in
	int nr = read ( fd , bufPtr , size );
	if ( nr != size ) {
		fprintf(stdout,"read %s : %s\n",full,strerror(errno));
		return nt;
	}
	// note it
	fprintf(stdout,"read %s\n",full);
	close ( fd );
	// NULL terminate
	bufPtr[nr] = '\0';

	// parse it up
	// the end of it
	char *pend = buf + nr;
	// set it up
	char *eol = NULL;
	// unknown
	long year = 0;
	// the date
	char *date = NULL;
	// wierd YTD adjustment sections, skip those
	bool inAdjustment = false;
	// loop over each line in buffer
	for ( char *p = buf ; p < pend ; p = eol + 1 ) {
		// get the end of the line
		eol = strchr ( p , '\n');
		// none? all done with this file
		if ( ! eol ) break;
		// terminate it there for the meantime
		*eol = '\0';
		// skip whitespace
		while ( *p && isspace(*p) ) p++;
		// need a date of the form 03/31/2006
		if ( isdigit(p[0]) &&
		     isdigit(p[1]) &&
		     p[2] == '/' &&
		     isdigit(p[3]) &&
		     isdigit(p[4]) &&
		     p[5] == '/' &&
		     isdigit(p[6]) &&
		     isdigit(p[7]) &&
		     isdigit(p[8]) &&
		     isdigit(p[9]) ) {
			// set it!
			date = p;
			// if this contains "YTD Adj" skip it
			if      ( strstr ( p , "YTD Adjustment"  ) ) 
				inAdjustment = true;
			else if ( strstr ( p , "Liability Adjust") ) 
				inAdjustment = true;
			else if ( strstr ( p , "Liability Check" ) ) 
				inAdjustment = true;
			else                
				inAdjustment = false;
			// NULL term it
			p[10] = '\0';
			// get the year
			year = atoi(p+6);
			// skip if in adjustment
			if ( inAdjustment ) continue;
			// skip that NULL
			p += 11;
			// skip tabs
			//while ( *p && *p=='\t' ) p++;
			// skip numbers (i.e. "2016")
			//while ( *p && isdigit(*p) ) p++;
			// more tabs
			//while ( *p && *p=='\t' ) p++;
			// skip Paycheck
			char *s = strstr(p,"Paycheck\t");
			// point to the name of the employee
			if ( s ) p = s + 10;
		}
		// need a date
		if ( ! date ) continue;
		// skip if in adjustment section
		if ( inAdjustment ) continue;
		// skip if its a quote, that is a "total" line methinks
		if ( *p == '\"' ) continue;
		// now we got the employee name
		char *dst = p;
		// skip until tab to find end of it
		while ( *p && *p!='\t' ) p++;
		// exhausted?
		if ( ! *p ) continue;
		// end of emplyee name
		*p = '\0';
		// skip that tab
		p++;
		// skip until alnum
		while ( *p && isspace(*p) ) p++;
		// save it
		char *desc = p;
		// skip until tab to find end of it
		while ( *p && *p!='\t' ) p++;
		// end of desc
		*p = '\0';
		// skip that tab
		p++;
		// skip until non-space
		while ( *p && isspace(*p) ) p++;
		// skip over this quoted number
		while ( *p && !isspace(*p) ) p++;
		// all done?
		if ( p >= pend ) break;
		// must be tab
		if ( *p != '\t' ) { char *xx=NULL;*xx=0; }
		// skip until non-space
		while ( *p && isspace(*p) ) p++;
		// skip if quote
		if ( *p=='\"' ) p++;
		// the default acct
		char *defaultacct = "0042 7533 6550";
		// set the acct
		char *src = defaultacct;
		// now we got the amount
		float amt = atof2(p);
		// flip the sign, since in quickbooks a negative sign means it
		// comes out of employee acct, but for us we are all relative 
		// to our operating accounts
		amt *= -1;
		// . this is stuff gigablast pays that employee does not pay 
		//   at all
		// . we just deduct these amounts from our main acct and put 
		//   into the HOLD acct
		bool flip = false;
		if ( strstr (desc,"Social Security Company" ) ||
		     strstr (desc,"Federal Unemployment" ) ||
		     strstr (desc,"NM - Unemployment" ) ||
		     strstr (desc,"CO - Unemployment" ) ||
		     strstr (desc,"MA - Unemployment" ) ||
		     strstr (desc,"TX - Unemployment" ) ||
		     strstr (desc,"MA - Workforce Training Fund" ) ||
		     strstr (desc,"Unemployment Company" ) ||
		     strstr (desc,"CO - Total Surcharge") ||
		     strstr (desc," (company paid)" ) ||
		     strstr (desc,"Matching Contribution" ) ||
		     strstr (desc,"Medicare Company" ) ) {
			// skip if negative, we already handled it
			if ( amt > 0.0 ) continue;
			// if positive acct is HOLD
			// TODO: set dstAcct to like the tax man, or something.
			//  who it's going to
			//if ( amt>0.0){ src = "HOLD"     ; dst = "Tax man"; }
			//else             { src = defaultacct; dst = "HOLD"; }
			//src = defaultacct; 
			dst = "HOLD";
			flip = true;
		}

		// IGNORE if it is "Health Insurance (company paid)"
		if ( strstr (desc,"Health Insurance (company paid)" ) ) 
			continue;

		if ( amt == 0.00 ) continue;

		// store it;
		t[nt].m_desc     = desc;
		t[nt].m_amount   = amt;
		t[nt].m_date     = date;
		t[nt].m_year     = year;
		t[nt].m_srcAcct  = src;
		t[nt].m_dstAcct  = dst;
		//t[nt].m_type     = 'E';
		t[nt].m_checkNum = -1;
		// got it
		nt++;

		// every transaction is flipped and repeated. only if we own 
		// both accts!
		if ( flip ) {
			t[nt].m_desc     = desc;
			t[nt].m_amount   = -1 * amt;
			t[nt].m_dstAcct  = src;
			t[nt].m_date     = date;
			t[nt].m_year     = year;
			t[nt].m_srcAcct  = dst;
			//t[nt].m_type     = 'E';
			t[nt].m_checkNum = -1;
			// got it
			nt++;
		}

		/*
		if ( strstr (desc,"Health Insurance (taxable)" ) ) {
			// employee pays this, so make it from negative to 
			// positive because our account is being credited by 
			// this employee we are measuring transactions between
			// us and employee now use employee as the "entity"
		}
		*/
		// . this is stuff employee pays and we hold
		// . we charged employee above and now we take that $$$ and 
		//   put into HOLD acct.
		if ( strstr (desc,"Federal Withholding" ) ||
		     strstr (desc,"Medicare Employee" ) ||
		     strstr (desc,"Pre Tax 401(k)" ) ||
		     strstr (desc,"Roth 401(k)" ) ||
		     strstr (desc,"NM - Income Tax" ) ||
		     strstr (desc,"Social Security Employee" ) ) {
			// remove the last one
			nt--;
			// . employee is source, HOLD is dst
			// . only our accts should be srcs
			/*
			t[nt].m_amount   = -1 * amt; // negative
			t[nt].m_dstAcct  = "HOLD";
			t[nt].m_date     = date;
			t[nt].m_year     = year;
			t[nt].m_srcAcct  = dst;
			t[nt].m_desc     = desc;
			t[nt].m_checkNum = -1;
			nt++;
			*/
			// HOLD is src   employee is dst
			t[nt].m_amount   = amt; // positive
			t[nt].m_dstAcct  = dst;
			t[nt].m_date     = date;
			t[nt].m_year     = year;
			t[nt].m_srcAcct  = "HOLD";
			t[nt].m_desc     = desc;
			t[nt].m_checkNum = -1;
			nt++;
		}
	}

	//free ( buf );
	return nt;
}


// grab descriptions from quickbooks checks and augment our transactions
void addQBChecks ( Trans *t , long nt ) {
	char *full = "./qb/qbchecks.txt";
	// get size
	struct stat stats;
	stats.st_size = 0;
	stat ( full , &stats );
	// return the size if the status was ok
	long size = stats.st_size;
	// skip if 0 or error
	if ( size <= 0 ) {
		fprintf(stdout,"skipping %s: %s",full,strerror(errno));
		return;
	}
	// read in qbpayroll.txt from cwd
	int fd = open ( full , O_RDONLY );
	// return if not there
	if ( fd < 0 ) {
		fprintf(stdout,"open %s : %s\n",full,strerror(errno));
		return;
	}
	// alloc it
	char *buf = (char *)malloc(3000000);
	char *bufPtr = buf;
	//fprintf(stdout,"reading file %s of size %li\n",full,size);
	// read it all in
	int nr = read ( fd , bufPtr , size );
	if ( nr != size ) {
		fprintf(stdout,"read %s : %s\n",full,strerror(errno));
		return;
	}
	// note it
	fprintf(stdout,"read %s\n",full);
	close ( fd );
	// NULL terminate
	bufPtr[nr] = '\0';

	// unmatched checks go into here
	float um_amt  [5000];
	long  um_cn   [5000];
	char *um_desc [5000];
	long  nu = 0;

	// parse it up
	// the end of it
	char *pend = buf + nr;
	// set it up
	char *eol = NULL;
	// unknown
	//long year = 0;
	// the date
	//char *date = NULL;
	// wierd YTD adjustment sections, skip those
	//bool ignore = false;
	// loop over each line in buffer
	for ( char *p = buf ; p < pend ; p = eol + 1 ) {
		// get the end of the line
		eol = strchr ( p , '\n');
		// none? all done with this file
		if ( ! eol ) break;
		// terminate it there for the meantime
		*eol = '\0';
		// skip whitespace
		while ( *p && isspace(*p) ) p++;
		// if line does not start with Bill Pmt -Check, skip the line
		// can start with "Check" as well too!
		if ( strncmp ( p , "Bill Pmt -Check" ,15) &&
		     strncmp ( p , "Check" ,5)            &&
		     strncmp ( p , "Liability Check" ,15) )
			continue;
		// skip until we hit a tab
		while ( *p && *p != '\t' ) p++;
		// skip spaces following
		while ( isspace(*p) ) p++;
		// . usually the check # but could be "CCFER" or "Cash" etc.
		// . TODO: support matching without a check # based on amount...
		if ( ! isdigit(p[0]) ) continue;
		if ( ! isdigit(p[1]) ) continue;
		if ( ! isdigit(p[2]) ) continue;
		if ( ! isdigit(p[3]) ) continue;
		if ( ! isspace(p[4]) ) continue;
		// then is the check #
		long cn = atol(p);
		// sanity check
		if ( cn <= 0 || cn >99999 ) { char*xx=NULL;*xx=0; }
		// skip check #
		while ( isdigit(*p) ) p++;
		// skip spaces following check #
		while ( isspace(*p) ) p++;
		// now is the date like 04/03/2008
		char *date = p;
		// null term date
		date[5] = '\0';
		// null term year
		date[10] = '\0';
		// get the year from it
		long year = atol(date + 6);
		// sanity check
		if ( year <= 0 || year > 2099 ) { char *xx=NULL;*xx=0;}
		// skip all including NULL
		p += 11;
		// skip spaces following date
		while ( isspace(*p) ) p++;
		// now is the dest acct
		char *dst = p;
		// skip until we hit a tab
		while ( *p && *p != '\t' ) p++;
		// strange?
		if ( ! *p ) continue;
		// null term that and skip over NULL
		*p++ = '\0';
		// skip spaces following dest acct name
		while ( isspace(*p) ) p++;
		// now we got the canonical acct name (i.e. Gigablast operacct)
		char *can = p;
		// skip until we hit a tab
		while ( *p && *p != '\t' ) p++;
		// strange?
		if ( ! *p ) continue;
		// null term that and skip over NULL
		*p++ = '\0';
		// skip spaces following the canonical acct name
		while ( isspace(*p) ) p++;
		// now we should have the amount
		float amt = atof2 ( p );
		// map the canonical acct name to a account #
		char *an = "unknown";
		if ( strncmp(can,"1040",4)==0 ) // old operating
			an = "004275336550";
		if ( strncmp(can,"1050",4)==0 ) // old (now closed) payroll
			an = "004275336563";
		// now find it in our list of transactions
		// go by check #
		long i = 0;
		for ( ; i < nt ; i++ ) {
			// skip if does not match
			if ( t[i].m_checkNum != cn ) continue;
			// amount must match!
			if ( t[i].m_amount != amt ) continue;
			// got a match, but skip if it already has a dst
			if ( t[i].m_dstAcct ) continue;
			// add it in
			t[i].m_dstAcct = dst;
			// and to desc to be clear
			if ( ! t[i].m_desc ) t[i].m_desc = dst;
			// print it
			//fprintf(stdout,"check #%li : %s\n",cn,t[i].m_desc);
			break;
		}
		// if match go to next line
		if ( i < nt ) continue;
		// store in the unmatched array
		um_amt  [nu] = amt;
		um_cn   [nu] = cn;
		um_desc [nu] = dst;
		nu++;
	}
	// sanity check
	if ( nu > 5000 ) { char *xx=NULL;*xx=0; }

	// now try to match our check transaction to an amount
	for ( long i = 0 ; i < nt ; i++ ) {
		// skip if it has a dst acct already
		if ( t[i].m_dstAcct ) continue;
		// skip if not a check
		if ( t[i].m_checkNum <= 0 ) continue;
		// get the amt
		float amt = t[i].m_amount;
		// loop over all unmached checks we accumulated
		for ( long u = 0 ; u < nu ; u++ ) {
			// amount must match!
			if ( um_amt[u] != amt ) continue;
			// MUST be the ONLY transaction with that amt
			long k = 0;
			for ( k = 0 ; k < nt ; k++ ) {
				if ( k == i ) continue;
				if ( t[k].m_amount == amt ) break;
			}
			// it was not unique
			if ( k < nt ) continue;
			// must be unique in the list of unmatched checks too!
			for ( k = 0 ; k < nu ; k++ ) {
				if ( k == u ) continue;
				if ( um_amt[k] == amt ) break;
			}
			// it was not unique
			if ( k < nu ) continue;
			// got it
			fprintf(stdout,"matched check #%li to #%li based on "
				"amt of %.02f\n",t[i].m_checkNum,
				um_cn[u],um_amt[u]);
			t[i].m_dstAcct = um_desc[u];
			t[i].m_desc    = um_desc[u];
			// print it
			//fprintf(stdout,"check #%li : %s\n",t[i].m_checkNum,t[i].m_desc);
			break;
		}
	}

	return;
}

float atof2 ( char *p ) {
	// copy into buf
	char buf[128];
	char *s = p;
	char *send = p + 30;
	char *dst = buf;
	// skip initial quote
	if ( *s=='\"' ) s++;
	// skip initial spaces and dollar
	for ( ; *s==' ' || *s=='\t' ; s++);
	// then dollar
	if ( *s=='$' ) s++;
	// skip spaces after dollar
	for ( ; *s==' ' || *s=='\t' ; s++);
	// add the -
	if ( *s=='-' ) { *dst++ = *s++; }
	for ( ;*s && s < send && (isdigit(*s) || *s==',' || *s=='.' ) ; s++) {
		// skip commas
		if ( *s == ',' ) continue;
		// copy othewise
		*dst++ = *s;
	}
	// NULL terminate
	*dst = '\0';
	// get it
	float val = atof(buf);
	// for acct 0265 (matt's personal) the sign is AFTER the #
	// damn! sometimes like for 0265/eStmt_07_20_2009.txt the +/- is
	// way out there
	for ( ; *s==' ' || *s=='\t' ; s++ );
	if ( *s == '-' && isspace(s[1]) ) 
		val = val * -1;
	// return it
	return val;
}

/*
void make941s ( Trans **t , long nt ) {

	long year    = 2008;
	// quarters go: 0, 1, 2 and 3
	long quarter = 1;

 loop:

	// print out the form
	char file[1024];
	sprintf(file,"./f941-%li-Q%li",year,quarter);
	FILE *fd = fopen (file,"w");

	fprintf(fd,"-----BEGIN FORM 941 WORKSHEET Q%li %lil-----\n",quarter,year);

	long numEmployees = 0;
	long ht[1024];
	memset ( ht , 0 , 1024*4 );
	float wages = 0.0;
	float taxableWages = 0.0;
	float payments = 0.0;
	float withheld = 0.0;
	float totalTax2 = 0.0;

	// tax liability per day for 941 schedule B
	float taxes[3][31];
	for ( long i = 0 ; i < 3 ; i++ )
		for ( long j = 0 ; j < 31 ; j++ )
			taxes[i][j] = 0.0;

	for ( long i = 0 ; i < nt ; i++ ) {
		// skip if not matching year
		if ( t[i]->m_year != year ) continue;
		// skip if not WAGES or WITHOLDINGS
		long type = 0;
		if ( ! t[i]->m_cat ) continue;
		if ( strstr(t[i]->m_cat,"WAGES"    ) ) type = 1;
		if ( strstr(t[i]->m_cat,"WITHHOLD1") ) type = 2;
		if ( strstr(t[i]->m_cat,"WITHHOLD2") ) type = 3;
		if ( strstr(t[i]->m_cat,"TAXES941" ) ) type = 4;
		if ( type == 0 ) continue;
		// get the month
		long mon = atoi(t[i]->m_date);
		// assign quarter
		long q = 4;
		if      ( mon <= 3 ) q = 1;
		else if ( mon <= 6 ) q = 2;
		else if ( mon <= 9 ) q = 3;
		// skip if not a match
		if ( q != quarter ) continue;

		// add up all payments we made for 941s this quarter
		if ( type == 4 ) {
			// the date
			char *date = t[i]->m_date;
			date[5] = '\0';
			// note it!
			fprintf(fd,"%s/%li PAYMENT  %10.02f\n",
				date , year , t[i]->m_amount );
			// count it
			payments += t[i]->m_amount;
			continue;
		}

		// . 941 Schedule B
		// . get the month # with respect to quarter
		// . this can be 0, 1 or 2
		long mn = (mon -1 ) % 3;
		// get the day of the month (SUBTRACT 1)
		long day = atoi ( t[i]->m_date + 3 ) - 1;

		// BOX #2
		// . add up total wages given out
		// . these should be before any deductions or withholdings
		if ( type == 1 ) {
			float amt = -1 * t[i]->m_amount;
			wages += amt;
			// BUT gourav is not liable for social sec or medicare
			if ( gb_strcasestr(t[i]->m_dstAcct,"gourav") )
				amt = 0.0;
			// hey we got some wages, add to tax liability!
			float tax = amt * .124 + amt * .029;
			// note it!
			fprintf(fd,"%s WAGE     %10.02f EMPLOYEE=%s\n",
				t[i]->m_date , -1 * t[i]->m_amount ,
				t[i]->m_dstAcct );
			fprintf(fd,"%s SSMEDTAX %10.02f EMPLOYEE=%s\n",
				t[i]->m_date , tax ,
				t[i]->m_dstAcct );
			// and the taxable wages for mediacare and social sec.
			taxableWages += amt;
			// add it up for 941 Schedule B
			taxes[mn][day] += tax;
			// add up total tax
			totalTax2 += tax;
		}
		
		// BOX #3
		// . Income tax withheld from wages tips and other compensation
		// . assume any tax on income is an income tax
		// . do not include taxes withheld from company (WITHHOLD2)
		// . DO NOT INCLUDE SOCIAL SECURITY AND MEDICARE, i guess they
		//   are not treated as income tax. ONLY FEDERAL INCOME TAX!
		// . "Enter the federal income tax you withheld..."
		bool incomeTax = false;
		if ( type == 2 ) {
			if ( strstr(t[i]->m_desc,"Federal Withholding") )
				incomeTax = true;
			if ( ! incomeTax ) 
				continue;
			// accumulate
			withheld += t[i]->m_amount;
			// and for single day of month for 941 Schedule B
			taxes[mn][day] += t[i]->m_amount;
			// note it!
			fprintf(fd,"%s WITHHELD %10.02f EMPLOYEE=%s\n",
				t[i]->m_date , t[i]->m_amount ,
				t[i]->m_dstAcct );
		}

		// count the employees using a hash table
		if ( type == 1 ) {
			long h = hash32 ( t[i]->m_dstAcct );
			unsigned long n = (unsigned long)h %1024;
			if ( ht[n] && ht[n]!=h )
				if ( ++n >= 1024 ) n = 0;
			if ( ! ht[n] ) { ht[n] = h; numEmployees++; }
		}
	}

	fprintf(fd,"TOTAL WAGES = %.02f\n",wages);
	fprintf(fd,"TOTAL TAXABLE WAGES = %.02f\n",taxableWages);
	fprintf(fd,"TOTAL SSMEDTAX = %.02f\n",totalTax2);
	fprintf(fd,"TOTAL WITHHELD = %.02f\n",withheld );
	fprintf(fd,"TOTAL TAXES = %.02f\n",totalTax2+withheld);

	fprintf(fd,"-----END FORM 941 WORKSHEET Q%li %lil-----\n",quarter,year);
	// if no wages stop!
	if ( wages <= 0.0 ) return;

	// for applying the social security and the medicare taxes we must
	// only tax the "POST-TAX" wages... but there are so many taxes
	// applied to the wage that the order of application might matter!
	// but the instructions make it clear ... every tax taxes the original
	// wage.
	float totalTax = taxableWages * .124 + taxableWages * .029;
	fprintf(fd,"-----BEGIN FORM 941 Q%li %li-----\n",quarter,year);
	fprintf(fd,"EIN = 20-1574374\n");
	fprintf(fd,"Name = Gigablast, Inc.\n");
	fprintf(fd,
		"Trade name = NONE\n"
		"Address = 4001 Bogan Ave. NE. Suite A\n"
		"City = Albuquerque\n"
		"State = NM\n"
		"Zip = 87109\n"
		"Quarter = %li\n"
		"Year = %li\n"
		"#1 Number of employees who received wages = %li\n"
		"#2 Wages, tips, and other compensation = %.02f\n"
		"#3 Income Tax Withheld from wages, tips and other "
		"compensation = %.02f\n"
		"#5a Taxable social security wages = %.02f x .124 = %.02f\n"
		"#5b Taxable social security tips = 0.00 x .124 = 0.00\n"
		"#5c Taxable Medicare wages & tips = %.02f x .029 = %.02f\n"
		"#5d Total social security and Medicare taxes = %.02f\n"
		"#6 Total taxes before adjustments = %.02f\n"
		,
		quarter , 
		year ,
		numEmployees , 
		wages , // total wages for quarter
		withheld,

		taxableWages , taxableWages * .124 ,

		taxableWages , taxableWages * .029 ,
		totalTax ,
		withheld + totalTax );

	float balance = withheld + totalTax - payments;
	float weOweThem = balance;
	float theyOweUs = balance;
	if ( weOweThem < 0 ) weOweThem  =  0.0;
	if ( theyOweUs < 0 ) theyOweUs *= -1.0;
	else                 theyOweUs  =  0.0;
	fprintf ( fd ,
		  "#7a Current quarter's fractions of cents = 0\n"
		  "#7b Current quarter's sick pay = 0\n"
		  "#7c Current quarter's adjustments for tips and group-term "
		  "life insurance = 0\n"
		  // adjust mistakes here:
		  "#7d Current year's income tax withholding = 0\n"
		  // and here:
		  "#7e Prior quarters' social security and Medicare taxes = "
		  "0\n"
		  "#7f Special additions to federal income tax = 0\n"
		  "#7g Special additions to social security and Medicare = 0\n"
		  "#7h TOTAL ADJUSTMENTS. Combine all amounts on lines 7a "
		  "through 7g = 0\n"
		  "#8 Total taxes after adjustments. Combine lines 6 and 7h = "
		  "%.02f\n"
		  "#9 Advance earned income credit (EIC) payments made to "
		  "employees = 0\n"
		  "#10 Total taxes after adjust for advance EIC (line 8 - "
		  "line 9 = line 10) = %.02f\n"
		  // make overpayment fixes here:
		  "#11 Total deposits for this quarter, including overpayment "
		  "applied from a prior quarter = \n"
		  "#12 Balance due. If line 10 is more than line 11, write "
		  "the difference here = %.02f\n"
		  "#13 Overpayment. If line 11 is more than line 10, write "
		  "the difference here = %.02f\n"
		  "#14 Write the state abbrivation for the state where you "
		  "made your deposits OR write \"MU\" if you made your "
		  "deposits in multiple states = NM\n"
		  "#15 You were a semiweekly schedule depositor for any part "
		  "of this quarter. Schedule B (Form 941) attached.\n"
		  "#16 N/A for now\n"
		  "Part 4: N/A\n"
		  "Part 5: Signature.\n"
		  ,
		  withheld + totalTax,
		  withheld + totalTax,
		  payments ,
		  weOweThem ,
		  theyOweUs
		);
	fprintf(fd,"-----END FORM 941-----\n");	

	// print out 941 Schedule B, tax liability per day!
	fprintf(fd,"-----BEGIN FORM 941 SCHEDULE B Q%li %li-----\n",quarter,year);
	float total = 0.0;
	fprintf(fd,"EIN = 20-1574374\n");
	fprintf(fd,"Name = Gigablast, Inc.\n");
	fprintf(fd,"Year = %li\n",year);
	fprintf(fd,"Quarter = %li of 1 through 4\n",quarter);
	for ( long i = 0 ; i < 3 ; i++ ) {
		float monthTotal = 0.0;
		for ( long j = 0 ; j < 31 ; j++ ) {
			fprintf(fd,
				"month #%li day #%li = %.02f\n",
				i+1,
				j+1,
				taxes[i][j] );
			monthTotal += taxes[i][j];
		}
		total += monthTotal;
		fprintf(fd,"Total for month #%li = %.02f\n",i+1,monthTotal);
	}
	fprintf(fd,"Total liability for the quarter = %.02f\n",total);
	fprintf(fd,"-----END FORM 941 SCHEDULE B-----\n");

	fclose(fd);

	if ( ++quarter > 4 ) { quarter = 1; year++; }
	goto loop;
	return;
}
*/

long hash32 ( char *s ) {
	unsigned long d = 234957202;
	for ( ; *s ; s++ ) { d ^= (unsigned char)s[0]; d *= 123; }
	return (long)d;
}

char *getCategory ( Trans *t ) {

	// check #
	long cn = t->m_checkNum;
	float amt = t->m_amount;

	float pamt = amt;
	if ( pamt < 0.0 ) pamt *= -1;

	// these are known to be 941 tax payments according to the
	// irs transcript they faxed me mid jan 2009 which is
	// stored as a pdf in the Taxes folder
	long long refNum = t->m_refNum;
	if ( refNum == 902530003017427LL ||
	     refNum == 902599010463946LL ||
	     refNum == 902599010463951LL ||
	     refNum == 902599010463945LL ||
	     refNum == 902523004805844LL ||
	     refNum == 902533006946878LL ||
	     refNum == 902568005371902LL ||
	     refNum == 902565002570035LL ||
	     refNum == 902565002570037LL ||
	     refNum == 902568005371901LL ||
	     refNum == 902575001946831LL ||
	     refNum == 902577010590950LL ||
	     refNum == 902585004623888LL ||
	     refNum == 902591010485344LL ||
	     refNum == 902512009111800LL ||
	     refNum == 902597001829476LL ||
	     refNum == 902506005386625LL ||
	     refNum == 902513003755206LL ||
	     refNum == 902514007093744LL ||
	     refNum == 902521006257537LL ||
	     refNum == 902533005462347LL ||
	     refNum == 902553005399818LL ||
	     refNum == 902563003847531LL ||
	     refNum == 902570001099484LL ||
	     refNum == 902501000763362LL ||
	     refNum == 902505001580617LL ||
	     refNum == 902588005155948LL ||
	     refNum == 902502006668598LL ||
	     refNum == 902515008820147LL ||
	     refNum == 902530010012022LL ||
	     refNum == 902538006830985LL ) {
		return "TAXES941";
		
	}
	// info relay check
	if ( refNum == 813003130568206LL )
		return "DATASVC";

	// get the src account
	char *src = t->m_srcAcct ;
	// look at description
	char *d = t->m_desc;
	char *date = t->m_date;
	// paid from gigablast llc means data ctr
	if ( ! d && src && strcmp(src,"4390 0007 2971") == 0 ) {
		return "DATACTR";
		
	}
	// default
	if ( ! d ) {
		if ( amt < 0 && amt >= -150.00 ) return "SUPPLIES"; 
		return "PROFESSIONAL";
		//return NULL;
	}

	if ( gb_strcasestr(d,"Return of Posted") ) { 
		// . undo credit card transaction
		// . also change "Fia Card" above
		if ( !strncmp(date ,"11-02",5) ) return "ERRORCHARGE";
		if ( !strncmp(date ,"03-23",5) ) return "ERRORCHARGE";
		// need to account for refunds because if they were an undeductible expense
		// then we get screwed
		char *xx=NULL;*xx=0;
	}			

	// greg gave me a check that bounced here
	if ( amt ==  50000.00 && !strncmp(date,"04/09",5) ) return "ERRORCHARGE";
	if ( amt == -50000.00 && !strncmp(date,"04/12",5) ) return "ERRORCHARGE";

	// checks
	if ( gb_strcasestr(d,"jezebel") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"martha") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"partap") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"zak") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"susan") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"raffert") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"rebekah") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"judy") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"elhers") ) { return "LEGAL";  }
	if ( gb_strcasestr(d,"dincel") ) { return "LEGAL";  }
	if ( gb_strcasestr(d,"land prop tax") ) { return "TAXES";  }
	if ( gb_strcasestr(d,"simple grinn") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"sylvain") ) { return "LEGAL";  }
	if ( gb_strcasestr(d,"smith and cook") ) { return "LEGAL";  }
	if ( gb_strcasestr(d,"town of bernal") ) { return "FEES";  }
	if ( gb_strcasestr(d,"madrid prop tax") ) { return "TAXES";  }
	if ( gb_strcasestr(d,"bogan prop tax") ) { return "TAXES";  }
	if ( gb_strcasestr(d,"discount blind") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"dan hansen") ) { return "LEGAL";  }
	if ( gb_strcasestr(d,"liberty life") ) { return "INSURANCE";  }
	if ( gb_strcasestr(d,"thompson ac") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"blind express") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"terminix") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"rudolph fri") ) { return "LEGAL";  }
	if ( gb_strcasestr(d,"new mexico gas") ) { return "UTILITIES";  }
	if ( gb_strcasestr(d,"robert goodman") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"guiding light") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"inspection") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"tax assesor") ) { return "TAXES";  }
	if ( gb_strcasestr(d,"city of abq") ) { return "FEES";  }
	if ( gb_strcasestr(d,"alarm fee") ) { return "FEES";  }
	if ( gb_strcasestr(d,"plumbing") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"eduardo rojo") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"heights lock") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"futa taxes") ) { return "TAXES";  }
	if ( gb_strcasestr(d,"garcia honda") ) { return "PROFESSIONAL";  }
	
	if ( gb_strcasestr(d,"jaffe") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"nm tax") ) { return "TAXES";  }
	if ( gb_strcasestr(d,"department of labor") ) { return "TAXES";  }
	if ( gb_strcasestr(d,"nm gas") ) { return "UTILITIES";  }
	if ( gb_strcasestr(d,"merit bennet") ) { return "LEGAL";  }
	if ( gb_strcasestr(d,"medical") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"nm vital records") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"dean neu") ) { return "LEGAL";  }
	if ( gb_strcasestr(d,"american express") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"armed response") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"sheehan") ) { return "LEGAL";  }
	if ( gb_strcasestr(d,"roadrunner wire") ) { return "DATASVC";  }
	if ( gb_strcasestr(d,"mvd motor") ) { return "TAXES";  }
	if ( gb_strcasestr(d,"sky high tech") ) { return "PROFESSIONAL";  }
	
		

	// matt's personal acct things:
	if ( gb_strcasestr(d,"Cach ID") ) { return "BOFAHOMELOAN";  }
	if ( gb_strcasestr(d,"Loan Pmt") ) { return "BOFAHOMELOAN";  }
	if ( gb_strcasestr(d,"Liberty Mut") ) { return "CARINSURANCE";  }
	if ( gb_strcasestr(d,"Xm Sat") ) { return "ENTERTAINMENT";  }
	if ( gb_strcasestr(d,"Home Sec") ) { return "DATASVC";  }
	if ( gb_strcasestr(d,"Credit Card") ) { return "CREDITCARD";  }
	if ( gb_strcasestr(d,"overdraft") ) { return "BANKFEES";  }


	if ( gb_strcasestr(d,"Mortgage") ) { 
		// this was refunded the next day
		if ( ! strncmp(date,"11-01",5) ) return "ERRORCHARGE";
		return "BOFAHOMELOAN";  
	}

	if ( gb_strcasestr(d,"Fia Card") ) {
		// this was refunded the next day
		if ( ! strncmp(date,"03-22",5) ) return "ERRORCHARGE";
		return "CREDITCARD";  
	}

	// gb in account things:
	if ( gb_strcasestr(d,"mtg paymt") ) { return "GBHOMELOAN";  }


	if ( gb_strcasestr(d,"tenamax") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"softlayer") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"Paypal") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"skype") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"serverpronto") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"globex") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"teriyaki") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d," legal") ) { return "LEGAL";  }
	if ( gb_strcasestr(d," butler") ) { return "LEGAL";  }
	if ( gb_strcasestr(d,"taco") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"centurylink") ) { return "DATASVC";  }
	if ( gb_strcasestr(d,"verizon") ) { return "DATASVC";  }
	if ( gb_strcasestr(d,"it'z") ) { return "ENTERTAINMENT";  }
	if ( gb_strcasestr(d,"spyderjump") ) { return "ENTERTAINMENT";  }
	if ( gb_strcasestr(d," theatres") ) { return "ENTERTAINMENT";  }
	if ( gb_strcasestr(d,"cliffs") ) { return "ENTERTAINMENT";  }
	if ( gb_strcasestr(d," adventures") ) { return "ENTERTAINMENT";  }
	if ( gb_strcasestr(d,"ski santa") ) { return "ENTERTAINMENT";  }
	if ( gb_strcasestr(d,"aventurine") ) { return "DATASVC";  }
	if ( gb_strcasestr(d,"caliber") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"cub cadet") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"wm super") ) { return "SUPPLIES"; }
	if ( gb_strcasestr(d,"prime") ) { return "SUPPLIES"; }
	if ( gb_strcasestr(d,"oreilly") ) { return "SUPPLIES"; }
	if ( gb_strcasestr(d,"true value") ) { return "SUPPLIES"; }
	if ( gb_strcasestr(d,"natureworks") ) { return "SUPPLIES"; }
	if ( gb_strcasestr(d,"teeth") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"patricia roberts") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"oswald") ) { return "LEGAL";  } // stock opinion
	// i still don't know what crowne plaza is!
	if ( gb_strcasestr(d,"crowne plaza") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"pueblo of sand") ) { return "ENTERTAINMENT";  }
	if ( gb_strcasestr(d,"pueblo of sand") ) { return "ENTERTAINMENT";  }
	if ( gb_strcasestr(d,"k-c services") ) { return "PROFESSIONAL"; }//a/c



	if ( gb_strcasestr(d,"gourmet") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"wendy's") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"los compadres") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"santacafe") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"taj mahal") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"high finance") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"papa murph") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"dions") ) { return "FOOD";  }

	// Online Business Suite Direct Pmt Services
	if ( gb_strcasestr(d,"online business") && ! g_ismatt ) { return "PROFESSIONAL";  }

	// piano movers
	if ( strcmp(d,"Check") == 0 &&
	     g_ismatt &&
	     strncmp(date,"07-27",5)==0 &&
	     amt >= -1951.00 && amt <= -1949.00 ) { 
		return "PERSONAL";
	}



	
	if ( gb_strcasestr(d,"wire transfer fee") ) { return "FEES";  }
	if ( gb_strcasestr(d,"electronic transaction") ) { return "FEES";  }
	if ( gb_strcasestr(d,"check photocopy") ) { return "FEES";  }
	if ( gb_strcasestr(d,"rybolt") ) { return "SUPPLIES";  }
	// paying workes with paypal
	if ( gb_strcasestr(d,"paypal des") ) { return "PROFESSIONAL";  }
	// gas station
	if ( gb_strcasestr(d,"bien mur travel") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"murphy exp") ) { return "TRAVEL";  }
	
	if ( gb_strcasestr(d,"water bill") ) { return "UTILITIES";  }
	if ( gb_strcasestr(d,"verizon wireless bill") ) { return "DATASVC";  }
	
	if ( gb_strcasestr(d,"new mexico tax & rev") ) { return "TAXES";  }
	if ( gb_strcasestr(d,"new mexico crs") ) { return "TAXESCRS";  }
	if ( gb_strcasestr(d,"nm trd crs") ) { return "TAXESCRS";  }
	if ( gb_strcasestr(d,"crsnet") ) { return "TAXESCRS";  }
	if ( gb_strcasestr(d,"crsecks") ) { return "TAXESCRS";  }
	if ( gb_strcasestr(d,"Salary") ) { return "PAYROLL";  }
	//if ( gb_strcasestr(d,"nsurance") ) { return "INSURANCE";  }
	if ( gb_strcasestr(d,"Federal Withhold") ) { return "WITHHOLD1";  }
	if ( gb_strcasestr(d,"Social Security Employee") ) { return "WITHHOLD1";  }
	if ( gb_strcasestr(d,"Social Security Company") ) { return "WITHHOLD2";  }
	if ( gb_strcasestr(d,"Matching Contrib") ) { return "RETIREMENT";  }
	if ( gb_strcasestr(d,"401(k)") ) { return "RETIREMENT";  }
	if ( gb_strcasestr(d,"Medicare Employee") ) { return "WITHHOLD1";  }
	if ( gb_strcasestr(d,"Medicare Company") ) { return "WITHHOLD2";  }
	if ( gb_strcasestr(d,"Income Tax") ) { return "WITHHOLD1";  }
	if ( gb_strcasestr(d,"Unemployment") ) { return "WITHHOLD2";  }
	if ( gb_strcasestr(d,"Bonus") ) { return "PAYROLL";  }
	if ( gb_strcasestr(d,"Withhold") ) { return "WITHHOLD1";  }
	if ( gb_strcasestr(d,"Workforce") ) { return "WITHHOLD2";  }
	if ( gb_strcasestr(d,"jersey jack") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"County Line") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"melting pot") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"dell sales") ) { return "NEWCOMPUTEREQUIP";  }
	if ( gb_strcasestr(d,"MCDONALD'S") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"MCDONALDS") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"keva ju") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"circlek") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"circle k") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"pumpkin patch") ) { return "ENTERTAINMENT";  }
	if ( gb_strcasestr(d,"bio par") ) { return "ENTERTAINMENT";  }
	
	if ( gb_strcasestr(d,"express lighting") ) { return "SUPPLIES";  }
	
	if ( gb_strcasestr(d,"rocky mountain ston") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"geronimo") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"walgreen") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"homedepot") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"godaddy") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"dermatology") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"toys r") ) { return "ENTERTAINMENT";  }
	if ( gb_strcasestr(d,"taco bueno") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"del taco") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"carl's jr") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"quizno") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"vitamindinc") ) { return "SOFTWARE";  }
	if ( gb_strcasestr(d,"la crepe") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"pappadeaux") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"just muffin") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"cajun kitchen") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"viet q") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"annapurnas") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"cedar crest mart") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"flying star") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"pc tools") ) { return "SOFTWARE";  }
	if ( gb_strcasestr(d,"symantec") ) { return "SOFTWARE";  }
	if ( gb_strcasestr(d,"Rotisserie") ) { return "FOOD";  }
	
	if ( gb_strcasestr(d,"dickeys") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"bagels") ) { return "FOOD";  }
	
	if ( gb_strcasestr(d,"biz furni") ) { return "FURNITURE";  }
	if ( gb_strcasestr(d,"Plink Computer") ) { return "NEWCOMPUTEREQUIP";  }
	if ( gb_strcasestr(d,"all breakers") ) { return "SUPPLIES";  }
	
	
	
	if ( gb_strcasestr(d,"Einstein Bros") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Dunkin") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Smiths") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Scarpas") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Starlight") ) { return "ENTERTAINMENT";  }
	if ( gb_strcasestr(d,"Qwest") ) { return "DATASVC";  }
	if ( gb_strcasestr(d,"Enterprise") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"Usps") ) { return "SHIPPING";  }
	if ( gb_strcasestr(d," Inn") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"Staples") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"AAA") ) { return "INSURANCE";  }
	if ( gb_strcasestr(d,"Monthly Maint") ) { return "BANKFEES";  }
	if ( gb_strcasestr(d,"Sweep-Dividend") ) { return "INTEREST";  }
	if ( gb_strcasestr(d,"Duffy Bro") ) { return "RENT";  }
	if ( gb_strcasestr(d,"Authorize.Net") ) { return "DATASVC";  }
	if ( gb_strcasestr(d,"Wal Sam") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"Albuquerque Publi") ) { return "ADVERTISING";  }
	if ( gb_strcasestr(d,"Presbyter") ) { return "INSURANCE";  }
	if ( gb_strcasestr(d,"Home Decora") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"Frontierair") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"US Patent") ) { return "LEGAL";  }
	if ( gb_strcasestr(d,"IRS") ) { 
		if ( strstr(d,"ID:220847700378672") ) return "IRSINCOMETAX";
		return "TAXES";  
	}
	if ( gb_strcasestr(d,"Internal revenue service") ) { return "TAXES";  }
	
	if ( gb_strcasestr(d,"Delta Dental") ) { return "INSURANCE";  }
	if ( gb_strcasestr(d,"Wal Wal") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"Southwestair") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"High Desert Graph") ) { return "ADVERTISING";  }
	if ( gb_strcasestr(d,"Whole Foods") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Indigo Gall") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"Jezebel") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"Netgrocer") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"India Palac") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Google_*ad") ) { return "ADVERTISING";  }
	if ( gb_strcasestr(d,"Google *ad") ) { return "ADVERTISING";  }
	if ( gb_strcasestr(d,"charcoal med") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"rons roost") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Barley Room") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Micro Electr") ) { return "NEWCOMPUTEREQUIP";  }
	if ( gb_strcasestr(d,"Cafe Miche") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Quarters") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Soopers") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Intuit") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"Fedex") ) { return "SHIPPING";  }
	if ( gb_strcasestr(d,"Newegg") ) { return "NEWCOMPUTEREQUIP";  }
	if ( gb_strcasestr(d,"Salesjobs") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"American Ai") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"Tax&") ) { return "TAXES";  }
	if ( gb_strcasestr(d,"Coffee") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"Usair") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"Delta Air") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"Jetblue") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"Click-N-Ship") ) { return "SHIPPING";  }
	if ( gb_strcasestr(d," Cafe") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Find Rfp") ) { return "DATASVC";  }
	if ( gb_strcasestr(d,"Elephant Walk") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Homestead") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"Starbucks") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Hotels") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"Bertucci") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Ice Cream") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Office Max") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"Thirfty") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d," Diner") ) { return "FOOD";  }
	if ( gb_strcasestr(d," Hotel") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"Compusa") ) { return "NEWCOMPUTEREQUIP";  }
	if ( gb_strcasestr(d,"Hourly Sick") ) { return "PAYROLL";  }
	if ( gb_strcasestr(d,"Hourly wage") ) { return "PAYROLL";  }
	if ( gb_strcasestr(d,"Commission") ) { return "PAYROLL";  }
	if ( gb_strcasestr(d,"Hourly Vacation") ) { return "PAYROLL";  }
	if ( gb_strcasestr(d,"On the Ave") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"Sea Food") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Staples") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"Mass EFT") ) { return "TAXES";  }
	if ( gb_strcasestr(d,"Intelius") ) { return "DATASVC";  }
	if ( gb_strcasestr(d,"Buy.com") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"Voipsupply") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"pmidentity") ) { return "SUPPLIES";  }
	
	if ( gb_strcasestr(d,"CO - Total Sur") ) { return "TAXES";  }
	if ( gb_strcasestr(d,"bizchair") ) { return "FURNITURE";  }
	if ( gb_strcasestr(d,"Bizchair") ) { return "FURNITURE";  }
	if ( gb_strcasestr(d," Grill") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Wal-Mart") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"Fornaio") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Cool River") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Saloon") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Giant") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"Shamrock") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"US Pto") ) { return "LEGAL";  }
	if ( gb_strcasestr(d,"Best Buy") ) { return "NEWCOMPUTEREQUIP";  }
	if ( gb_strcasestr(d,"Standard Restaurant") ) { return "FURNITURE";  }
	if ( gb_strcasestr(d," Restaurant") ) { return "FOOD";  }
	if ( gb_strcasestr(d," Ups") ) { return "SHIPPING";  }
	if ( gb_strcasestr(d,"Cogent") ) { return "DATASVC";  }
	if ( gb_strcasestr(d,"High Desert Web") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"Hartford") ) { return "INSURANCE";  }
	if ( gb_strcasestr(d,"VIII Sycamore") ) { return "RENT";  }
	if ( gb_strcasestr(d,"Domain Directo") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"Delaware Corp Fil") ) { return "LEGAL";  }
	if ( gb_strcasestr(d,"Bottled Wat") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"FedEx") ) { return "SHIPPING";  }
	if ( gb_strcasestr(d,"InfoRelay") ) { return "DATASVC";  }
	if ( gb_strcasestr(d,"Valentia") ) { return "RENT";  }
	if ( gb_strcasestr(d,"Paint-Bal") ) { return "ENTERTAINMENT";  }
	if ( gb_strcasestr(d,"& Hansen") ) { return "LEGAL";  }
	if ( gb_strcasestr(d,"Front End Audio") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"Comcast") ) { return "DATASVC";  }
	if ( gb_strcasestr(d,"Twin Micro") ) { return "NEWCOMPUTEREQUIP";  }
	if ( gb_strcasestr(d,"Sierra Spring") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"Betty Mills") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d," Suites") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"City of Albuq") ) { return "TAXES";  }
	if ( gb_strcasestr(d,"Muehlmeyer") ) { return "LEGAL";  }
	if ( gb_strcasestr(d,"Colo4") ) { return "DATASVC";  }
	if ( gb_strcasestr(d,"Global Netopte") ) { return "DATASVC";  }
	if ( gb_strcasestr(d,"SOS of New") ) { return "TAXES";  }
	if ( gb_strcasestr(d,"Monster") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"Technology Ventur") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"Walmart") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"Gardenocity") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"Ds Waters") ) { return "SUPPLIES";  }
	
	if ( gb_strcasestr(d,"Thrifty") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"Prezza") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"On The Ave") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"Buy.com") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"Lowe") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"amazon") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d," Bar ") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"2co.com") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"Jobing") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"Nothing But") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"American Endowment") ) { return "CHARITY";  }
	if ( gb_strcasestr(d,"Lotaburger") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Escobedo") ) { return "MAINTEN";  }
	if ( gb_strcasestr(d,"Peacock") ) { return "LEGAL";  }
	if ( gb_strcasestr(d,"Porsche") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"Samsclub") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"Agents and Corp") ) { return "LEGAL";  }
	if ( gb_strcasestr(d,"Acct Analys") ) { return "BANKFEES";  }
	
	if ( gb_strcasestr(d,"Advanced Power") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"Poster Complia") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"Prairie Star") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Sushi") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"ALLTEL") ) { return "DATASVC";  }
	if ( gb_strcasestr(d,"Protectplus") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d," Gas Stat") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"Warrior 66") ) { return "TRAVEL";  } // gas
	if ( gb_strcasestr(d,"Westridge 66") ) { return "TRAVEL";  } // gas
	if ( gb_strcasestr(d," Grill") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Segal &") ) { return "LEGAL";  }
	if ( gb_strcasestr(d,"Title Co") ) { return "LEGAL";  }
	if ( gb_strcasestr(d," Klein") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"Soekris") ) { return "COMPUTERASSET";  }
	if ( gb_strcasestr(d,"Gamestop") ) { return "SOFTWARE";  }
	if ( gb_strcasestr(d,"Acteva.com") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"Avtech") ) { return "PROFESSIONAL";  }
	if ( gb_strcasestr(d,"Shell Oil") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"Michael Wright") ) { return "DATACTR";  }
	if ( gb_strcasestr(d,"Subway") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Hospitality") ) { return "TRAVEL";  }
	
	if ( gb_strcasestr(d,"Aldo") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"Mccormick") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"Loaf N Jug") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"Better Stop") ) { return "TRAVEL";  }
	if ( gb_strcasestr(d,"Amz") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"Borders") ) { return "SUPPLIES";  }
	if ( gb_strcasestr(d,"Bistro") ) { return "FOOD";  }
	if ( gb_strcasestr(d,"UPS") ) { return "SHIPPING";  }
	if ( gb_strcasestr(d,"Chili") ) { return "FOOD";  }
	
	if ( gb_strcasestr(d,"Alltel") ) { return "DATASVC";  }
	
	if ( gb_strcasestr(d,"Transameri") ) { 
		// they are just charging us fees in 2009+ it seems
		//if ( t->m_year >= 2009 ) return "PROFESSIONAL";
		//else                     return "RETIREMENT"; 
		return "RETIREMENT";
	}

		if ( gb_strcasestr(d," Cake") ) { return "FOOD";  }

		if ( gb_strcasestr(d,"Target") ) { return "SUPPLIES";  }
		if ( gb_strcasestr(d,"Aviation") ) { return "TRAVEL";  }
		if ( gb_strcasestr(d,"Lenovo") ) { return "NEWCOMPUTEREQUIP";  }
		if ( gb_strcasestr(d,"Spain 66") ) { return "SUPPLIES";  }
		if ( gb_strcasestr(d," Food") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"Abaca") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Hallmark") ) { return "SUPPLIES";  }
		if ( gb_strcasestr(d,"Cookies") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"Club Mem") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Floating Holi") ) { return "PAYROLL";  }
		if ( gb_strcasestr(d,"Olive Gard") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"MA Departm") ) { return "TAXES";  }
		if ( gb_strcasestr(d,"Paid Holid") ) { return "PAYROLL";  }
		//if ( gb_strcasestr(d,"Commercial Loans") ) { return "LOAN";  }
		if ( gb_strcasestr(d,"Toyota") ) { return "CAREXPENSE";  }
		if ( gb_strcasestr(d,"Homebase") ) { return "SUPPLIES";  }
		if ( gb_strcasestr(d,"drugstore") ) { return "SUPPLIES";  }
		if ( gb_strcasestr(d,"rebekah") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"GP Sycamore") ) { return "RENT";  }
		if ( gb_strcasestr(d,"Overtime") ) { return "PAYROLL";  }
		if ( gb_strcasestr(d,"NM Taxation") ) { return "TAXES";  }
		if ( gb_strcasestr(d,"Tas ") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Sneakerz") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"Summit Electric") ) { return "SUPPLIES";  } // MDW

		if ( gb_strcasestr(d,"Deborah Berger") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Judy Heck") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"US Treasu") ) { return "TAXESUSTREAS";  }
		if ( gb_strcasestr(d,"Vietnamese") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"Rambler") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"Metzger") ) { return "CAPITALASSETS";  } // MDW
		if ( gb_strcasestr(d,"PC Club") ) { return "NEWCOMPUTEREQUIP";  }
		if ( gb_strcasestr(d,"Acma Computer") ) { return "NEWCOMPUTEREQUIP";  }
		if ( gb_strcasestr(d,"pcnation") ) { return "NEWCOMPUTEREQUIP";  }
		if ( gb_strcasestr(d,"deep surplus") ) { return "NEWCOMPUTEREQUIP";  }
		if ( gb_strcasestr(d,"american pwr") ) { return "NEWCOMPUTEREQUIP";  }
		if ( gb_strcasestr(d,"saras day spa") ) { return "ENTERTAINMENT";  }
		if ( gb_strcasestr(d,"stone age climb") ) { return "ENTERTAINMENT";  }
		if ( gb_strcasestr(d,"xtreme hang") ) { return "ENTERTAINMENT";  }
		if ( gb_strcasestr(d,"wild bird") ) { return "SUPPLIES";  }
		if ( gb_strcasestr(d,"chef du jo") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"casa veja") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"aeromexico") ) { return "TRAVEL";  }
		if ( gb_strcasestr(d,"twisters") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"gnc") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"gas company") ) { return "UTILITIES";  }
		if ( gb_strcasestr(d,"hello deli") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"uspto") ) { return "LEGAL";  }
		if ( gb_strcasestr(d,"gourav") ) { return "PAYROLL";  }
		

		
		if ( gb_strcasestr(d,"Best Choice Glass") ) { return "PROFESSIONAL";  } // MDW
		if ( gb_strcasestr(d,"google *adws") ) { return "ADVERTISING";  } // MDW
		if ( gb_strcasestr(d,"madu ") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"French 250") ) { return "SUPPLIES";  }
		if ( gb_strcasestr(d,"Lawit, PC") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"beach audio") ) { return "NEWCOMPUTEREQUIP";  }
		if ( gb_strcasestr(d,"colamco") ) { return "NEWCOMPUTEREQUIP";  }
		if ( gb_strcasestr(d,"overland") ) { return "SHIPPING";  }
		if ( gb_strcasestr(d,"acme micro") ) { return "NEWCOMPUTEREQUIP";  }
		if ( gb_strcasestr(d,"vicino") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"nick & jim") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"pizza") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"chama river") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"chez axel") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"brewing") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"7-eleven") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"applebees") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"mvd express") ) { return "FEES";  }
		if ( gb_strcasestr(d,"fitness store") ) { return "FURNITURE";  }
		if ( gb_strcasestr(d,"adams county") ) { return "LEGAL";  }
		if ( gb_strcasestr(d,"velocity products") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"The Home D") ) { return "SUPPLIES";  }
		if ( gb_strcasestr(d,"PR Newswire") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"minuteman press") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Pub Svc New") ) { return "UTILITIES";  }
		if ( gb_strcasestr(d,"Texas Land") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"italian kitchen") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"pf changs") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"classic roc") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"officescapes") ) { return "FURNITURE";  }
		if ( gb_strcasestr(d,"Tommy") ) { return "TRAVEL";  }
		if ( gb_strcasestr(d,"namescout") ) { return "FEES";  }
		if ( gb_strcasestr(d,"medcom") ) { return "SUPPLIES";  }
		if ( gb_strcasestr(d,"tlc ") ) { return "PROFESSIONAL";  } // plumbing
		if ( gb_strcasestr(d," tlc") ) { return "PROFESSIONAL";  } // plumbing
		if ( gb_strcasestr(d," bernalillo tire") ) { return "PROFESSIONAL";  } // plumbing
		if ( gb_strcasestr(d,"dwights glass") ) { return "FURNITURE";  } // plumbing
		if ( gb_strcasestr(d,"chocolate") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"itunes") ) { return "SOFTWARE";  }
		if ( gb_strcasestr(d,"Sonny Bryan") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"Shell Serv") ) { return "TRAVEL";  }
		if ( gb_strcasestr(d,"roberts oil") ) { return "TRAVEL";  }
		if ( gb_strcasestr(d,"smx third") ) { return "TRAVEL";  }
		if ( gb_strcasestr(d,"penske") ) { return "SHIPPING";  }
		if ( gb_strcasestr(d,"Exel ") ) { return "SHIPPING";  }
		if ( gb_strcasestr(d,"Dairy Quee") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"taste of") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"zea alba") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"saffron tig") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"springer and stein") ) { return "LEGAL";}
		if ( gb_strcasestr(d,"cty of alb park") ) { return "TRAVEL";}
		if ( gb_strcasestr(d,"IN-N-OUT") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"india palace") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"zeas rotiss") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"pars cuisine") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"owners.com") ) { return "PROFESSIONAL"; }
		if ( gb_strcasestr(d,"travel insurance") ) { return "PROFESSIONAL"; }
		if ( gb_strcasestr(d,"audible") ) { return "SUPPLIES"; }
		// haunted house:
		if ( gb_strcasestr(d,"realm of dark") ) { return "ENTERTAINMENT"; }		
		if ( gb_strcasestr(d,"cleverbridge") ) { return "SUPPLIES"; }		
		if ( gb_strcasestr(d,"ohkay eagle mart") ) { return "SUPPLIES"; }		
		

		if ( gb_strcasestr(d,"westin") ) { return "TRAVEL";  }
		if ( gb_strcasestr(d,"Hyatt") ) { return "TRAVEL";  }
		if ( gb_strcasestr(d,"Chaparral") ) { return "REPAIRS";  }
		if ( gb_strcasestr(d,"Motor Vehicle") ) { return "FEES";  }
		if ( gb_strcasestr(d,"dan rask") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"taxi") ) { return "TRAVEL";  }
		if ( gb_strcasestr(d,"delaware corp & Tax") ) { return "CORPTAX";  }
		if ( gb_strcasestr(d,"retroactive hours") ) { return "PAYROLL";  }
		if ( gb_strcasestr(d,"security design") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Premier Flooring") ) { return "PROFESSIONAL";  } // MDW
		if ( gb_strcasestr(d,"tile guys") ) { return "PROFESSIONAL";  } // MDW
		if ( gb_strcasestr(d,"cuisine") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"chamber of comm") ) { return "fee";  }
		if ( gb_strcasestr(d,"hoover") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"chevron") ) { return "TRAVEL";  }
		if ( gb_strcasestr(d,"steve armbrecht") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"authnet") ) { return "DATASVC";  }
		if ( gb_strcasestr(d,"domainpeople") ) { return "DATASVC";  }
		// accounting
		if ( gb_strcasestr(d,"Atkinson & Co") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"united air") ) { return "TRAVEL";  }
		if ( gb_strcasestr(d,"case mod.com") ) { return "NEWCOMPUTEREQUIP";  }
		if ( gb_strcasestr(d,"Western Disposal") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Lexus") ) { return "PROFESSIONAL";  }

		if ( gb_strcasestr(d,"christmas") ) { return "SUPPLIES";  }
		if ( gb_strcasestr(d,"City of Cambridge") ) { return "FEES";  }
		if ( gb_strcasestr(d,"Richard Kaip") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Denny's") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"Colorado Department of Rev") ) { return "TAXES";  }
		if ( gb_strcasestr(d,"Grainger") ) { return "SUPPLIES";  }
		if ( gb_strcasestr(d,"Tom Richards") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Accountemps") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Home Depot") ) { return "SUPPLIES";  }
		if ( gb_strcasestr(d,"Fairway Inc") ) { return "CAPITALASSETS";  } // MDW
		if ( gb_strcasestr(d,"Eurodns") ) { return "DATASVC";  }
		if ( gb_strcasestr(d,"OfficeTeam") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Lobo ") ) { return "DATASVC";  }
		if ( gb_strcasestr(d,"cnsp") ) { return "DATASVC";  }
		if ( gb_strcasestr(d,"southwestcom") ) { return "TRAVEL";  }
		if ( gb_strcasestr(d,"last chance") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"chuck e ch") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"eventwidget") ) { return "DATASVC";  }
		if ( gb_strcasestr(d,"codero") ) { return "DATASVC";  }
		if ( gb_strcasestr(d,"silva lane") ) { return "ENTERTAINMENT";  }
		if ( gb_strcasestr(d,"monkey mania") ) { return "ENTERTAINMENT";  }
		if ( gb_strcasestr(d,"prickly pear") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"chophouse") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"tax consult") ) { return "LEGAL";  }
		if ( gb_strcasestr(d,"sonic drive") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"yahoo *dir") ) { return "DATASVC";  }
		if ( gb_strcasestr(d,"google*") ) { return "DATASVC";  }
		if ( gb_strcasestr(d,"logotourn") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"radioshack") ) { return "SUPPLIES";  }
		if ( gb_strcasestr(d,"little caesar") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"kfc") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"county treasurer") ) { return "TAXES";  }
		
		if ( gb_strcasestr(d,"ultimed") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"rio luna fam") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"loopnet") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"haddon") ) { return "LEGAL";  }
		if ( gb_strcasestr(d,"Schlotzsky") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"hibachi") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"double drag") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"donut mart") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"chile rio") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"Amicis") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"Fry'S") ) { return "SUPPLIES";  }
		if ( gb_strcasestr(d,"Outback") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"Banana Leaf") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"Palo Alto Limo") ) { return "TRAVEL";  }
		if ( gb_strcasestr(d,"Smoke") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"Sunny Buffet") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"Lucky ") ) { return "TRAVEL";  }
		if ( gb_strcasestr(d,"Locksmith") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Orange Cab") ) { return "TRAVEL";  }
		if ( gb_strcasestr(d,"Dion's") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"Ihop") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"Bennigans") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"Expedia") ) { return "TRAVEL";  }
		if ( gb_strcasestr(d,"Mexico Taxation") ) { return "TAXES";  }
		if ( gb_strcasestr(d,"Costco") ) { return "SUPPLIES";  }
		if ( gb_strcasestr(d,"Hilton") ) { return "TRAVEL";  }
		if ( gb_strcasestr(d,"Western Assur") ) { return "INSURANCE";  }
		if ( gb_strcasestr(d,"Empire Burr") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"Alpha Graphics") ) { return "SUPPLIES";  }
		if ( gb_strcasestr(d,"Logobee") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Better Business B") ) { return "FEES";  }
		if ( gb_strcasestr(d,"Sadies") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"Buzz Elect") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Crown TV") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Reimbursement Exp") ) { return "TRAVEL";  }
		if ( gb_strcasestr(d,"United Healthc") ) { return "INSURANCE";  }
		if ( gb_strcasestr(d,"PNM") ) { return "UTILITIES";  }
		if ( gb_strcasestr(d,"Sweet Toma") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"Van Line") ) { return "SHIPPING";  }
		if ( gb_strcasestr(d,"Fe Business Solut") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Severance pay") ) { return "PAYROLL";  }
		if ( gb_strcasestr(d,"stock bldg suppl") ) { return "SUPPLIES";  } // MDW
		if ( gb_strcasestr(d,"choice steel") ) { return "SUPPLIES";  } // MDW
		if ( gb_strcasestr(d,"richard baker") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"red robin") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"contractors heating") ) { return "PROFESSIONAL";  } // MDW
		if ( gb_strcasestr(d,"james fulmer") ) { return "DATACTR";  }
		if ( gb_strcasestr(d,"states crane") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"dakota hale") ) { return "DATACTR";  }
		if ( gb_strcasestr(d,"trae hale") ) { return "DATACTR";  }
		if ( gb_strcasestr(d,"terry moorehead") ) { return "DATACTR";  }
		if ( gb_strcasestr(d,"scott switzer") ) { return "DATACTR";  }
		if ( gb_strcasestr(d,"glenn williams") ) { return "DATACTR";  }
		if ( gb_strcasestr(d,"inphonex") ) { return "DATASVC";  }
		if ( gb_strcasestr(d,"johnstone supply") ) { return "SUPPLIES";  }
		if ( gb_strcasestr(d,"trader joe") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"anjel roman") ) { return "DATACTR";  }
		if ( gb_strcasestr(d,"landry") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"fitness superstore") ) { return "FURNITURE";  }
		if ( gb_strcasestr(d,"rental service") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"tigerdirect") ) { return "NEWCOMPUTEREQUIP";  }
		if ( gb_strcasestr(d,"paul partin") ) { return "DATACTR";  }
		if ( gb_strcasestr(d,"gorman industries") ) { return "CAPITALASSETS";  } // MDW
		if ( gb_strcasestr(d,"Fox &") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"Art Sims") ) { return "DATACTR";  }
		if ( gb_strcasestr(d,"Frank's supply") ) { return "SUPPLIES";  }
		if ( gb_strcasestr(d,"Luke Carlile") ) { return "DATACTR";  }
		if ( gb_strcasestr(d,"Ferguson Ent") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Colorado Secretary") ) { return "TAXES";  }
		if ( gb_strcasestr(d,"taxandrev") ) { return "TAXES";  }
		if ( gb_strcasestr(d,"jason wiedeman") ) { return "DATACTR";  }
		if ( gb_strcasestr(d,"Surfaces ") ) { return "SUPPLIES";  } // MDW
		if ( gb_strcasestr(d,"Heights Key") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Carlile Electrical") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Glen williams") ) { return "DATACTR";  }
		if ( gb_strcasestr(d,"jimmie carlile") ) { return "DATACTR";  }
		if ( gb_strcasestr(d,"steelmailb") ) { return "CAPITALASSETS";  } // MDW
		if ( gb_strcasestr(d,"efax") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"asset manage") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"adele services") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Blockbuster.com") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d," Tavern") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"netflix") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"carib display") ) { return "FURNITURE";  }
		if ( gb_strcasestr(d,"albertson") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"simplex grinnel") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"water utility") ) { return "UTILITIES";  }
		if ( gb_strcasestr(d,"californiapizza") ) { return "FOOD";  }
		if ( gb_strcasestr(d,"bostons albu") ) { return "FOOD";  }


		if ( gb_strcasestr(d,"petsmart") ) { return "SUPPLIES";  }
		if ( gb_strcasestr(d,"daniel duran") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"border states elec") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Allen Salazar") ) { return "DATACTR";  }
		if ( gb_strcasestr(d,"Valve Soft") ) { return "SOFTWARE";  }
		if ( gb_strcasestr(d,"stamps.com") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"polytec ") ) { return "SUPPLIES";  }

		if ( gb_strcasestr(d,"Customer Withdrawal Image") && amt==-28250.00) { return "RENT";  }
		if ( gb_strcasestr(d,"Customer Withdrawal Image") ) {
			if ( amt>=-303118 && amt <=-303110) { return "MADRIDPROPERTY";  }
		}

		// this is the secondary mortgage on matt's house
		if ( gb_strcasestr(d,"Rls - Debit") ) { 
			return "GBHOMELOAN";  }

		// was PETTYCASH
		if ( amt >= -600.00 && gb_strcasestr(d," withdrwl") ) { 
			if ( g_ismatt ) return "PETTYCASH";
			else return "BLDGMAINT";  
		}

		if ( amt >= -600.00 && gb_strcasestr(d," withdrawal") ) { 
			if ( g_ismatt ) return "PETTYCASH";
			else return "BLDGMAINT";  
		}


		if ( gb_strcasestr(d," withdrwl") && ! g_ismatt ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d," withdrawal") && ! g_ismatt ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Des:Software") ) { return "SOFTWARE";  }
		if ( gb_strcasestr(d,"Des:Fee") ) { return "BANKFEES";  }
		if ( gb_strcasestr(d,"recovery for forced") ) { return "BANKFEES";  }

		if ( gb_strcasestr(d,"ONLINE BUSINESS SUITE") ) {
			return "BANKFEE"; }

		if ( gb_strcasestr(d,"cna ") ) { 
			// gb corp had car insurance from cna
			if ( g_isgbcorp ) { return "CARINSURANCE";}
			// but gb llc had bldg insurance
			return "BOGANINSURANCE";  
		}
		if ( gb_strcasestr(d,"7-11 ") ) { return "SUPPLIES";  }
		if ( gb_strcasestr(d,"Interest Earned") ) { return "INTEREST";  }
		if ( gb_strcasestr(d,"Citylink") ) { return "DATASVC";  }
		if ( gb_strcasestr(d,"Taxation and Rev") ) { return "TAXES";  }
		if ( gb_strcasestr(d,"Custom-Air") ) { return "PROFESSIONAL";  } // MDW
		if ( gb_strcasestr(d,"solomon Froh") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"City of Greenwood ") ) { return "TAXES";  }
		if ( gb_strcasestr(d,"CO Department of Labor") ) { return "TAXES";  }
		if ( gb_strcasestr(d,"Commonwealth of Mass") ) { return "TAXES";  }
 		if ( gb_strcasestr(d,"Jaime Gasson") ) { return "DATACTR";  }
		if ( gb_strcasestr(d,"Color New Mexico") ) { return "DATACTR";  } // MDW
		if ( gb_strcasestr(d,"picsearch") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Matt French") ) { return "DATACTR";  }

		if ( gb_strcasestr(d,"WilmerHale") ) { return "LEGAL";  }
		if ( gb_strcasestr(d,"Glass Systems") ) { return "CAPITALASSETS";  } // MDW
		if ( gb_strcasestr(d,"Simons Archi") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Arsed Engineering") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Concrete Coring") ) { return "PROFESSIONAL";  } // MDW
		if ( gb_strcasestr(d,"Morrison Media") ) { return "REFUNDCLIENT";  }
		if ( gb_strcasestr(d,"Heizer Paul") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Internet Mktg") ) { return "REFUNDCLIENT";  }
		if ( gb_strcasestr(d,"Law Office") ) { return "LEGAL";  }
		if ( gb_strcasestr(d,"Corrales Plumbing") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Harry Caplan") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Gilkey and") ) { return "LEGAL";  }
		if ( gb_strcasestr(d,"Human Rights Bureau") ) { return "LEGAL";  }
		if ( gb_strcasestr(d,"Deposit") ) { return "INCOME";  }
		if ( amt > 0.0 &&
		     gb_strcasestr(d,"Wire Type") ) { return "INCOME";  }


		if ( gb_strcasestr(d,"ZBA Transfer") ) { return "PAYROLL";  }

		if ( g_isgbcorp && gb_strcasestr(d,"transfer to Chk 0265") ) return "SHARELOAN";

		if ( gb_strcasestr(d,"transfer from Chk 0265") ) { return "MATTSOFTLICINTEREST";  }


		// matt paying back llc loan to gb's saving's account
		if ( g_ismatt && gb_strcasestr(d,"transfer to sav 1271") ) {
			return "LLCLOAN"; }

		if ( gb_strcasestr(d,"Acma") ) { return "SUPPLIES";  }
		if ( gb_strcasestr(d,"interfocal") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"jessica caplan") ) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"clematis") ) { return "PROFESSIONAL";  }
		// bogan mortgage/rent
		if ( gb_strcasestr(d,"Commercial Loans Debit") ) { return "BOFABOGANLOAN";  }


		if ( gb_strcasestr(d,"aylgq-Book Transfer") ) { return "BOGANRENT";  }


		if ( gb_strcasestr(d,"-Book Transfer") ) { return "IGNORE";  }
		if ( gb_strcasestr(d,"Funds Transfer") ) { return "IGNORE";  }


		// . this is gb corp paying rent to gb llc
		// . seen as income to gb llc
		//if ( gb_strcasestr(d,"Online scheduled transfer to Chk 2971") ) { return "LLCLOAN";  } // OVERPAYERROR
		if ( gb_strcasestr(d,"Automatic Transfer To 2971") ) { return "BOGANRENT";  }
		
		if ( pamt >= 5498 && pamt <=5499 &&
		  gb_strcasestr(d,"Online Banking transfer from Chk 2971") ) {
			// this was a double recurring payments schedule error
			// by bofa agents, so just pretend this is still
			// gigablast's money, but just sitting in this account.
			// we periodically transfer it back to gigablast.
			// TODO: put this back to IGNORE (MDW 2/3/13)
			//if ( t->m_year <= 2010 ) return "LLCLOAN";
			//else                     return "IGNORE";
			return "LLCLOAN";
		}
		if ( pamt >= 5498 && pamt <=5499 &&
		   gb_strcasestr(d,"Online scheduled transfer to Chk 2971") ){
			// TODO: put this back to IGNORE (MDW 2/3/13)
			//if ( t->m_year <= 2010 ) return "LLCLOAN";
			//else                     return "IGNORE";
			return "LLCLOAN";
			//return "IGNORE"; //return "BOGANRENT";  }
		}


		// for manually labelling/inserting transactions in the
		// account files that are just inter-account transactions
		if ( gb_strcasestr(d,"IGNOREME") )
			return "IGNORE";

		// . LLC loan principal repayment.
		// . trying to fix the bank's double recurring payment error
		//   from gigablast inc to gigablast llc
		// . otherwise it gets set to "IGNORE" below
		// 10/03/11 +100000.00 1284    Online Banking transfer from Chk 2971
		if ( ! g_ismatt &&
		     pamt >= 99999.0 && 
		     pamt <= 100000.01 &&
		     gb_strcasestr(d,"Online Banking transfer from Chk 2971"))
			return "LLCLOAN";

		// gb llc repaid 60,000.00 on 12/27/12 to gb inc
		if ( ! g_ismatt &&
		     pamt >= 10140.00 && 
		     pamt <= 10150.00 &&
		     gb_strcasestr(d,"Online Banking transfer from Chk 2971"))
			return "LLCLOAN";


		// gb llc repaid 60,000.00 on 12/27/12 to gb inc
		if ( ! g_ismatt &&
		     pamt >= 49000.00 && 
		     pamt <= 65000.00 &&
		     gb_strcasestr(d,"Online Banking transfer from Chk 2971"))
			return "LLCLOAN";

		if ( ! g_ismatt &&
		     gb_strcasestr(d,"Online Banking transfer from Chk 2971")){
			return "IGNORE";  }


		if ( pamt >= 5498 && pamt <= 5499 &&
		  gb_strcasestr(d,"Online scheduled transfer from Chk 6550")){ 
			// this is to the gb llc acct and was an overpayment 
			// error on the part of bofa agents
			//if ( g_ismatt ) { return "IGNORE";  } // OVERPAYERROR
			if ( g_ismatt ) return "LLCLOAN";
			// what is it then?
			char *xx=NULL;*xx=0; 
		}


		// transfer to 1255 is webresearch properties, basically gb
		if( ! g_ismatt &&
		    gb_strcasestr(d,"Online Banking transfer to Chk 1255") ){
			return "IGNORE"; }
		if( ! g_ismatt &&
		    gb_strcasestr(d,"Online Banking transfer from Chk 1255") ){
			return "IGNORE"; }
		// transfer to advantage licensing / massive shield, same as gb
		if( ! g_ismatt &&
		    gb_strcasestr(d,"Online Banking transfer to Chk 2044") ){
			return "IGNORE"; }



		// either a software license to matts personal checking or 
		// bogan rent to gb llc acct
		if(gb_strcasestr(d,"Online scheduled transfer from Chk 6550")){
			if ( g_ismatt ) { 
				return "LLCLOAN";  } // OVERPAYERROR (line #2)
			// what is it then?
			char *xx=NULL;*xx=0; 
		}

		// the final transfer to close 7418 (matt wells llc )was to 
		// 1271. repay the bogan loan
		if ( pamt >= 10251.39 && pamt <= 10251.41 &&
		     gb_strcasestr(d,"Online Banking transfer from Chk 7418")){ 
			if ( g_ismatt ) { char *xx=NULL;*xx=0; }
			return "LLCLOAN";
		}

		// the final transfer to close 1255 (web research projects llc)
		// was on 12/27/2012 into account 7418, our savings acct
		if ( pamt >= 15311.99 && pamt < 15312.01 &&
		     gb_strcasestr(d,"Online Banking transfer to Chk 7418")){ 
			if ( g_ismatt ) { char *xx=NULL;*xx=0; }
			return "IGNORE";
		}

		// we closed the advantage licensing llc account used to
		// pay event guru turks.  TODO: record the expenses from
		// paying the turks? the transfer is into account 1284
		// or into account 1271. i've seen both.
		if ( gb_strcasestr(d,"Online Banking transfer from CHK 2044")){
 			if ( g_ismatt ) { char *xx=NULL;*xx=0; }
			return "IGNORE";
		}
		// from 1284 into 2044, advantage licensing
		if ( gb_strcasestr(d,"Online Banking transfer to CHK 2044")){
 			if ( g_ismatt ) { char *xx=NULL;*xx=0; }
			return "IGNORE";
		}
		

		// 7418 is matt wells llc and not used now
		if ( pamt >= 999 && pamt <= 1001 &&
		     gb_strcasestr(d,"Online Banking transfer to Chk 7418")){ 
			// gb corp put $1k in there to prevent bank fees
			// TODO: put this back to IGNORE (MDW 2/3/13)
			//if ( t->m_year <= 2010 ) return "LLCLOAN";
			//else                     return "IGNORE";
			return "LLCLOAN";
			// this is an overpayment transfer error done
			// on the part of bofa agents. so just pretend
			// this money, the 5498.80 is still in gb inc.'s 
			// control.
		// No, because it's easier for now to just be a loan since
		// that is how we recorded it in 2009 and 2010 returns.
			//return "IGNORE";
		}

		if ( gb_strcasestr(d,"Online Banking transfer to Chk 7418")){
			// gb corp put $1k in there to prevent bank fees
			//return "LLCLOAN";  // OVERPAYERROR
			// two 5k payments to make up for shortfall i think
			return "BOGANRENT";
		}


		if ( g_ismatt && 
		     gb_strcasestr(d,"Online Banking transfer to Chk 1284")){ 
			// TODO: put this back to IGNORE (MDW 2/3/13)
			// i would say at least for 2011, consider it an error then
			//if ( t->m_year <= 2010 ) return "LLCLOAN";
			//else                     return "IGNORE";
			return "LLCLOAN";
			// ignore repaying of bofa's overpayment error
			//return "IGNORE";
		}

		// 1284 is gb corp's new checking acct
		if ( g_isgbcorp && gb_strcasestr(d,"Online Banking transfer to Chk 1284") ) { return "IGNORE";  }
		if ( g_isgbcorp && gb_strcasestr(d,"Online Banking transfer from Chk 1284") ) { return "IGNORE";  }

		// this is gb corp savings
		if ( g_isgbcorp && gb_strcasestr(d,"Online Banking transfer from Sav 1271") ) { return "IGNORE";  }
		if ( g_isgbcorp && gb_strcasestr(d,"Online Banking transfer to sav 1271") ) { return "IGNORE";  }

		// this is gb corp old checking
		if ( g_isgbcorp && gb_strcasestr(d,"Online Banking transfer from Chk 6550") ) { return "IGNORE";  }
		if ( g_isgbcorp && gb_strcasestr(d,"Online Banking transfer to Chk 6550") ) { return "IGNORE";  }

		// need to categorize this!!
		//if ( gb_strcasestr(d,"Online Banking transfer") ) { 
		//	char *xx=NULL;*xx=0; }

		// this is gb c

		// automatic transfer from 1284
		if ( gb_strcasestr(d,"Des:Amt Trnsfr ID:00439002411284") ){ 
			return "BOGANRENT";  }

		if ( g_ismatt && pamt == 6000.0 &&
		     gb_strcasestr(d,"Des:Amt Trnsfr ID:") ){ 
			return "BOGANRENT";  }

		// penalty to pay out rent to denver office
		if ( gb_strcasestr(d,"boingo")) { return "DATASVC";  }
		// what is this?
		if ( gb_strcasestr(d,"0402 Orb")) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"0401 Orb")) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Opay New Mexico")) { return "PROFESSIONAL";  }
		if ( gb_strcasestr(d,"Paypal") && amt >0.0 ) { return "INCOME";  }
		if ( gb_strcasestr(d,"Counter Credit") ) { return "INCOME";  }
		if ( gb_strcasestr(d,"Counter Debit") ) { return "BLDGMAINT";  }
		if ( gb_strcasestr(d,"window repair")){return "BLDGMAINT";}
		if ( gb_strcasestr(d,"EXPENSEounter Debit") ) { return "BLDGMAINT";  }

		// transfer money between matt and gb llc, basically the
		// same entity for tax purposes
		if ( pamt >= 50000.00 && pamt <= 50000.01 &&
		     g_ismatt &&
		     gb_strcasestr(d,"Confirmation# 3025089052") ) {
			return "IGNORE";
		}

		// this does not include the acct # for some reason
		if ( g_ismatt && gb_strcasestr(d,"transfer from Chk")) return "SHARELOAN";
		if ( g_ismatt && gb_strcasestr(d,"transfer from SAV")) return "SHARELOAN";

		if ( cn>0&&gb_strcasestr(d,"uark") ) { return "TRAVEL";  }
		if ( cn>0&&gb_strcasestr(d,"anney")) { return "TRAVEL";  }
		if ( cn>0&&gb_strcasestr(d,"inad")) { return "TRAVEL";  }
		if ( cn>0&&gb_strcasestr(d,"artap")) { return "TRAVEL";  }
		if ( cn>0&&gb_strcasestr(d,"avier")) { return "TRAVEL";  }
		if ( cn>0&&gb_strcasestr(d,"enjamin")){return "TRAVEL";  }
		if ( cn>0&&gb_strcasestr(d,"sabino")){return "TRAVEL";  }
		if ( cn>0&&gb_strcasestr(d,"magdalena")){return "TRAVEL"; }
		if ( cn>0&&gb_strcasestr(d,"hatlee")){return "TRAVEL"; }
		if ( cn>0&&gb_strcasestr(d,"joel")){return "TRAVEL"; }
		if ( cn>0&&gb_strcasestr(d,"alden")){return "TRAVEL"; }
		if ( cn>0&&gb_strcasestr(d,"tracy")){return "TRAVEL"; }
		if ( cn>0&&gb_strcasestr(d,"alin")){return "TRAVEL"; }
		if ( cn>0&&gb_strcasestr(d,"cramer")){return "TRAVEL"; }

		if ( amt<0 && gb_strcasestr(d,"Wire Type:Wire Out") &&
		     amt<-4754.00 && amt>-4756.00 ) {
			// payment to cogent for fiber to bogan which was
			// refunded several months later, the in 2012
			return "DATASVC";
		}
		// wire to partap who needed it
		if ( amt<0 && gb_strcasestr(d,"Wire Type:Book Out") &&
		     amt<-2974.00 && amt>-2976.00 ) {
			return "PROFESSIONAL";
		}
		if ( amt<0 && gb_strcasestr(d,"Wire Type:Book Out") &&
		     amt<-3997.00 && amt>-3998.00 ) {
			return "PROFESSIONAL";
		}

		if ( pamt >=0.0 && pamt <=.02 &&
		     gb_strcasestr(d,"Online scheduled transfer to CHK 2971"))
			return "BANKFEE";

		//4/18/11 -2975.00   1284 NEGUNKNOWN        442608.81 Wire Type:Book O


		if ( amt < 0 ) { return "NEGUNKNOWN";  }
		{ return "POSUNKNOWN";  }

		// default
		//if ( amt < 150 ) { return "SUPPLIES";  }
		//return "PROFESSIONAL";
		//

		/*

		if ( gb_strcasestr(d,"") ) { return "";  }
		if ( gb_strcasestr(d,"") ) { return "";  }
		if ( gb_strcasestr(d,"") ) { return "";  }
		if ( gb_strcasestr(d,"") ) { return "";  }
		if ( gb_strcasestr(d,"") ) { return "";  }
		if ( gb_strcasestr(d,"") ) { return "";  }
		if ( gb_strcasestr(d,"") ) { return "";  }
		if ( gb_strcasestr(d,"") ) { return "";  }
		if ( gb_strcasestr(d,"") ) { return "";  }
		if ( gb_strcasestr(d,"") ) { return "";  }
		if ( gb_strcasestr(d,"") ) { return "";  }
		if ( gb_strcasestr(d,"") ) { return "";  }
		if ( gb_strcasestr(d,"") ) { return "";  }
		if ( gb_strcasestr(d,"") ) { return "";  }
		if ( gb_strcasestr(d,"") ) { return "";  }
		if ( gb_strcasestr(d,"") ) { return "";  }
		if ( gb_strcasestr(d,"") ) { return "";  }
		if ( gb_strcasestr(d,"") ) { return "";  }
		*/
}


// QBPAYROLL.TXT
// 1. goto to "Employee Center" in QuickBooks
// 2. click on an employee then click on "Payroll Transaction Detail" link
// 3. In that report, set Dates to "All"
// 4. In that report, click "Modify Report" and remove the filter that 
//    contains the employee name
// 5. Export the resulting report into Excell by clicking on "Export"
// 6. Save as a tab-delimted file to /gb/samba/Bank\ Statements/qbpayroll.txt 
//    from excel

// QBDEPOSITS.TXT
// 1. reports-banking->depositDetail
// 2. select "All" for dates
// 3. export to qbdeposits.txt
// 3. then try to match the deposit amount with what we have in register.txt
// 4. when found, add the subdeposits under the parent with the same 
//    transaction id. put an asterisk after the transaction id to indicate 
//    they are children/subdeposits
// 5. skip "paycheck" deposits.

// QBCHECKS.TXT
// 1. goto banking->checkdetail
// 2. select "All" for Dates.
// 3. export
// 4. save as qbchecks.txt
// 5. when parsing just look at "Bill Pmt -Check" lines. although the lines 
//    below it are good descriptions so grab them for that.
// 6. consider stealing the category codes from quickbooks... and using them 
//    as our dst accts

// QBTRANSACTIONS.TXT
// 1. goto Reports->Accountan & Taxes->Transaction List By Date
// 2. save it as qbtransactions.csv (no need to use excel)
// 3. it will be used like qbchecks.txt to match bofa transactions, BUT it will also
//    add its own transactions into the list if no bofa transaction matched it
// 4. add old asset files into qbtransactions-2.csv i guess since 2005+ OR we can try
//    to get a file from ye olde peach tree


char *loadTransactions ( bool newFormat , char *bufPtr , char *bufEnd ) {

	long accyear = g_accyear;

	// save start of buf
	char *buf = bufPtr;

	bool ismatt = g_ismatt;
	bool isgbcorp = g_isgbcorp;

	long ndirs = 0;
	char *dirs[5];
	if ( ismatt ) {
		ndirs = 2;
		dirs[0] = "2971"; // gb llc
		dirs[1] = "0265"; // matt's checking
		// 0041 (credit card jezebel uses)
		// we do not need because we assume none are business
		// expenses, and it is paid off automatically by 2971
		// with a description of "FIA CARD SERVICE"
	}
	if ( isgbcorp ) {
		dirs[0] = "1284"; // new checking
		dirs[1] = "1271"; // savings
		dirs[2] = "6550"; // old checking
		dirs[3] = "1255"; // web research properties llc
		// advantage licensing llc. massive shield llc. 
		// owned by gb inc
		dirs[4] = "2044"; 
		// matt wells llc 
		// closed in dec 27, 2012 with transfer/ of 10,264.40 to 1271
		// but do not count as income. matt wells llc.
		//dirs[5] = "7418"; 
		ndirs = 5;
	}

	char *prefix = "/gb/samba/Bank - bofa";
		
	long nd = 0;

	while ( nd < ndirs ) {

		// open the dir and scan for log files
		//char *dirname = dirs[nd]; // argv[nd];

		char final[1024];
		sprintf(final,"%s/%s",prefix,dirs[nd]);

		// starting in 2012 we just put all in one file and the 
		// account number is in the filename...
		if ( newFormat ) {
			// all account files from all years are now in 
			// this single dir
			sprintf(final,"%s/allstatements/",prefix);
			if ( nd >= 1 ) break;
		}


		char *dirname = final;

		nd++;
		DIR *edir = opendir ( dirname );
		if ( ! edir ) {
			fprintf ( stdout, "ditzy::opendir (\"%s\"):%s\n",
				  dirname,strerror( errno ) );
			return NULL;//-1;
		}
		
		// loop over all the log files in this directory
		struct dirent *ent;
		while ( (ent = readdir ( edir ))  ) {
			char *filename = ent->d_name;
			// skipif not a bofa estatement
			bool goodName = false;
			if ( strncasecmp ( filename , "eStmt_" , 6 ) == 0 ) 
				goodName = true;
			// for >= 2012 allow e0265-02042013.txt
			long accountNumberInFilename = 0;
			if ( filename[0] == 'e' &&
			     isdigit(filename[1]) &&
			     isdigit(filename[2]) &&
			     isdigit(filename[3]) &&
			     isdigit(filename[4]) &&
			     filename[5] == '-' ) {
				accountNumberInFilename = atoi(filename+1);
				long x; for ( x = 0 ; x < ndirs ; x++ ) {
					if ( atoi(dirs[x]) == 
					     accountNumberInFilename )
						break;
				}
				// only read in file if account number is a 
				// match!
				if ( x < ndirs )
					goodName = true;
			}
			if ( ! goodName ) continue;
			// must have .txt
			if ( ! strstr ( filename,".txt" ) ) continue;
			// skip if ends in a ~, it is an emacs backup file
			if ( filename[strlen(filename)-1] == '~' ) continue;
			// must have _<year>
			// no! might be borderline!
			//char need[128];
			//sprintf(need,"_%li",accyear);
			//if ( ! strstr(filename,need) ) continue;
			// make a full filename
			char full[1024];
			sprintf ( full , "%s/%s", dirname,filename);
			// get size
			struct stat stats;
			stats.st_size = 0;
			stat ( full , &stats );
			// return the size if the status was ok
			long size = stats.st_size;
			// skip if 0 or error
			if ( size <= 0 ) {
				fprintf(stdout,"skipping %s: %s",full,
					strerror(errno));
				continue;
			}
			// . we got one, process it
			// . returns -1 on failure
			int fd = open ( full , O_RDONLY );
			if ( fd < 0 ) {
				fprintf(stdout,"open %s : %s\n",
					full,strerror(errno));
				continue;
			}
			// sanity check
			if ( bufPtr + size >= bufEnd ) {
				fprintf(stdout,"size of file %s is %li too "
					"big!",full,size);
				return NULL;//-1;
			}

			// first stamp the account file number becase these 
			// new file formats
			// do not have it, it is only in the filename
			long added = 0;
			if ( accountNumberInFilename ) {
				added = sprintf(bufPtr,
						"Account Number: %li\n",
						accountNumberInFilename);
			}
				

			// read it all in
			int nr = read ( fd , bufPtr + added , size );
			if ( nr != size ) {
				fprintf(stdout,"read %s : %s\n",full,
					strerror(errno));
				continue;
			}
			// increment to include lines we inserted
			nr += added;
			size += added;
			// all done
			close ( fd );
			// note it
			fprintf(stdout,"read %s\n",full);
			// NULL terminate
			bufPtr[nr] = '\0';
			// find "Daily Ledger Balances"
			char *s = strstr ( bufPtr , "Daily Ledger Balances" );
			// acct 0265 uses this one...
			if ( ! s ) s = strstr ( bufPtr, "Daily Balance Summary" );
			// sanity check
			if ( s ) {
				// make that the new size
				size = s - bufPtr;
			}
			else if ( ! newFormat ) {
				fprintf(stdout,"file %s had no ledger\n",full);
			}
			// mark this
			char *mark = "END OF FILE";
			// sanity
			if ( ! s && ! newFormat ) {
				char *end = bufPtr + size - 1 - strlen(mark);
				strcpy ( end,mark);
			}
			else if ( ! newFormat ) { 
				// at least mark this line
				strcpy(s,mark);
				// skip that
				s += 11;
				// mark the new \0
				*s = '\0';
			}

			// remove back to back spaces
			char *dst = bufPtr;
			char *src = bufPtr;
			char lastc = 0;
			for ( ; *src ; src++ ) {
				char c = *src;
				if ( c == '\t' ) c = ' ';
				if ( c == lastc && c == ' ' ) continue;
				lastc = c;
				*dst++ = c;
			}
			*dst = '\0';

			// that is the new size
			size = dst - bufPtr;

			// advance
			bufPtr += size;
			// sanity check
			if ( bufPtr >= bufEnd ) { char *xx=NULL;*xx=0; }
			// separate
			*bufPtr++ = '\n';
			// just in case we are done
			*bufPtr = '\0';

			// next file
			continue;
		}
	}


	// . parse up all files
	// . we start off in the deposits and credits section
	bool inDebitSection = false;
	bool inCreditSection = false;
	bool inOtherSection  = false;
	// parse it up
	// the end of it
	char *pend = bufPtr;
	// set it up
	char *eol = NULL;
	// account number
	char *an = "unknown";
	// unknown
	//long year = 0;
	long year1 = 0;
	long year2 = 0;
	long month1 = 0;
	long month2 = 0;
	char *nextLine = NULL;
	float statementTotal = 0.0;
	//float accYearStartBalance = 0.0;
	//float accYearEndBalance = 0.0;
	// temp vars
	float startBal = 0.0;
	float endBal = 0.0;
	// loop over each line in buffer
	for ( char *p = buf ; p < pend ; p = nextLine ) {//eol + 1 ) {
		// get the end of the line
		eol = strchr ( p , '\n');
		// none? all done with this file
		if ( ! eol ) break;
		// point to next line
		nextLine = eol + 1;
		// skip if empty
		if ( eol - p <= 1 ) continue;
		// remove trailing spaces
		while ( eol-1 > buf && isspace(eol[-1]) ) eol--;
		// terminate it there for the meantime
		*eol = '\0';


		// get withdraw/deposit summary info for sanity checking
		char *tt = "Statement Beginning Balance";
		char *nn = strstr(p,tt);
		if ( nn ) {
			char *pp = nn + strlen(tt);
			startBal = atof2( pp );
			continue;
		}
		tt = "Statement Ending Balance";
		nn = strstr(p,tt);
		if ( nn ) {
			char *pp = nn + strlen(tt);
			endBal = atof2( pp );
			continue;
		}
		tt = "Beginning Balance on";
		nn = strstr(p,tt);
		if ( nn ) {
			char *pp = nn + strlen(tt);
			// skip to $
			for ( ; *pp && *pp != '$' ; pp++ );
			// panic?
			if ( ! *pp ) { char *xx=NULL;*xx=0; }
			startBal = atof2( pp );
			continue;
		}
		tt = "Beginning balance as of ";
		nn = strstr(p,tt);
		if ( nn ) {
			char *pp = nn + strlen(tt);
			// skip to period in amount
			pp = strstr(pp,".");
			// back up
			for ( ; pp && (isdigit(pp[-1])||pp[-1]==','||pp[-1]=='-') ; pp-- );
			// skip to $
			//for ( ; *pp && *pp != '$' ; pp++ );
			// panic?
			if ( ! *pp ) { char *xx=NULL;*xx=0; }
			startBal = atof2( pp );
			continue;
		}
		tt = "Ending Balance on";
		nn = strstr(p,tt);
		if ( nn ) {
			char *pp = nn + strlen(tt);
			// skip to $
			for ( ; *pp && *pp != '$' ; pp++ );
			// panic?
			if ( ! *pp ) { char *xx=NULL;*xx=0; }
			endBal = atof2( pp );
			continue;
		}
		tt = "Ending balance";
		nn = gb_strcasestr(p,tt);
		if ( nn ) {
			char *pp = nn + strlen(tt);
			// skip to period in amount
			pp = strstr(pp,".");
			// back up
			for ( ; pp && (isdigit(pp[-1])||pp[-1]==','||pp[-1]=='-') ; pp-- );
			// skip to $
			//for ( ; *pp && *pp != '$' ; pp++ );
			// panic?
			if ( ! *pp ) { char *xx=NULL;*xx=0; }
			endBal = atof2( pp );
			continue;
		}

		// end of that statements?
		tt = "END OF FILE";
		nn = strstr(p,tt);
		if ( nn ) {
			float diff = endBal - startBal;
			// round both
			long a1 = (long)(statementTotal + 0.50);
			long a2 = (long)(diff + 0.50);
			// if within $100 let it slide
			long adiff = a1 - a2;
			if ( adiff < 0 ) adiff = -1 * adiff;
			// only show this if our year...
			if ( adiff > 100 &&
			     a1 != a2 &&
			     ( year1 == accyear || year2 == accyear ) ) {
				// 2008 start/end balances are wrong!
				if ( accyear != 2008 ) 
					fprintf(stdout,"statement discrepancy "
						"%li != %li\n",a1,a2);
				//exit (-1);
			}
			// reset
			statementTotal = 0.0;
			endBal = 0.0;
			startBal = 0.0;
			inCreditSection = false;
			inDebitSection = false;
			// reset ptr
			//nextStatement = p + (strlen(p)+1);
		}

		// skip line if it has "Sweep-Principal Credit"
		if ( strstr(p,"Statement Beginning Balance") ) continue;
		if ( strstr(p,"Sweep-Principal Credit") ) continue;
		if ( strstr(p,"Sweep Debit") ) continue;
		// skip leading space
		while ( *p && ! isalnum(*p) ) p++;
		// exhausted the line? get next line
		if ( ! *p ) continue;
		// grab account number
		nn = strstr(p,"Account Number ");
		if ( nn ) {
			an = nn + 15;
			if ( ! isdigit(an[0]) ) { char *xx=NULL;*xx=0; }
			inDebitSection  = false;
			inCreditSection = false;
			inOtherSection  = false;
			continue;
		}
		nn = strstr(p,"Account Number: ");
		if ( nn ) {
			an = nn + 16;
			if ( ! isdigit(an[0]) ) { char *xx=NULL;*xx=0; }
			inDebitSection  = false;
			inCreditSection = false;
			inOtherSection  = false;
			continue;
		}
		// check for "Withdrawls and Debits" line to indicate we
		// are in the debits section
		if ( strstr ( p , "Withdrawals and Debits" ) ) {
			inDebitSection = true;
			inCreditSection = false;
			continue;
		}
		if ( strstr ( p , "Deposits and Credits" ) ) {
			inDebitSection = false;
			inCreditSection = true;
			continue;
		}
		if ( strstr ( p , "Payments and Other Credits" ) ) {
			inCreditSection = true;
			inDebitSection  = false;
		}
		// for acct #0265 (matt's personal).
		if ( strstr ( p , "Additions and Subtractions") ) {
			inCreditSection = false;
			inDebitSection  = false;
			inOtherSection = true;
		}
		if ( strstr ( p , "Payments and Credits" ) ) {
			inCreditSection = true;
			inDebitSection  = false;
		}
		if ( strstr ( p , "Purchases and Adjustments" ) ) {
			inCreditSection = false;
			inDebitSection = true;
		}
		// this is usually indicative of summary info
		if ( strstr ( p, "....." ) ||
		     strstr ( p , "Finance Charges" ) ) {
			inOtherSection = false;
			inDebitSection = false;
			inCreditSection = false;
			continue;
		}
		// . parse out year of the statement
		// . look for "04/01/08 through 04/30/08"
		char *oo = strstr ( p , " through " ) ;
		if ( oo ) {
			// backup
			char *s = oo - 8;
			// skip till digit
			while ( *s && !isdigit(*s) ) s++;
			// must be a year format
			if ( isdigit(s[0]) &&
			     isdigit(s[1]) &&
			     s[2] == '/' &&
			     isdigit(s[3]) &&
			     isdigit(s[4]) &&
			     s[5] == '/' &&
			     isdigit(s[6]) &&
			     isdigit(s[7]) ) {
				year1 = 2000 + atoi(s+6);
				year2 = 2000 + atoi(oo+15);
				month1 = atoi(s+6-3);
				month2 = atoi(oo+15-3);
				if ( year1 < 2000 || year1 > 2050 ||
				     year2 < 2000 || year2 > 2050 ) { 
					char *xx=NULL;*xx=0;}
				continue;
			}
		}
		// 05-16-09 through 06-17-09 (for acct 0265, matt's personal)
		if ( oo ) {
			char *s = oo - 6;
			if ( s[0] == '-' &&
			     isdigit(s[1]) &&
			     isdigit(s[2]) &&
			     s[3] == '-' &&
			     isdigit(s[4]) &&
			     isdigit(s[5]) ) {
				year1 = 2000 + atoi(oo-2);
				year2 = 2000 + atoi(oo+15);
				month1 = atoi(oo-2-3);
				month2 = atoi(oo+15-3);
				if ( year1 < 2000 || year1 > 2050 ||
				     year2 < 2000 || year2 > 2050 ) { 
					char *xx=NULL;*xx=0;}
				// fix 12-18-08 through 01-16-09
				//if ( accyear != year1 &&
				//     accyear != year2){char *xx=NULL;*xx=0;}
				//year = accyear;
				continue;
			}
		}
		// sometimes Jun 24 - Jul 24, 2010"
		if ( strstr(p," - ") && 
		     eol - 10 > p &&
		     eol[-6] == ',' ) { // the comma like in "July 24, 2010"
			long eyear = atol(eol-4);
			// add 2000?
			if ( eyear > 2007 &&
			     eyear < 2020 ) {
				year1 = eyear;
				year2 = eyear;
				// fix this
				char *xx=NULL;*xx=0;
				continue;
			}
			else { char *xx=NULL;*xx=0; }
		}
		// sometimes November 2009 Statement"
		if ( strstr(p,"Statement") && 
		     eol - 20 > p &&
		     eol[-10] == ' ' &&
		     eol[-14] == '2' ) {
			long eyear = atol(eol-14);
			// add 2000?
			if ( eyear > 2007 &&
			     eyear < 2020 ) {
				year1 = eyear;
				year2 = eyear;
				char *xx=NULL;*xx=0;
				continue;
			}
			else { char *xx=NULL;*xx=0; }
		}


		// the new format as of 2012 is very clean, just transactions
		if ( ! newFormat &&
		     ! inCreditSection &&
		     ! inDebitSection  &&
		     ! inOtherSection )
			continue;

		// skip if not our year
		// not now, we want to get total deposits and withdraws for verifying...
		//if ( year1 != accyear && year2 != accyear ) continue;
		//if ( year1 < accyear && year2 < accyear ) continue;

		// each line has the year in the 2012 format
		if ( ! newFormat &&
		     year1 < MINYEAR && year2 < MINYEAR ) continue;

		// we parse two checks on one line
		bool firstCheckOnLine = true;
	subloop:
		// if line begins with a 4 digit number followed by a space
		// or an asterisk, then it is a checknumber
		if ( isdigit(p[0]) &&
		     isdigit(p[1]) &&
		     isdigit(p[2]) &&
		     isdigit(p[3]) &&
		     (p[4]=='*' || p[4]==' ') ) {
			// point to the full transaction
			t[nt].m_ptr = p;
			// it is a check
			//t[nt].m_type = 'C';
			// grab check #
			char *cnptr = p;
			// . usually this is two checks per line
			// . skip check number
			p += 4;
			// skip asterisk
			if ( *p == '*' ) p++;
			// skip until we hit another digit
			while ( *p && isspace(*p) ) p++;
			// exhausted?
			if ( ! *p ) continue;
			// if not a digit, i guess not a check line
			// might be an address line
			if ( !isdigit(*p) ) 
				continue;


			// crap! 6550 statements have the amt next but 
			// matt wells llc 0265 statements have the date next!
			if ( isPriceFormat ( p ) ) {
				// get the amount #
				t[nt].m_amount = -1 * atof2(p);
				// skip until we hit a space
				while ( *p && !isspace(*p) ) p++;
				// exhausted?
				if ( ! *p ) continue;
				// skip spaces
				while ( *p && isspace(*p) ) p++;
				// exhausted?
				if ( ! *p ) continue;
				// get the date posted
				t[nt].m_date = p;
			}
			else {
				// get the date posted
				t[nt].m_date = p;
				// skip until we hit a space
				while ( *p && !isspace(*p) ) p++;
				// exhausted?
				if ( ! *p ) continue;
				// skip spaces
				while ( *p && isspace(*p) ) p++;
				// exhausted?
				if ( ! *p ) continue;
				// get the amount #
				t[nt].m_amount = -1 * atof2(p);
			}
				
			// sanity
			//if ( strstr(p,"6938")) { char *xx=NULL;*xx=0; }
			// get month
			long month = atoi(t[nt].m_date);
			if ( month <= 0 || month > 12 ) { char *xx=NULL;*xx=0; }
			// skip date (04/08)
			//p += 5;

			// skip until we hit a space
			while ( *p && !isspace(*p) ) p++;
			// exhausted?
			if ( ! *p ) continue;
			// NULL terminate
			*p++ = '\0';
			// skip spaces
			while ( *p && isspace(*p) ) p++;
			// must be digit! the bank ref number
			if ( ! isdigit(*p) ) continue;
			// should be bank ref # now
			long long ref = atoll(p);
			// sanity 
			if ( ref <= 0 ) { char *xx=NULL;*xx=0; }
			// save that
			t[nt].m_refNum = ref;
			// skip digits of bank ref number
			while ( *p && isdigit(*p) ) p++;
			// skip spaces after bank ref num
			while ( *p && isspace(*p) ) p++;
			// and we got this
			t[nt].m_srcAcct = an;
			// we do not know the recipient from this data
			t[nt].m_dstAcct = NULL;
			// pick correct year
			long year ;
			if ( month == 12 ) year = year1;
			if ( month == 01 ) year = year2;
			else  {
				if ( year1 != year2 ) { char *xx=NULL;*xx=0; }
				year = year1;
			}

			// keep tabs on this
			float pf = t[nt].m_amount;

			// even if not our year, keep tabs on total deposited and withdrawn for ALL transactions
			statementTotal += pf;
			// print for debug 
			//fprintf(stdout,"%.02f --> %.02f\n",pf,statementTotal);

			// skip if not our year (mdw)
			//if ( year != accyear ) continue;
			// save year
			t[nt].m_year = year;
			// skip if not 08! HACK!
			//if ( year != accyear ) { char *xx=NULL;*xx=0; }//continue;
			// it is a check
			//t[nt].m_type = 'C';
			// store check #
			cnptr[4] = 0;
			t[nt].m_checkNum = atoi(cnptr);
			// last 4 of acct
			char *last4 = (char *)an+strlen(an)-4;
			// assume none
			t[nt].m_desc = NULL;
			// look up description of check
			long n = sizeof(checks)/sizeof(char *);
			for ( long i = 0 ; i < n ; i++ ) {
				char *s = checks[i];
				// match check #
				char *scn = s + 5;
				if ( strncmp(cnptr,scn,4) != 0 ) continue;
				// match acct #
				if ( strncmp(s,last4,4) != 0 ) continue;
				// if duplicate core
				if ( t[nt].m_desc ) { char *xx=NULL;*xx=0; }
				// got a match!
				t[nt].m_desc = (char *)s +4+1+4+1;
			}
			// count it
			nt++;
			// exhausted?
			if ( ! *p ) continue;
			// all done with this line? ok, get the next then
			if ( ! firstCheckOnLine ) continue;
			// the 2nd time
			firstCheckOnLine = false;
			// now we got the other check # on this line
			goto subloop;
		}

		// scan line for date/amt/ref#/desc
		char *start = p;
		char *date  = NULL;
		char *desc  = NULL;
		char *price = NULL;

		for ( ; *p ; p++ ) {

			// skip spaces
			if ( *p == ' ' ) continue;

			// if it begins with <digit><digit>/<digit><digit><space>
			// then it is a transaction, probably check card
			if ( ! date &&
			     isdigit(p[0]) &&
			     isdigit(p[1]) &&
			     // matt's acct uses 06-03, so allow hyphen
			     (p[2]=='/' || p[2]=='-') &&
			     isdigit(p[3]) &&
			     isdigit(p[4]) &&
			     p[5]==' ' ) {
				date = p;
				// skip date
				p += 5;
				p--;
				continue;
			}

			// 05/10/2012 (new file format)
			if ( ! date &&
			     isdigit(p[0]) &&
			     isdigit(p[1]) &&
			     // matt's acct uses 06-03, so allow hyphen
			     (p[2]=='/' || p[2]=='-') &&
			     isdigit(p[3]) &&
			     isdigit(p[4]) &&
			     (p[5]=='/' || p[5]=='-') &&
			     isdigit(p[6]) &&
			     isdigit(p[7]) &&
			     isdigit(p[8]) &&
			     isdigit(p[9]) &&
			     p[10]==' ' ) {
				date = p;
				// a hack for the new format
				year1 = atoi(p+6);
				year2 = year1;
				// skip date
				p += 10;
				p--;
				continue;
			}
			

			// skip if not price format. includes beginning + or -?
			if ( ! price && isPriceFormat(p) ) {
				price = p;
				continue;
			}

			// description?
			if ( ! desc && isalpha(*p) ) {
				desc = p;
				continue;
			}
		}

		// the new format is like this:
		//03/20/2012     Wire Transfer Fee   -12.00    17,181.12

		


		// skip if line did not have all 3
		if ( ! date  ) continue;
		if ( ! price ) continue;
		if ( ! desc  ) continue;

		// get month
		long month = atoi(date);
		if ( month <= 0 || month > 12 ) { char *xx=NULL;*xx=0; }
		// pick correct year
		long year ;
		if      ( month == 12 ) year = year1;
		else if ( month == 01 ) year = year2;
		else  {
			if ( year1 != year2 ) { char *xx=NULL;*xx=0; }
			year = year1;
		}
		// to float (this deals with commas and signs like + or -)
		float pf = atof2(price);

		// must not be negative i fin special section
		if ( inDebitSection || inCreditSection ) {
			if ( pf < 0.0 ) { char *xx=NULL;*xx=0; }
		}

		// convert it
		if ( inDebitSection ) pf = -1 * pf;

		// even if not our year, keep tabs on total deposited and withdrawn for ALL transactions
		statementTotal += pf;
		// print for debug 
		//fprintf(stdout,"%.02f --> %.02f\n",pf,statementTotal);

		// skip if not our year (mdw)
		//if ( year != accyear ) continue;

		// point to the full transaction
		t[nt].m_ptr = start;
		// also the date here
		t[nt].m_date = date;

		// it is a debit? 2 means debit
		//if ( inDebitSection ) t[nt].m_type = 'E'; // expense
		// 1 means credit
		//else if ( inCreditSection )  t[nt].m_type = 'D'; // deposit
		// otherwise, we gotta check the sign...
		//else {
		//	if ( pf < 0 ) t[nt].m_type = 'E';
		//	else          t[nt].m_type = 'D';
		//}
		
		t[nt].m_amount = pf;
		// that is the description
		t[nt].m_desc = desc; // p;
		// and we got this
		t[nt].m_srcAcct = an;
		// sanity
		if ( ! isdigit(an[0] ) ) { char *xx=NULL;*xx=0; }
		// save year
		t[nt].m_year = year;
		// skip if not 08! HACK!
		//if ( year != accyear ) { char *xx=NULL;*xx=0; } // 2008 ) continue;
		//t[nt].m_checkNum = -1;
		// assume none
		//t[nt].m_refNum = 0;

		// this does not have one for some reasion
		if ( strstr(desc,"Automatic Transfer To") ) {
			nt++;
			continue;
		}

		// ref num is always last on the line so start backward
		char *rp = eol - 1;
		// if not digit, must be earned interst or something
		// backup until space
		while ( isdigit(rp[-1]) ) rp--;
		// get it
		long long refNum = 0;
		if ( isdigit(*rp) ) refNum = atoll(rp);
		// back up over that
		if ( isdigit(*rp) ) {
			// backup over first digit
			rp--;
			// back up over spaces before that
			while ( isspace(*rp) ) rp--;
			// null terminate
			rp[1] = '\0';
		}
		// sanity check
		//if ( refNum < 0 ) { char *xx=NULL;*xx=0;};
		// save it
		t[nt].m_refNum = refNum;

		nt++;
	}

	return bufPtr;
}
