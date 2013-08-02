#ifndef _GBTYPES_H_
#define _GBTYPES_H_

// . max # of tags any one site or url can have
// . even AFTER the "inheritance loop"
// . includes the 4 bytes used for size and # of tags
//#define MAX_TAGREC_SIZE 1024
#define MAX_TAGREC_SIZE 4000

// . up to 32768 collections possible, MUST be signed
// . a collnum_t of -1 is used by RdbCache to mean "no collection"
#define collnum_t short

//typedef char bool

// damn, /usr/include/sys/types.h defines this as just an int!
#undef u_int128_t

#define MAX_KEY_BYTES 28

// shit, how was i supposed to know this is defined in sys/types.h...
#define key_t   u_int96_t

// or you can be less ambiguous with these types
#define key96_t   u_int96_t
#define uint96_t  u_int96_t
#define key128_t  u_int128_t
#define uint128_t u_int128_t

class u_int96_t {

 public:
	// it's little endian
	unsigned long long n0; // the low  long long
	unsigned long      n1; // the high long 

	u_int96_t (                ) { };
	u_int96_t ( unsigned long i ) {	n0 = i; n1 = 0; };

	bool isNegativeKey ( ) { 
		return ( (   ((long)n0) & ((long)0x01)  ) == 0x00 ); };

	void setMin ( ) { n0 = 0LL; n1 = 0; }

	void setToMin ( ) { n0 = 0LL; n1 = 0; }

	void setMax ( ) { n0 = 0xffffffffffffffffLL; n1 = 0xffffffff; };

	void setToMax ( ) { n0 = 0xffffffffffffffffLL; n1 = 0xffffffff;	};

	long getHighLong ( ) { return n1; };

	bool operator == ( u_int96_t i ) { 
		return ( i.n0 == n0 && i.n1 == n1);};
	bool operator != ( u_int96_t i ) { 
		return ( i.n0 != n0 || i.n1 != n1);};
	void operator =  ( u_int96_t i ) {
		n0 = i.n0; n1 = i.n1; };

	u_int96_t  operator |  ( u_int96_t i ) {
		n0 |= i.n0; n1 |= i.n1; return *this; }
	u_int96_t  operator ^  ( u_int96_t i ) {
		n0 ^= i.n0; n1 ^= i.n1; return *this; }
	u_int96_t operator &  ( u_int96_t i ) {
		n0 &= i.n0; n1 &= i.n1; return *this; }
	u_int96_t  operator ~  ( ) {
		n0 = ~n0; n1 = ~n1;  return *this; };

	bool operator != ( unsigned long i ) { 
		return ( i    != n0 ); };
	void operator =  ( unsigned long i ) {
		n0 = i; n1 = 0; };
	long operator &  ( unsigned long i ) {
		return n0 & i; };

	//void operator += ( unsigned long long i ) { // watch out for carry
	//if ( n0 + i < n0 ) n1++;
	//n0 += i; };

	/*
	void operator += ( u_int96_t i ) {
		if ( n0 + i.n0  < n0 ) n1++;
		n0 += i.n0;
		n1 += i.n1;
	};
	*/

	void operator |= ( u_int96_t i ) {
		n0 |= i.n0;
		n1 |= i.n1;
	};

	// NOTE: i must be bigger than j!?
	/*
	key_t operator + ( u_int96_t i ) {
		unsigned long long oldn0 = n0;
		n0 = n0 + i.n0;
		if ( n0 < oldn0 ) n1++; // carry
		n1 += i.n1;
		return *this;
	};
	*/

	// . NOTE: i must be bigger than j!
	// . this is used by RdbCache only and doesn't need to be exact
	//key_t operator - ( u_int96_t i ) {
	u_int96_t minus ( u_int96_t i ) {
		n0 = n0 - i.n0 ;
		if ( n0 < i.n0 ) { n1--; n0++; } // carry
		n1 = n1 - i.n1;
		return *this;
	};

	// NOTE: i must be bigger than j!?
	/*
	key_t operator + ( unsigned long i ) {
		if ( n0 + i < n0 ) n1++;
		n0 += i; 
		return *this;
	};

	key_t operator - ( unsigned long i ) {
		if ( n0 - i > n0 ) n1--;
		n0 -= i; 
		return *this;
	};
	*/

	// NOTE: i must be bigger than j!?
	void operator -= ( unsigned long i ) {
		if ( n0 - i > n0 ) n1--;
		n0 -= i;
	};

	void operator += ( unsigned long i ) { // watch out for carry
		if ( n0 + i < n0 ) n1++;
		n0 += i; };

	// TODO: make this more efficient
	u_int96_t operator >> ( long i ) {
		for ( long j = 0 ; j < i ; j++ ) {
			long long carry = n1 & 0x01;
			n1 >>= 1;
			n0 >>= 1;
			if ( carry ) n0 |= 0x8000000000000000LL;
		}
		return *this;
	};
	// TODO: make this more efficient
	u_int96_t operator << ( long i ) {
		for ( long j = 0 ; j < i ; j++ ) {
			long long carry = n0 & 0x8000000000000000LL;
			n0 <<= 1;
			n1 <<= 1;
			if ( carry ) n1 |= 0x01;
		}
		return *this;
	};

	bool operator >  ( u_int96_t i ) {
		if ( n1 > i.n1 ) return true;
		if ( n1 < i.n1 ) return false;
		if ( n0 > i.n0 ) return true;
		return false;
	};
	bool operator <  ( u_int96_t i ) {
		if ( n1 < i.n1 ) return true;
		if ( n1 > i.n1 ) return false;
		if ( n0 < i.n0 ) return true;
		return false;
	};
	bool operator <= ( u_int96_t i ) {
		if ( n1 < i.n1 ) return true;
		if ( n1 > i.n1 ) return false;
		if ( n0 < i.n0 ) return true;
		if ( n0 > i.n0 ) return false;
		return true;
	};
	bool operator >= ( u_int96_t i ) {
		if ( n1 > i.n1 ) return true;
		if ( n1 < i.n1 ) return false;
		if ( n0 > i.n0 ) return true;
		if ( n0 < i.n0 ) return false;
		return true;
	};
	// TODO: should we fix this?
	long operator %  ( unsigned long mod ) { 
		return n0 % mod; };

};

class u_int128_t {

 public:
	// it's little endian
	unsigned long long n0; // the low  long long
	unsigned long long n1; // the high long 

	u_int128_t (                ) { };
	u_int128_t ( unsigned long i ) {	n0 = i; n1 = 0; };

	bool isNegativeKey ( ) { 
		return ( (   ((long)n0) & ((long)0x01)  ) == 0x00 ); };

	void setMin ( ) { n0 = 0LL; n1 = 0LL; }

	void setToMin ( ) { n0 = 0LL; n1 = 0LL; }

	void setMax ( ) { n0=0xffffffffffffffffLL; n1=0xffffffffffffffffLL;};

	void setToMax ( ) { n0=0xffffffffffffffffLL; n1=0xffffffffffffffffLL;};

	long getHighLong ( ) { return n1; };

	bool operator == ( u_int128_t i ) { 
		return ( i.n0 == n0 && i.n1 == n1);};
	bool operator != ( u_int128_t i ) { 
		return ( i.n0 != n0 || i.n1 != n1);};
	void operator =  ( u_int128_t i ) {
		n0 = i.n0; n1 = i.n1; };

	u_int128_t  operator |  ( u_int128_t i ) {
		n0 |= i.n0; n1 |= i.n1; return *this; }
	u_int128_t  operator ^  ( u_int128_t i ) {
		n0 ^= i.n0; n1 ^= i.n1; return *this; }
	u_int128_t operator &  ( u_int128_t i ) {
		n0 &= i.n0; n1 &= i.n1; return *this; }
	u_int128_t  operator ~  ( ) {
		n0 = ~n0; n1 = ~n1;  return *this; };

	bool operator != ( unsigned long i ) { 
		return ( i    != n0 ); };
	void operator =  ( unsigned long i ) {
		n0 = i; n1 = 0; };
	long operator &  ( unsigned long i ) {
		return n0 & i; };

	void operator |= ( u_int128_t i ) {
		n0 |= i.n0;
		n1 |= i.n1;
	};

	// . NOTE: i must be bigger than j!
	// . this is used by RdbCache only and doesn't need to be exact
	//key_t operator - ( u_int128_t i ) {
	u_int128_t minus ( u_int128_t i ) {
		n0 = n0 - i.n0 ;
		if ( n0 < i.n0 ) { n1--; n0++; } // carry
		n1 = n1 - i.n1;
		return *this;
	};

	// NOTE: i must be bigger than j!?
	void operator -= ( unsigned long i ) {
		if ( n0 - i > n0 ) n1--;
		n0 -= i;
	};

	void operator += ( unsigned long i ) { // watch out for carry
		if ( n0 + i < n0 ) n1++;
		n0 += i; };

	// TODO: make this more efficient
	u_int128_t operator >> ( long i ) {
		for ( long j = 0 ; j < i ; j++ ) {
			long long carry = n1 & 0x01;
			n1 >>= 1;
			n0 >>= 1;
			if ( carry ) n0 |= 0x8000000000000000LL;
		}
		return *this;
	};
	// TODO: make this more efficient
	u_int128_t operator << ( long i ) {
		for ( long j = 0 ; j < i ; j++ ) {
			long long carry = n0 & 0x8000000000000000LL;
			n0 <<= 1;
			n1 <<= 1;
			if ( carry ) n1 |= 0x01;
		}
		return *this;
	};

	bool operator >  ( u_int128_t i ) {
		if ( n1 > i.n1 ) return true;
		if ( n1 < i.n1 ) return false;
		if ( n0 > i.n0 ) return true;
		return false;
	};
	bool operator <  ( u_int128_t i ) {
		if ( n1 < i.n1 ) return true;
		if ( n1 > i.n1 ) return false;
		if ( n0 < i.n0 ) return true;
		return false;
	};
	bool operator <= ( u_int128_t i ) {
		if ( n1 < i.n1 ) return true;
		if ( n1 > i.n1 ) return false;
		if ( n0 < i.n0 ) return true;
		if ( n0 > i.n0 ) return false;
		return true;
	};
	bool operator >= ( u_int128_t i ) {
		if ( n1 > i.n1 ) return true;
		if ( n1 < i.n1 ) return false;
		if ( n0 > i.n0 ) return true;
		if ( n0 < i.n0 ) return false;
		return true;
	};
	// TODO: should we fix this?
	long operator %  ( unsigned long mod ) { 
		return n0 % mod; };

};

// used only by m_orderTree in Spider.cpp for RdbTree.cpp
class key192_t {
 public:
	// k0 is the LEAST significant long
	//uint32_t k0;
	//uint32_t k1;
	//uint32_t k2;
	//uint32_t k3;
	//uint32_t k4;
	//uint32_t k5;
	// it's little endian
	unsigned long long n0; // the low  long long
	unsigned long long n1; // the medium long long
	unsigned long long n2; // the high long long

	bool operator == ( key192_t i ) { 
		return ( i.n0 == n0 && 
			 i.n1 == n1 && 
			 i.n2 == n2 
			 );};

	void operator += ( unsigned long i ) { // watch out for carry
		if ( n0 + i < n0 ) {
			if ( n1 + i < n1 )
				n2++;
			n1 += i;
		}
		n0 += i; 
	};

	bool operator <  ( key192_t i ) {
		if ( n2 < i.n2 ) return true;
		if ( n2 > i.n2 ) return false;
		if ( n1 < i.n1 ) return true;
		if ( n1 > i.n1 ) return false;
		if ( n0 < i.n0 ) return true;
		return false;
	};
	void setMin ( ) { n0 = 0LL; n1 = 0LL; n2 = 0LL; }


	void setMax ( ) { 
		n0=0xffffffffffffffffLL; 
		n1=0xffffffffffffffffLL;
		n2=0xffffffffffffffffLL;
	};


};

class key224_t {
 public:
	// k0 is the LEAST significant long
	//uint32_t k0;
	//uint32_t k1;
	//uint32_t k2;
	//uint32_t k3;
	//uint32_t k4;
	//uint32_t k5;
	// it's little endian
	unsigned long      n0;
	unsigned long long n1; // the low  long long
	unsigned long long n2; // the medium long long
	unsigned long long n3; // the high long long

	bool operator == ( key224_t i ) { 
		return ( i.n0 == n0 && 
			 i.n1 == n1 && 
			 i.n2 == n2 &&
			 i.n3 == n3
			 );};

	void operator += ( unsigned long i ) { // watch out for carry
		if ( n0 + i > n0 ) { n0 += i; return; }
		if ( n1 + 1 > n1 ) { n1 += 1; n0 += i; return; }
		if ( n2 + 1 > n2 ) { n2 += 1; n1 += 1; n0 += i; return; }
		n3 += 1; n2 += 1; n1 += 1; n0 += i; return;
	}

	// NOTE: i must be bigger than j!?
	void operator -= ( unsigned long i ) {
		if ( n0 - i < n0 ) {n0 -= i;return;}
		if ( n1 - i < n1 ) { n1--; n0 -=i; return; }
		if ( n2 - i < n2 ) { n2--; n1--; n0 -=i; return; }
		n3--; n2--; n1--; n0 -= i;
	}

	bool operator <  ( key224_t i ) {
		if ( n3 < i.n3 ) return true;
		if ( n3 > i.n3 ) return false;
		if ( n2 < i.n2 ) return true;
		if ( n2 > i.n2 ) return false;
		if ( n1 < i.n1 ) return true;
		if ( n1 > i.n1 ) return false;
		if ( n0 < i.n0 ) return true;
		return false;
	};
	void setMin ( ) { n0 = 0; n1 = 0LL; n2 = 0LL; n3 = 0LL; }


	void setMax ( ) { 
		n0=0xffffffff;
		n1=0xffffffffffffffffLL; 
		n2=0xffffffffffffffffLL;
		n3=0xffffffffffffffffLL;
	};


};

#pragma pack(2)

class key144_t {
 public:
	// it's little endian
	unsigned short      n0; // the low short
	unsigned long long  n1; // the medium long long
	unsigned long long  n2; // the high long long

	bool operator == ( key144_t i ) { 
		return ( i.n0 == n0 && 
			 i.n1 == n1 && 
			 i.n2 == n2 
			 );};

	void operator += ( unsigned long i ) { // watch out for carry
		if ( (unsigned short)(n0+i) > n0 ) { n0 += i; return; }
		if ( n1 + 1 > n1 ) { n1 += 1; n0 += i; return; }
		n2 += 1; n1 += 1; n0 += i; return;
	};

	// NOTE: i must be bigger than j!?
	void operator -= ( unsigned long i ) {
		if ( (unsigned short)(n0 - i) < n0 ) {n0 -= i;return;}
		if ( n1 - i < n1 ) { n1--; n0 -=i; return; }
		n2--; n1--; n0 -= i;
	}

	bool operator <  ( key144_t i ) {
		if ( n2 < i.n2 ) return true;
		if ( n2 > i.n2 ) return false;
		if ( n1 < i.n1 ) return true;
		if ( n1 > i.n1 ) return false;
		if ( n0 < i.n0 ) return true;
		return false;
	};
	void setMin ( ) { n0 = 0; n1 = 0LL; n2 = 0LL; }


	void setMax ( ) { 
		n0=0xffff;
		n1=0xffffffffffffffffLL;
		n2=0xffffffffffffffffLL;
	};


};

#pragma pack(4)

// handy quicky functions
inline char KEYCMP ( char *k1, long a, char *k2, long b , char keySize ) {
	// posdb
	if ( keySize == 18 ) {
		if ( (*(unsigned long long *)(k1+a*keySize+2+8)) <
		   (*(unsigned long long *)(k2+b*keySize+2+8)) ) return -1;
		if ( (*(unsigned long long *)(k1+a*keySize+2+8)) >
		   (*(unsigned long long *)(k2+b*keySize+2+8)) ) return  1;
		if ( (*(unsigned long long *)(k1+a*keySize+2)) < 
		   (*(unsigned long long *)(k2+b*keySize+2)) ) return -1;
		if ( (*(unsigned long long *)(k1+a*keySize+2)) > 
		   (*(unsigned long long *)(k2+b*keySize+2)) ) return  1;
		if ( (*(unsigned short *)(k1+a*keySize+0)) <
		   (*(unsigned short *)(k2+b*keySize+0)) ) return -1;
		if ( (*(unsigned short *)(k1+a*keySize+0)) >
		   (*(unsigned short *)(k2+b*keySize+0)) ) return  1;
		return 0;
	}
	if ( keySize == 12 ) { 
		if ( (*(unsigned long long *)(k1+a*keySize+4)) < 
		     (*(unsigned long long *)(k2+b*keySize+4)) ) return -1;
		if ( (*(unsigned long long *)(k1+a*keySize+4)) > 
		     (*(unsigned long long *)(k2+b*keySize+4)) ) return  1;
		if ( (*(unsigned long      *)(k1+a*keySize+0)) < 
		     (*(unsigned long      *)(k2+b*keySize+0)) ) return -1;
		if ( (*(unsigned long      *)(k1+a*keySize+0)) > 
		     (*(unsigned long      *)(k2+b*keySize+0)) ) return  1;
		return 0;
	}
	// must be size of 16 then
	if ( keySize == 16 ) {
		if ( (*(unsigned long long *)(k1+a*keySize+8)) < 
		   (*(unsigned long long *)(k2+b*keySize+8)) ) return -1;
		if ( (*(unsigned long long *)(k1+a*keySize+8)) > 
		   (*(unsigned long long *)(k2+b*keySize+8)) ) return  1;
		if ( (*(unsigned long long *)(k1+a*keySize+0)) <
		   (*(unsigned long long *)(k2+b*keySize+0)) ) return -1;
		if ( (*(unsigned long long *)(k1+a*keySize+0)) >
		   (*(unsigned long long *)(k2+b*keySize+0)) ) return  1;
		return 0;
	}
	// allow half key comparison too
	if ( keySize == 6 ) {
		if ( (*(unsigned long  *)(k1+a*keySize+2)) <
		     (*(unsigned long  *)(k2+b*keySize+2)) ) return -1;
		if ( (*(unsigned long  *)(k1+a*keySize+2)) >
		     (*(unsigned long  *)(k2+b*keySize+2)) ) return  1;
		if ( (*(unsigned short *)(k1+a*keySize+0)) <
		     (*(unsigned short *)(k2+b*keySize+0)) ) return -1;
		if ( (*(unsigned short *)(k1+a*keySize+0)) >
		     (*(unsigned short *)(k2+b*keySize+0)) ) return  1;
		return 0;
	}
	// 128+64= 196bit keys for m_orderKey in Spider.cpp in RdbTree.cpp
	if ( keySize == 24 ) {
		if ( (*(unsigned long long *)(k1+a*keySize+16)) <
		   (*(unsigned long long *)(k2+b*keySize+16)) ) return -1;
		if ( (*(unsigned long long *)(k1+a*keySize+16)) >
		   (*(unsigned long long *)(k2+b*keySize+16)) ) return  1;
		if ( (*(unsigned long long *)(k1+a*keySize+8)) < 
		   (*(unsigned long long *)(k2+b*keySize+8)) ) return -1;
		if ( (*(unsigned long long *)(k1+a*keySize+8)) > 
		   (*(unsigned long long *)(k2+b*keySize+8)) ) return  1;
		if ( (*(unsigned long long *)(k1+a*keySize+0)) <
		   (*(unsigned long long *)(k2+b*keySize+0)) ) return -1;
		if ( (*(unsigned long long *)(k1+a*keySize+0)) >
		   (*(unsigned long long *)(k2+b*keySize+0)) ) return  1;
		return 0;
	}
	if ( keySize == 28 ) {
		if ( (*(unsigned long long *)(k1+a*keySize+20)) <
		   (*(unsigned long long *)(k2+b*keySize+20)) ) return -1;
		if ( (*(unsigned long long *)(k1+a*keySize+20)) >
		   (*(unsigned long long *)(k2+b*keySize+20)) ) return  1;
		if ( (*(unsigned long long *)(k1+a*keySize+12)) < 
		   (*(unsigned long long *)(k2+b*keySize+12)) ) return -1;
		if ( (*(unsigned long long *)(k1+a*keySize+12)) > 
		   (*(unsigned long long *)(k2+b*keySize+12)) ) return  1;
		if ( (*(unsigned long long *)(k1+a*keySize+4)) <
		   (*(unsigned long long *)(k2+b*keySize+4)) ) return -1;
		if ( (*(unsigned long long *)(k1+a*keySize+4)) >
		   (*(unsigned long long *)(k2+b*keySize+4)) ) return  1;
		if ( (*(unsigned long *)(k1+a*keySize)) <
		   (*(unsigned long *)(k2+b*keySize)) ) return -1;
		if ( (*(unsigned long *)(k1+a*keySize)) >
		   (*(unsigned long *)(k2+b*keySize)) ) return  1;
		return 0;
	}
	if ( keySize == 8 ) {
		if ( (*(unsigned long long *)(k1+a*keySize+0)) <
		     (*(unsigned long long *)(k2+b*keySize+0)) ) return -1;
		if ( (*(unsigned long long *)(k1+a*keySize+0)) >
		     (*(unsigned long long *)(k2+b*keySize+0)) ) return  1;
		return 0;
	}
	char *xx=NULL;*xx=0;
	return 0;
}

inline char KEYCMP ( char *k1, char *k2, char keySize ) {
	// posdb
	if ( keySize == 18 ) {
		if ( (*(unsigned long long *)(k1+10)) <
		     (*(unsigned long long *)(k2+10)) ) return -1;
		if ( (*(unsigned long long *)(k1+10)) >
		     (*(unsigned long long *)(k2+10)) ) return  1;
		if ( (*(unsigned long long *)(k1+2)) <
		     (*(unsigned long long *)(k2+2)) ) return -1;
		if ( (*(unsigned long long *)(k1+2)) >
		     (*(unsigned long long *)(k2+2)) ) return  1;
		if ( (*(unsigned short *)(k1)) <
		     (*(unsigned short *)(k2)) ) return -1;
		if ( (*(unsigned short *)(k1)) >
		     (*(unsigned short *)(k2)) ) return  1;
		return 0;
	}
	if ( keySize == 12 ) { 
		if ( (*(unsigned long long *)(k1+4)) < 
		     (*(unsigned long long *)(k2+4)) ) return -1;
		if ( (*(unsigned long long *)(k1+4)) > 
		     (*(unsigned long long *)(k2+4)) ) return  1;
		if ( (*(unsigned long      *)(k1)) < 
		     (*(unsigned long      *)(k2)) ) return -1;
		if ( (*(unsigned long      *)(k1)) > 
		     (*(unsigned long      *)(k2)) ) return  1;
		return 0;
	}
	// must be size of 16 then
	if ( keySize == 16 ) {
		if ( (*(unsigned long long *)(k1+8)) <
		     (*(unsigned long long *)(k2+8)) ) return -1;
		if ( (*(unsigned long long *)(k1+8)) >
		     (*(unsigned long long *)(k2+8)) ) return  1;
		if ( (*(unsigned long long *)(k1)) <
		     (*(unsigned long long *)(k2)) ) return -1;
		if ( (*(unsigned long long *)(k1)) >
		     (*(unsigned long long *)(k2)) ) return  1;
		return 0;
	}
	// allow half key comparison too
	if ( keySize == 6 ) {
		if ( (*(unsigned long  *)(k1+2)) <
		     (*(unsigned long  *)(k2+2)) ) return -1;
		if ( (*(unsigned long  *)(k1+2)) >
		     (*(unsigned long  *)(k2+2)) ) return  1;
		if ( (*(unsigned short *)(k1+0)) <
		     (*(unsigned short *)(k2+0)) ) return -1;
		if ( (*(unsigned short *)(k1+0)) >
		     (*(unsigned short *)(k2+0)) ) return  1;
		return 0;
	}
	// must be size of 16 then
	if ( keySize == 24 ) {
		if ( (*(unsigned long long *)(k1+16)) <
		     (*(unsigned long long *)(k2+16)) ) return -1;
		if ( (*(unsigned long long *)(k1+16)) >
		     (*(unsigned long long *)(k2+16)) ) return  1;
		if ( (*(unsigned long long *)(k1+8)) <
		     (*(unsigned long long *)(k2+8)) ) return -1;
		if ( (*(unsigned long long *)(k1+8)) >
		     (*(unsigned long long *)(k2+8)) ) return  1;
		if ( (*(unsigned long long *)(k1)) <
		     (*(unsigned long long *)(k2)) ) return -1;
		if ( (*(unsigned long long *)(k1)) >
		     (*(unsigned long long *)(k2)) ) return  1;
		return 0;
	}
	if ( keySize == 28 ) {
		if ( (*(unsigned long long *)(k1+20)) <
		     (*(unsigned long long *)(k2+20)) ) return -1;
		if ( (*(unsigned long long *)(k1+20)) >
		     (*(unsigned long long *)(k2+20)) ) return  1;
		if ( (*(unsigned long long *)(k1+12)) <
		     (*(unsigned long long *)(k2+12)) ) return -1;
		if ( (*(unsigned long long *)(k1+12)) >
		     (*(unsigned long long *)(k2+12)) ) return  1;
		if ( (*(unsigned long long *)(k1+4)) <
		     (*(unsigned long long *)(k2+4)) ) return -1;
		if ( (*(unsigned long long *)(k1+4)) >
		     (*(unsigned long long *)(k2+4)) ) return  1;
		if ( (*(unsigned long *)(k1)) <
		     (*(unsigned long *)(k2)) ) return -1;
		if ( (*(unsigned long *)(k1)) >
		     (*(unsigned long *)(k2)) ) return  1;
		return 0;
	}
	if ( keySize == 8 ) {
		if ( (*(unsigned long  *)(k1+0)) <
		     (*(unsigned long  *)(k2+0)) ) return -1;
		if ( (*(unsigned long  *)(k1+0)) >
		     (*(unsigned long  *)(k2+0)) ) return  1;
		return 0;
	}
	char *xx=NULL;*xx=0;
	return 0;
}


inline char KEYCMPNEGEQ ( char *k1, char *k2, char keySize ) {
	// posdb
	if ( keySize == 18 ) { 
		if ( (*(unsigned long long *)(k1+10)) < 
		     (*(unsigned long long *)(k2+10)) ) return -1;
		if ( (*(unsigned long long *)(k1+10)) > 
		     (*(unsigned long long *)(k2+10)) ) return  1;
		if ( (*(unsigned long long *)(k1+2)) < 
		     (*(unsigned long long *)(k2+2)) ) return -1;
		if ( (*(unsigned long long *)(k1+2)) > 
		     (*(unsigned long long *)(k2+2)) ) return  1;
		unsigned short k1n0 = ((*(unsigned short*)(k1)) & ~0x01UL);
		unsigned short k2n0 = ((*(unsigned short*)(k2)) & ~0x01UL);
		if ( k1n0 < k2n0 ) return -1;
		if ( k1n0 > k2n0 ) return  1;
		return 0;
	}
	if ( keySize == 24 ) { 
		if ( (*(unsigned long long *)(k1+16)) < 
		     (*(unsigned long long *)(k2+16)) ) return -1;
		if ( (*(unsigned long long *)(k1+16)) > 
		     (*(unsigned long long *)(k2+16)) ) return  1;
		if ( (*(unsigned long long *)(k1+8)) < 
		     (*(unsigned long long *)(k2+8)) ) return -1;
		if ( (*(unsigned long long *)(k1+8)) > 
		     (*(unsigned long long *)(k2+8)) ) return  1;
		unsigned long long k1n0 = 
			((*(unsigned long long*)(k1)) & ~0x01ULL);
		unsigned long long k2n0 = 
			((*(unsigned long long*)(k2)) & ~0x01ULL);
		if ( k1n0 < k2n0 ) return -1;
		if ( k1n0 > k2n0 ) return  1;
		return 0;
	}
	// linkdb
	if ( keySize == 28 ) { 
		if ( (*(unsigned long long *)(k1+20)) < 
		     (*(unsigned long long *)(k2+20)) ) return -1;
		if ( (*(unsigned long long *)(k1+20)) > 
		     (*(unsigned long long *)(k2+20)) ) return  1;
		if ( (*(unsigned long long *)(k1+12)) < 
		     (*(unsigned long long *)(k2+12)) ) return -1;
		if ( (*(unsigned long long *)(k1+12)) > 
		     (*(unsigned long long *)(k2+12)) ) return  1;
		if ( (*(unsigned long long *)(k1+4)) < 
		     (*(unsigned long long *)(k2+4)) ) return -1;
		if ( (*(unsigned long long *)(k1+4)) > 
		     (*(unsigned long long *)(k2+4)) ) return  1;
		unsigned long long k1n0 = 
			((*(unsigned long *)(k1)) & ~0x01ULL);
		unsigned long long k2n0 = 
			((*(unsigned long *)(k2)) & ~0x01ULL);
		if ( k1n0 < k2n0 ) return -1;
		if ( k1n0 > k2n0 ) return  1;
		return 0;
	}
	if ( keySize == 12 ) { 
		if ( (*(unsigned long long *)(k1+4)) < 
		     (*(unsigned long long *)(k2+4)) ) return -1;
		if ( (*(unsigned long long *)(k1+4)) > 
		     (*(unsigned long long *)(k2+4)) ) return  1;
		unsigned long k1n0 = ((*(unsigned long*)(k1)) & ~0x01UL);
		unsigned long k2n0 = ((*(unsigned long*)(k2)) & ~0x01UL);
		if ( k1n0 < k2n0 ) return -1;
		if ( k1n0 > k2n0 ) return  1;
		return 0;
	}
	// must be size of 16 then
	if ( keySize == 16 ) {
		if ( (*(unsigned long long *)(k1+8)) <
		     (*(unsigned long long *)(k2+8)) ) return -1;
		if ( (*(unsigned long long *)(k1+8)) >
		     (*(unsigned long long *)(k2+8)) ) return  1;
		unsigned long long k1n0 = ((*(unsigned long long *)(k1)) & ~0x01ULL);
		unsigned long long k2n0 = ((*(unsigned long long *)(k2)) & ~0x01ULL);
		if ( k1n0 < k2n0 ) return -1;
		if ( k1n0 > k2n0 ) return  1;
		return 0;
	}
	// allow half key comparison too
	if ( keySize == 6 ) {
		if ( (*(unsigned long  *)(k1+2)) <
		     (*(unsigned long  *)(k2+2)) ) return -1;
		if ( (*(unsigned long  *)(k1+2)) >
		     (*(unsigned long  *)(k2+2)) ) return  1;
		if ( (*(unsigned short *)(k1+0)) <
		     (*(unsigned short *)(k2+0)) ) return -1;
		if ( (*(unsigned short *)(k1+0)) >
		     (*(unsigned short *)(k2+0)) ) return  1;
		return 0;
	}
	char *xx=NULL; *xx = 0;
	return 0;
}

inline char *KEYSTR ( void *vk , long ks ) {
	char *k = (char *)vk;
	static char tmp[128];
	char *s = tmp;
	*s++ = '0';
	*s++ = 'x';
	for ( unsigned char *p = (unsigned char *)k + ks - 1 ; 
	      p >= (unsigned char *)k ; p-- ) {
		unsigned char v = *p >> 4;
		if ( v <= 9 ) *s++ = v + '0';
		else          *s++ = v - 10 + 'a';
		v = *p & 0x0f;
		if ( v <= 9 ) *s++ = v + '0';
		else          *s++ = v - 10 + 'a';
	}
	*s = '\0';
	return tmp;
}

inline unsigned short KEY0 ( char *k , long ks ) {
	if ( ks == 18 ) return *(unsigned short *)k;
	else { char *xx=NULL;*xx=0; }
	return 0;
}

inline long long KEY1 ( char *k , char keySize ) {
	if ( keySize == 12 ) return *(long *)(k+8);
	if ( keySize == 18 ) return *(long long *)(k+2);
	// otherwise, assume 16
	return *(long long *)(k+8);
}

inline long long KEY2 ( char *k , char keySize ) {
	if ( keySize == 18 ) return *(long long *)(k+10);
	char *xx=NULL;*xx=0;
	return 0;
}



inline long long KEY0 ( char *k ) {
	return *(long long *)k;
}
inline void KEYSET ( char *k1 , char *k2 , char keySize ) {
	// posdb
	if ( keySize == 18 ) {
		*(short *)(k1  ) = *(short *)(k2  );
		*(long long *)(k1+2) = *(long long *)(k2+2);
		*(long long *)(k1+10) = *(long long *)(k2+10);
		return;
	}
	if ( keySize == 12 ) {
		*(long long *) k1    = *(long long *) k2;
		*(long      *)(k1+8) = *(long      *)(k2+8);
		return;
	}
	// otherwise, assume 16
	if ( keySize == 16 ) {
		*(long long *)(k1  ) = *(long long *)(k2  );
		*(long long *)(k1+8) = *(long long *)(k2+8);
		return;
	}
	if ( keySize == 24 ) {
		*(long long *)(k1  ) = *(long long *)(k2  );
		*(long long *)(k1+8) = *(long long *)(k2+8);
		*(long long *)(k1+16) = *(long long *)(k2+16);
		return;
	}
	if ( keySize == 28 ) {
		*(long long *)(k1  ) = *(long long *)(k2  );
		*(long long *)(k1+8) = *(long long *)(k2+8);
		*(long long *)(k1+16) = *(long long *)(k2+16);
		*(long *)(k1+24) = *(long *)(k2+24);
		return;
	}
	if ( keySize == 8 ) {
		*(long long *)(k1  ) = *(long long *)(k2  );
		return;
	}
	//if ( keySize == 4 ) {
	//	*(long *)(k1  ) = *(long *)(k2  );
	//	return;
	//}
	char *xx=NULL;*xx=0;
	return;
}

inline char KEYNEG ( char *k , long a , char keySize ) {
	// posdb
	if ( keySize == 18 ) {
		if ( (k[a*18] & 0x01) == 0x00 ) return 1;
		return 0;
	}
	if ( keySize == 12 ) {
		if ( (k[a*12] & 0x01) == 0x00 ) return 1;
		return 0;
	}
	// otherwise, assume 16 bytes
	if (keySize == 16 ) {
		if ( (k[a*16] & 0x01) == 0x00 ) return 1;
		return 0;
	}
	if ( keySize == 24 ) {
		if ( (k[a*24] & 0x01) == 0x00 ) return 1;
		return 0;
	}
	if ( keySize == 28 ) {
		if ( (k[a*28] & 0x01) == 0x00 ) return 1;
		return 0;
	}
	if ( keySize == 8 ) {
		if ( (k[a*8] & 0x01) == 0x00 ) return 1;
		return 0;
	}
	char *xx=NULL;*xx=0;
	return 0;
}

inline char KEYNEG ( char *k ) {
	if ( (k[0] & 0x01) == 0x00 ) return 1;
	return 0;
}

inline char KEYNEG ( key_t k ) {
	if ( (k.n0 & 0x01) == 0x00 ) return 1;
	return 0;
}

inline char KEYPOS ( char *k ) {
	if ( (k[0] & 0x01) == 0x01 ) return 1;
	return 0;
}

inline void KEYADD ( char *k , long add , char keySize ) {
	// posdb
	if ( keySize == 18 ) { *((key144_t *)k) += (long)1; return; }
	if ( keySize == 12 ) { *((key96_t  *)k) += (long)1; return; }
	if ( keySize == 16 ) { *((key128_t *)k) += (long)1; return; }
	if ( keySize == 8  ) { *((uint64_t *)k) += (long)1; return; }
	if ( keySize == 24 ) { *((key192_t *)k) += (long)1; return; }
	if ( keySize == 28 ) { *((key224_t *)k) += (long)1; return; }
	char *xx=NULL;*xx=0;
}

inline void KEYSUB ( char *k , long add , char keySize ) {
	if ( keySize == 18 ) { *((key144_t *)k) -= (long)1; return; }
	if ( keySize == 12 ) { *((key96_t  *)k) -= (long)1; return; }
	if ( keySize == 16 ) { *((key128_t *)k) -= (long)1; return; }
	if ( keySize == 28 ) { *((key224_t *)k) -= (long)1; return; }
	char *xx=NULL;*xx=0;
}

inline void KEYOR ( char *k , long opor ) {
	*((unsigned long *)k) |= opor;
	//if ( keySize == 12 ) ((key12_t *)k)->n0 |= or;
	//else                 ((key16_t *)k)->n0 |= or;
}

inline void KEYXOR ( char *k , long opxor ) {
	*((unsigned long *)k) ^= opxor;
}

inline void KEYMIN ( char *k, char keySize ) {
	memset ( k , 0 , keySize );
}

inline void KEYMAX ( char *k, char keySize ) {
	for ( long i = 0 ; i < keySize ; i++ ) k[i]=0xff;
}

inline char *KEYMIN() { return  "\0\0\0\0"
			 "\0\0\0\0"
			 "\0\0\0\0"
			 "\0\0\0\0"
			 "\0\0\0\0"
			 "\0\0\0\0"
			 "\0\0\0\0"
			 "\0\0\0\0"; };
static char s_foo[] = { 0xff , 0xff , 0xff , 0xff , 
			0xff , 0xff , 0xff , 0xff , 
			0xff , 0xff , 0xff , 0xff , 
			0xff , 0xff , 0xff , 0xff , 
			0xff , 0xff , 0xff , 0xff , 
			0xff , 0xff , 0xff , 0xff , 
			0xff , 0xff , 0xff , 0xff , 
			0xff , 0xff , 0xff , 0xff };
inline char *KEYMAX() { return s_foo; };


#endif
