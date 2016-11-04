#include "gb-include.h"

#include "Loop.h"
#include "Entities.h"
#include "UCWordIterator.h"
#include "SafeBuf.h"
#include "Xml.h"
#include "XmlNode.h"
#include "iana_charset.h"

static bool g_clockInSync = false;

bool g_clockNeedsUpdate = true;

bool isClockInSync() { 
	if ( g_hostdb.m_initialized && g_hostdb.m_hostId == 0 ) return true;
	return g_clockInSync; 
}


bool print96 ( char *k ) {
        key_t *kp = (key_t *)k;
        printf("n1=0x%"XINT32" n0=0x%"XINT64"\n",(int32_t)kp->n1,(int64_t)kp->n0);
	return true;
}

bool print96 ( key_t *kp ) {
        printf("n1=0x%"XINT32" n0=0x%"XINT64"\n",(int32_t)kp->n1,(int64_t)kp->n0);
	return true;
}

bool print128 ( char *k ) {
        key128_t *kp = (key128_t *)k;
        printf("n1=0x%"XINT64" n0=0x%"XINT64"\n",(int64_t)kp->n1,(int64_t)kp->n0);
	return true;
}

bool print128 ( key128_t *kp ) {
        printf("n1=0x%"XINT64" n0=0x%"XINT64"\n",(int64_t)kp->n1,(int64_t)kp->n0);
	return true;
}

// . put all the maps here now
// . convert "c" to lower case
	const unsigned char g_map_to_lower[] = {
		0  , 1  , 2  ,  3 ,  4 ,  5 ,  6 ,  7 ,           
		8  , 9  , 10 , 11 , 12 , 13 , 14 , 15 ,       
		16 , 17 , 18 , 19 , 20 , 21 , 22 , 23 ,       
		24 , 25 , 26 , 27 , 28 , 29 , 30 , 31 ,       
		32 , 33 , 34 , 35 , 36 , 37 , 38 , 39 ,       
		40 , 41 , 42 , 43 , 44 , 45 , 46 , 47 ,       
		48 , 49 , 50 , 51 , 52 , 53 , 54 , 55 ,       
		56 , 57 , 58 , 59 , 60 , 61 , 62 , 63 ,       
		64 , 'a','b' ,'c' ,'d' ,'e' ,'f' ,'g' ,   
		'h', 'i','j' ,'k' ,'l' ,'m' ,'n' ,'o' ,   
		'p', 'q','r' ,'s' ,'t' ,'u' ,'v' ,'w' ,   
		'x', 'y','z' , 91 , 92 ,93  ,94  ,95  ,       
		96 , 'a','b' ,'c' ,'d' ,'e' ,'f' ,'g' ,   
		'h', 'i','j' ,'k' ,'l' ,'m' ,'n' ,'o' ,   
		'p', 'q','r' ,'s' ,'t' ,'u' ,'v' ,'w' ,   
		'x', 'y','z' ,123 ,124 ,125 ,126 ,127 ,   
		128,129,130,131,132,133,134,135,   
		136,137,138,139,140,141,142,143,   
		144,145,146,147,148,149,150,151,   
		152,153,154,155,156,157,158,159,   
		160,161,162,163,164,165,166,167,
		168,169,170,171,172,173,174,175, 
		176,177,178,179,180,181,182,183,      
		184,185,186,187,188,189,190,191, 
		224,225,226,227,228,229,230,231,
		232,233,234,235,236,237,238,239,
		240,241,242,243,244,245,246,247,
		248,249,250,251,252,253,254,223,
		224,225,226,227,228,229,230,231,
		232,233,234,235,236,237,238,239,
		240,241,242,243,244,245,246,247,
		248,249,250,251,252,253,254,255
	};


// converts ascii chars and IS_O chars to their lower case versions
	const unsigned char g_map_to_upper[] = {
		0  , 1  , 2  ,  3 ,  4 ,  5 ,  6 ,  7 ,           
		8  , 9  , 10 , 11 , 12 , 13 , 14 , 15 ,       
		16 , 17 , 18 , 19 , 20 , 21 , 22 , 23 ,       
		24 , 25 , 26 , 27 , 28 , 29 , 30 , 31 ,       
		32 , 33 , 34 , 35 , 36 , 37 , 38 , 39 ,       
		40 , 41 , 42 , 43 , 44 , 45 , 46 , 47 ,       
		48 , 49 , 50 , 51 , 52 , 53 , 54 , 55 ,       
		56 , 57 , 58 , 59 , 60 , 61 , 62 , 63 ,       
		64 , 'A','B' ,'C' ,'D' ,'E' ,'F' ,'G' ,   
		'H', 'I','J' ,'K' ,'L' ,'M' ,'N' ,'O' ,   
		'P', 'Q','R' ,'S' ,'T' ,'U' ,'V' ,'W' ,   
		'X', 'Y','Z' , 91 , 92 ,93  ,94  ,95  ,       
		96 , 'A','B' ,'C' ,'D' ,'E' ,'F' ,'G' ,   
		'H', 'I','J' ,'K' ,'L' ,'M' ,'N' ,'O' ,   
		'P', 'Q','R' ,'S' ,'T' ,'U' ,'V' ,'W' ,   
		'X', 'Y','Z' ,123 ,124 ,125 ,126 ,127 ,   
		128,129,130,131,132,133,134,135,   
		136,137,138,139,140,141,142,143,   
		144,145,146,147,148,149,150,151,   
		152,153,154,155,156,157,158,159,   
		160,161,162,163,164,165,166,167,
		168,169,170,171,172,173,174,175, 
		176,177,178,179,180,181,182,183,      
		184,185,186,187,188,189,190,191, 
		192,193,194,195,196,197,198,199,
		200,201,202,203,204,205,206,207,
		208,209,210,211,212,213,214,215,
		216,217,218,219,220,221,222,223,
		192,193,194,195,196,197,198,199,
		200,201,202,203,204,205,206,207,
		208,209,210,211,212,213,214,215,
		216,217,218,219,220,221,222,255
	};

	const unsigned char g_map_to_ascii[] = {
		0  , 1  , 2  ,  3 ,  4 ,  5 ,  6 ,  7 ,           
		8  , 9  , 10 , 11 , 12 , 13 , 14 , 15 ,       
		16 , 17 , 18 , 19 , 20 , 21 , 22 , 23 ,       
		24 , 25 , 26 , 27 , 28 , 29 , 30 , 31 ,       
		32 , 33 , 34 , 35 , 36 , 37 , 38 , 39 ,       
		40 , 41 , 42 , 43 , 44 , 45 , 46 , 47 ,       
		48 , 49 , 50 , 51 , 52 , 53 , 54 , 55 ,       
		56 , 57 , 58 , 59 , 60 , 61 , 62 , 63 ,       
		64 , 'A','B' ,'C' ,'D' ,'E' ,'F' ,'G' ,   
		'H', 'I','J' ,'K' ,'L' ,'M' ,'N' ,'O' ,   
		'P', 'Q','R' ,'S' ,'T' ,'U' ,'V' ,'W' ,   
		'X', 'Y','Z' , 91 , 92 ,93  ,94  ,95  ,       
		96 , 'a','b' ,'c' ,'d' ,'e' ,'f' ,'g' ,   
		'h', 'i','j' ,'k' ,'l' ,'m' ,'n' ,'o' ,   
		'p', 'q','r' ,'s' ,'t' ,'u' ,'v' ,'w' ,   
		'x', 'y','z' ,123 ,124 ,125 ,126 ,127 ,   
		128,129,130,131,  132,133,134,135,   
		136,137,138,139,  140,141,142,143,   
		144,145,146,147,  148,149,150,151,   
		152,153,154,155,  156,157,158,159,   
		160,161,162,'#',  'o','Y','|','S',      
		168,169,'a',171,  172,173,174,175, 
		176,177,'2','3',  180,'u',182,183,      
		' ','1','o',187,  188,189,190,'?', 
		'A','A','A','A',  'A','A','A'/*198-AE*/,'C',
		'E','E','E','E',  'I','I','I','I', 
		'D','N','O','O',  'O','O','O','x',      
		'O','U','U','U',  'U','Y',222/*TH*/,'s'/*changed from B*/, 
		'a','a','a','a',  'a','a','a'/*230-ae*/,'c',
		'e','e','e','e',  'i','i','i','i', 
		'd','n','o','o',  'o','o','o','/',      
		'o','u','u','u',  'u','y',254/*th*/,'y' 
	};


	const char g_map_is_upper[] = {
		0,0,0,0,0,0,0,0,  // 0 -7        
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,   
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,1,1,1,1,1,1,1, // 64
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,0,0,0,0,0, // 88
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 15*8
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, // 20*8
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		1,1,1,1,1,1,1,1, // 192
		1,1,1,1,1,1,1,1, // 200
		1,1,1,1,1,1,1,0, // 208
		1,1,1,1,1,1,1,1, // 216
		0,0,0,0,0,0,0,0, // 224
		0,0,0,0,0,0,0,0, // 232
		0,0,0,0,0,0,0,0, // 240
		0,0,0,0,0,0,0,0}; // 248


// can this character be in an html (or xml) tag name??
	const char g_map_canBeInTagName[] = {
		0,0,0,0,0,0,0,0,  // 0 -7        
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,1,0,0, // 40 -- hyphen can be in tag name
		0,0,0,0,0,0,0,0, // 48     
		0,0,0,0,0,0,0,0, // 56          
		0,1,1,1,1,1,1,1, // 64
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,0,0,0,0,0, // 88
		0,1,1,1,1,1,1,1, // 96
		1,1,1,1,1,1,1,1, // 104
		1,1,1,1,1,1,1,1, // 112
		1,1,1,0,0,0,0,0, // 15*8 = 120
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, // 20*8 = 160
		0,0,0,0,0,0,0,0, // 168
		0,0,0,0,0,0,0,0, // 176
		0,0,0,0,0,0,0,0, // 184
		0,0,0,0,0,0,0,0, // 192
		0,0,0,0,0,0,0,0, // 200
		0,0,0,0,0,0,0,0, // 208
		0,0,0,0,0,0,0,0, // 216
		0,0,0,0,0,0,0,0, // 224
		0,0,0,0,0,0,0,0, // 232
		0,0,0,0,0,0,0,0, // 240
		0,0,0,0,0,0,0,0}; // 248


	const char g_map_is_control [] = {
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,   
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0, // 64
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0, // 96
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, 		
		0,0,0,0,0,0,0,1, // 120, 127 = DEL
		1,1,1,1,1,1,1,1, // 128
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,0,0,0,0,0,0,0, // 160 = backspace
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, // 192
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, // 224
		0,0,0,0,0,0,0,0, // 232
		0,0,0,0,0,0,0,0, // 240
		0,0,0,0,0,0,0,0};  // 248

// people mix windows 1252 into latin-1 so we have to be less restrictive here...
	const char g_map_is_binary[] = { 
		1,1,1,1,1,1,1,1,
		1,0,0,1,1,0,1,1, // \t=9 \n = 10 \r = 13
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,   
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0, // 64
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0, // 96
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, 		
		0,0,0,0,0,0,0,1, // 120, 127 = DEL
		0,1,1,1,1,1,1,1, // 128 (128 is a quote)
		1,1,1,1,1,1,1,1, // 136
		1,0,0,0,0,0,0,1, // 144 (145 146 147 are quotes, 148 is dash, 149 bullet,150 dash)
		0,0,1,1,0,0,1,1, // 152 (152 & 153 are quotes, 156 & 157 are double quotes)
		0,0,0,0,0,0,0,0, // 160 = backspace (some urls have this???)
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, // 192
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, // 224
		0,0,0,0,0,0,0,0, // 232
		0,0,0,0,0,0,0,0, // 240
		0,0,0,0,0,0,0,0};  // 248

// ' ' '\n' '\t' '\r'
	const char g_map_is_wspace[] = {
		0,0,0,0,0,0,0,0,  // 0 -7        
		0,1,1,0,0,1,0,0,  // \t=9  \n = 10  \r = 13
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		1,0,0,0,0,0,0,0,  // space=32         
		0,0,0,0,0,0,0,0,   
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0, // 64
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 88
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 15*8
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, // 160 -- turn off 160, it might be utf8 byte
		0,0,0,0,0,0,0,0, // 168
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, // 192
		0,0,0,0,0,0,0,0, // 200 
		0,0,0,0,0,0,0,0, // 208
		0,0,0,0,0,0,0,0, // 216
		0,0,0,0,0,0,0,0, // 224
		0,0,0,0,0,0,0,0, // 232
		0,0,0,0,0,0,0,0, // 240
		0,0,0,0,0,0,0,0}; // 248


// '\n'
	const char g_map_is_vspace[] = {
		0,0,0,0,0,0,0,0,  // 0 -7        
		0,0,1,0,0,0,0,0,  // \t=9 \n = 10
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,  // space=32         
		0,0,0,0,0,0,0,0,   
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0, // 64
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 88
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 15*8
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, // 20*8
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		1,1,1,1,1,1,1,1, // 192
		1,1,1,1,1,1,1,1, // 200
		1,1,1,1,1,1,1,0, // 208
		1,1,1,1,1,1,1,1, // 216
		0,0,0,0,0,0,0,0, // 224
		0,0,0,0,0,0,0,0, // 232
		0,0,0,0,0,0,0,0, // 240
		0,0,0,0,0,0,0,0}; // 248

// ' ' '\t'
	const char g_map_is_hspace[] = {
		0,0,0,0,0,0,0,0,  // 0 -7        
		0,1,0,0,0,0,0,0,  // \t=9 \n = 10
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		1,0,0,0,0,0,0,0,  // space=32         
		0,0,0,0,0,0,0,0,   
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0, // 64
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 88
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 15*8
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, // 20*8
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		1,1,1,1,1,1,1,1, // 192
		1,1,1,1,1,1,1,1, // 200
		1,1,1,1,1,1,1,0, // 208
		1,1,1,1,1,1,1,1, // 216
		0,0,0,0,0,0,0,0, // 224
		0,0,0,0,0,0,0,0, // 232
		0,0,0,0,0,0,0,0, // 240
		0,0,0,0,0,0,0,0}; // 248


const char g_map_is_vowel[] = {
		0,0,0,0,0,0,0,0,  // 0 -7        
		0,0,0,0,0,0,0,0,  // 8-15
		0,0,0,0,0,0,0,0,  // 16-  
		0,0,0,0,0,0,0,0,  // 24-   
		0,0,0,0,0,0,0,0,  // 32-   
		0,0,0,0,0,0,0,0,  // 40-
		0,0,0,0,0,0,0,0,  // 48-   
		0,0,0,0,0,0,0,0,  // 56-   
		0,1,0,0,0,1,0,0,  // 64  (A=65)
		0,1,0,0,0,0,0,1,  // 72
		0,0,0,0,0,1,0,0,  // 80    
		0,0,0,0,0,0,0,0,  // 88-    
		0,1,0,0,0,1,0,0,  // 96- (a=97)
		0,1,0,0,0,0,0,1,           
		0,0,0,0,0,1,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0, // 128
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, // 160
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, // 192
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0};



// converts ascii chars and IS_O chars to their lower case versions
	const char g_map_is_lower[] = { // 97-122 and 224-255 (excluding 247)
		0,0,0,0,0,0,0,0,  // 0 -7        
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,   
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0, // 64
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,0,0,0,0,0,0,0,           
		0,1,1,1,1,1,1,1, // 96
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,0,0,0,0,0, // 120
		0,0,0,0,0,0,0,0, // 128
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, // 160
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, // 192
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		0,0,0,0,0,0,0,0, 
		1,1,1,1,1,1,1,1, // 224
		1,1,1,1,1,1,1,1, // 232
		1,1,1,1,1,1,1,0, // 240
		1,1,1,1,1,1,1,1};  // 248

	const char g_map_is_ascii[] = { // 32 to 126
		0,0,0,0,0,0,0,0, // 0
		0,0,0,0,0,0,0,0, // 8
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		1,1,1,1,1,1,1,1, // 32
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,0,
		0,0,0,0,0,0,0,0, // 128
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0};

// just from 0-127, used by the inlined *_utf8() functions in fctypes.h
	const char g_map_is_ascii3[] = { // 32 to 126
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1, // 32
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		0,0,0,0,0,0,0,0, // 128
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0};



	const char g_map_is_iso[] = { // 32 to 126
		0,0,0,0,0,0,0,0, // 0
		0,0,0,0,0,0,0,0, // 8
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 32
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 128
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 152
		0,1,1,1,1,1,1,1, // 160
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1};

	const char g_map_is_punct[] = { // 33-47, 58-64, 91-96, 123-126, 161-191, 215,247
		0,0,0,0,0,0,0,0, // 0
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 16
		0,0,0,0,0,0,0,0,
		0,1,1,1,1,1,1,1, // 32
		1,1,1,1,1,1,1,1,
		0,0,0,0,0,0,0,0, // 48
		0,0,1,1,1,1,1,1,
		1,0,0,0,0,0,0,0, // 64
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 80
		0,0,0,1,1,1,1,1,
		1,0,0,0,0,0,0,0, // 96
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 112
		0,0,0,1,1,1,1,0,
		0,0,0,0,0,0,0,0, // 128
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 144
		0,0,0,0,0,0,0,0,
		0,1,1,1,1,1,1,1, // 160
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1, // 176
		1,1,1,1,1,1,1,1,
		0,0,0,0,0,0,0,0, // 192
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,1, // 208
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 224
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,1, // 240
		0,0,0,0,0,0,0,0};   // 248

	const char g_map_is_alnum[] = { // 48-57, 65-90,97-122,192-255(excluding 215,247)
		0,0,0,0,0,0,0,0, // 0
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 16
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 32   
		0,0,0,0,0,0,0,0,
		1,1,1,1,1,1,1,1, // 48
		1,1,0,0,0,0,0,0,
		0,1,1,1,1,1,1,1, // 64
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1, // 80
		1,1,1,0,0,0,0,0,
		0,1,1,1,1,1,1,1, // 96
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1, // 112
		1,1,1,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 128
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 144
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 160
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 176
		0,0,0,0,0,0,0,0,

		1,1,1,1,1,1,1,1, // 192
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,0, // 208
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1, // 224
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,0, // 240
		1,1,1,1,1,1,1,1};    

	const char g_map_is_alpha[] = { // 65-90, 97-122, 192-255 (excluding 215, 247)
		0,0,0,0,0,0,0,0, // 0
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 16
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 32   
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 48
		0,0,0,0,0,0,0,0,
		0,1,1,1,1,1,1,1, // 64
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1, // 80
		1,1,1,0,0,0,0,0,
		0,1,1,1,1,1,1,1, // 96
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1, // 112
		1,1,1,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 128
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 144
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 160
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 176
		0,0,0,0,0,0,0,0,

		1,1,1,1,1,1,1,1, // 192
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,0, // 208
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1, // 224
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,0, // 240
		1,1,1,1,1,1,1,1};    

	const char g_map_is_digit[] = { // 48-57
		0,0,0,0,0,0,0,0, // 0
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 16
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 32
		0,0,0,0,0,0,0,0,
		1,1,1,1,1,1,1,1, // 48
		1,1,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0};


	const char g_map_is_hex[] = { // 48-57
		0,0,0,0,0,0,0,0, // 0
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 16
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 32
		0,0,0,0,0,0,0,0,
		1,1,1,1,1,1,1,1, // 48
		1,1,0,0,0,0,0,0, // 56
		0,1,1,1,1,1,1,0, // 64  (65='A')
		0,0,0,0,0,0,0,0, // 72
		0,0,0,0,0,0,0,0, // 80
		0,0,0,0,0,0,0,0, // 88
		0,1,1,1,1,1,1,0, // 96  (97='a')
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0};

	// stolen from is_alnum, but turned on - and _
	const char g_map_is_tagname_char [] = { // 48-57, 65-90,97-122,192-255(excluding 215,247)
		0,0,0,0,0,0,0,0, // 0
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 16
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 32   
		0,0,0,0,0,1,0,0, // -
		1,1,1,1,1,1,1,1, // 48
		1,1,1,0,0,0,0,0, // we include the : for feedburner:origlink
		0,1,1,1,1,1,1,1, // 64
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1, // 80
		1,1,1,0,0,0,0,1, // _
		0,1,1,1,1,1,1,1, // 96
		1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1, // 112
		1,1,1,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 128
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 144
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 160
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 176
		0,0,0,0,0,0,0,0,

		// we are no longer necessarily latin-1!!
		0,0,0,0,0,0,0,0, // 192
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 208
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 224
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 240
		0,0,0,0,0,0,0,0};

	const char g_map_is_tag_control_char[] = { // 48-57
		0,0,0,0,0,0,0,0, // 0
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 16
		0,0,0,0,0,0,0,0,
		0,0,1,0,0,0,0,1, // 32 " and '
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, // 48
		0,0,0,0,1,0,1,0, // 56  < and >
		0,0,0,0,0,0,0,0, // 64
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0};

// when matching query terms to words/phrases in doc skip over spaces
// or other punct so that "flypaper" in the query matches "fly paper" in the
// doc
/*
	const char g_map_is_match_skip[] = { // 48-57
		0,0,0,0,0,0,0,0, // 0
		0,1,1,0,0,0,0,0, // \t and \n
		0,0,0,0,0,0,0,0, // 16
		0,0,0,0,0,0,0,0,
		1,0,0,0,0,0,0,1, // 32 space and '
		0,0,0,0,0,1,0,0, // 40 -
		0,0,0,0,0,0,0,0, // 48
		0,0,0,0,0,0,0,0, // 56 
		0,0,0,0,0,0,0,0, // 64
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0};
*/

// seems like this should be defined, but it isn't
int32_t strnlen ( const char *s , int32_t maxLen ) {
	int32_t i ;
	for ( i = 0 ; i < maxLen ; i++ ) if ( ! s[i] ) return i;
	return i;
}

char *strncasestr( char *haystack, int32_t haylen, char *needle){
	int32_t matchLen = 0;
	int32_t needleLen = gbstrlen(needle);
	for (int32_t i = 0; i < haylen;i++){
		char c1 = to_lower_a(haystack[i]);
		char c2 = to_lower_a(needle[matchLen]);
		if ( c1 != c2 ){
			// no match
			matchLen = 0;
			continue;
		}
		// we matched another character
		matchLen++;
		if (matchLen < needleLen) continue;
		
		// we've matched the whole string
		return haystack + i - matchLen + 1;
	}
	return NULL;
}

char *strnstr2( char *haystack, int32_t haylen, char *needle){
	int32_t matchLen = 0;
	int32_t needleLen = gbstrlen(needle);
	for (int32_t i = 0; i < haylen;i++){
		char c1 = (haystack[i]);
		char c2 = (needle[matchLen]);
		if ( c1 != c2 ){
			// no match
			matchLen = 0;
			continue;
		}
		// we matched another character
		matchLen++;
		if (matchLen < needleLen) continue;
		
		// we've matched the whole string
		return haystack + i - matchLen + 1;
	}
	return NULL;
}

// . get the # of words in this string
int32_t getNumWords ( char *s , int32_t len, int32_t titleVersion ) {

	int32_t wordCount = 0;
	bool inWord   = false;
	for ( int32_t i = 0 ; i < len ; i++ ) {
		if ( ! is_alnum_a ( s[i] ) && s[i]!='\'' ) {
			inWord = false;
			continue;
		}
		if ( ! inWord ) {
			inWord = true;
			wordCount++;
		}
	}
	return wordCount;
}

// . this stores a "n" into "s" and returns the # of bytes written into "s"
// . it also puts commas into the number
// . it now also NULL terminates bytes written into "s"
int32_t ulltoa ( char *s , uint64_t n ) {
	// if n is zero, it's easy
	if ( n == 0LL ) { *s++='0'; *s='\0'; return 1; }
	// a hunk is a number in [0,999]
	int32_t hunks[10]; 
	int32_t lastHunk = -1;
	// . get the hunks
	// . the first hunk we get is called the "lowest hunk"
	// . "lastHunk" is called the "highest hunk"
	for ( int32_t i = 0 ; i < 10 ; i++ ) {
		hunks[i] = n % 1000;
		n /= 1000;
		if ( hunks[i] != 0 ) lastHunk = i;
	}
	// remember start of buf for calculating # bytes written
	char *start = s;
	// print the hunks separated by comma
	for ( int32_t i = lastHunk ; i >= 0 ; i-- ) {
		// pad all hunks except highest hunk with zeroes
		if ( i != lastHunk ) sprintf ( s , "%03"INT32"" , hunks[i] );
		else                 sprintf ( s , "%"INT32"" , hunks[i] );
		s += gbstrlen(s);
		// comma after all hunks but lowest hunk
		if ( i != 0 ) *s++ = ',';
	}
	// null terminate it
	*s = '\0';
	// return # of bytes stored into "s"
	return s - start;
}

/*
int32_t atol2 ( const char *s, int32_t len ) {
	char tmp[32];
	if ( len > 30 ) len = 30;
	gbmemcpy ( tmp , s , len );
	tmp [ len ] = '\0';
	return atol ( s );
}
*/

int32_t atol2 ( const char *s, int32_t len ) {
	// skip over spaces
	const char *end = s + len;
	while ( s < end && is_wspace_a ( *s ) ) s++;
	// return 0 if all spaces
	if ( s == end ) return 0;
	int32_t i   = 0;
	int32_t val = 0;
	bool negative = false;
	if ( s[0] == '-' ) { negative = true; i++; }
	while ( i < len && is_digit(s[i]) ) val = val * 10 + ( s[i++] - '0' );
	if ( negative ) return -val;
	return val;
}

int64_t atoll1 ( const char *s ) {
	return atoll ( s );
}

int64_t atoll2 ( const char *s, int32_t len ) {
	// skip over spaces
	const char *end = s + len;
	while ( s < end && is_wspace_a ( *s ) ) s++;
	// return 0 if all spaces
	if ( s == end ) return 0;
	int32_t i   = 0;
	int64_t val = 0LL;
	bool negative = false;
	if ( s[0] == '-' ) { negative = true; i++; }
	while ( i < len && is_digit(s[i]) ) val = val * 10LL + ( s[i++] - '0');
	if ( negative ) return -val;
	return val;
}

double atof2 ( const char *s, int32_t len ) {
	// skip over spaces
	const char *end = s + len;
	while ( s < end && is_wspace_a ( *s ) ) { s++; len--; }
	// return 0 if all spaces
	if ( s == end ) return 0;
	char tmpBuf[128];
	if ( len >= 128 ) len = 127;
	//strncpy ( dst , s , len );

	const char *p = s;
	const char *srcEnd = s + len;
	char *dst = tmpBuf;
	// remove commas
	for ( ; p < srcEnd ; p++ ) {
		// skip commas
		if ( *p == ',' ) continue;
		// otherwise store it
		*dst++ = *p;
	}
	// null term
	*dst = '\0';
	//buf[len] = '\0';
	return atof ( tmpBuf );
}

double atod2 ( char *s, int32_t len ) {
	// point to end
	char *end = s + len;
	// null term temp
	char c = *end;
	*end = '\0';
	// get it
	double ret = strtod ( s , NULL );
	// undo it
	*end = c;
	return ret;
}


bool atob ( const char *s, int32_t len ) {
	// skip over spaces
	const char *end = s + len;
	while ( s < end && is_wspace_a ( *s ) ) s++;
	// return false if all spaces
	if ( s == end ) return false;
	// parse the ascii bool value
	if ( s[0] == 't' || s[0] == 'T' ) return true;
	if ( s[0] == 'y' || s[0] == 'Y' ) return true;
	if ( ! is_digit ( *s ) || *s == '0' ) return false;
	return true;
}

// hexadecimal ascii to key_t
int64_t htoint32_tint32_t ( const char *s, int32_t len ) {
	// skip over spaces
	const char *end = s + len;
	while ( s < end && is_wspace_a ( *s ) ) s++;
	// return 0 if all spaces
	if ( s == end ) return 0;
	int32_t i   = 0;
	int64_t val = 0;
	while ( i < len && is_hex(s[i]) )
		val = val * 16 + htob ( s[i++] );
	return val;
}

// convert hex ascii string into binary at "dst"
void hexToBin ( char *src , int32_t srcLen , char *dst ) {
	char *srcEnd = src + srcLen;
	for ( ; src && src < srcEnd ; ) {
		*dst  = htob(*src++);
		*dst <<= 4;
		*dst |= htob(*src++);
		dst++;
	}
	// sanity check
	if ( src != srcEnd ) { char *xx=NULL;*xx=0; }
}

void binToHex ( unsigned char *src , int32_t srcLen , char *dst ) {
	unsigned char *srcEnd = src + srcLen;
	for ( ; src && src < srcEnd ; ) {
		*dst++ = btoh(*src>>4);
		*dst++ = btoh(*src&15);
		src++;
	}
	// always null term!
	*dst = '\0';
	// sanity check
	if ( src != srcEnd ) { char *xx=NULL;*xx=0; }
}


// . like strstr but haystack may not be NULL terminated
// . needle, however, IS null terminated
char *strncasestr ( char *haystack , char *needle , int32_t haystackSize ) {
	int32_t needleSize = gbstrlen(needle);
	int32_t n = haystackSize - needleSize ;
	for ( int32_t i = 0 ; i <= n ; i++ ) {
		// keep looping if first chars do not match
		if ( to_lower_a(haystack[i]) != to_lower_a(needle[0]) ) 
			continue;
		// if needle was only 1 char it's a match
		if ( ! needle[1] ) return &haystack[i];
		// compare the whole strings now
		if ( strncasecmp ( &haystack[i] , needle , needleSize ) == 0 ) 
			return &haystack[i];			
	}
	return NULL;
}

// . like strstr but haystack may not be NULL terminated
// . needle, however, IS null terminated
char *strncasestr ( char *haystack , char *needle , 
		    int32_t haystackSize, int32_t needleSize ) {
	int32_t n = haystackSize - needleSize ;
	for ( int32_t i = 0 ; i <= n ; i++ ) {
		// keep looping if first chars do not match
		if ( to_lower_a(haystack[i]) != to_lower_a(needle[0]) ) 
			continue;
		// if needle was only 1 char it's a match
		if ( ! needle[1] ) return &haystack[i];
		// compare the whole strings now
		if ( strncasecmp ( &haystack[i] , needle , needleSize ) == 0 ) 
			return &haystack[i];			
	}
	return NULL;
}

char *strnstr ( char *haystack , char *needle , int32_t haystackSize ) {
	int32_t needleSize = gbstrlen(needle);
	int32_t n = haystackSize - needleSize ;
	for ( int32_t i = 0 ; i <= n ; i++ ) {
		// keep looping if first chars do not match
		if ( haystack[i] != needle[0] ) continue;
		// if needle was only 1 char it's a match
		if ( ! needle[1] ) return &haystack[i];
		// compare the whole strings now
		if ( strncmp ( &haystack[i] , needle , needleSize ) == 0 ) 
			return &haystack[i];			
	}
	return NULL;
}

// independent of case
char *gb_strcasestr ( char *haystack , char *needle ) {
	int32_t needleSize   = gbstrlen(needle);
	int32_t haystackSize = gbstrlen(haystack);
	int32_t n = haystackSize - needleSize ;
	for ( int32_t i = 0 ; i <= n ; i++ ) {
		// keep looping if first chars do not match
		if ( to_lower_a(haystack[i]) != to_lower_a(needle[0]) ) 
			continue;
		// if needle was only 1 char it's a match
		if ( ! needle[1] ) return &haystack[i];
		// compare the whole strings now
		if ( strncasecmp ( &haystack[i] , needle , needleSize ) == 0 ) 
			return &haystack[i];			
	}
	return NULL;
}


char *gb_strncasestr ( char *haystack , int32_t haystackSize , char *needle ) {
	// temp term
	char c = haystack[haystackSize];
	haystack[haystackSize] = '\0';
	char *res = gb_strcasestr ( haystack , needle );
	haystack[haystackSize] = c;
	return res;
}

// . convert < to &lt; and > to &gt
// . store "t" into "s"
// . returns bytes stored into "s"
// . NULL terminates "s" if slen > 0
int32_t saftenTags ( char *s , int32_t slen , char *t , int32_t tlen ) {
	char *start = s ;
	// bail if slen is 0
	if ( slen <= 0 ) return 0;
	// leave a char for the \0
	char *send  = s + slen - 1;
	char *tend  = t + tlen;
	for ( ; t < tend && s + 4 < send ; t++ ) {
		if ( *t == '<' ) {
			*s++ = '&';
			*s++ = 'l';
			*s++ = 't';
			*s++ = ';';
			continue;
		}			
		if ( *t == '>' ) {
			*s++ = '&';
			*s++ = 'g';
			*s++ = 't';
			*s++ = ';';
			continue;
		}			
		*s++ = *t;
	}
	// NULL terminate "s"
	*s = '\0';
	// return # of bytes, excluding \0, stored into s
	return s - start;
}

// . if "doSpecial" is true, then we change &lt;, &gt; and &amp; to
//   the following:
//   UnicodeData.txt:22E6;LESS-THAN BUT NOT EQUIVALENT TO;Sm;0;ON;;;;;Y;
//   UnicodeData.txt:22E7;GREATER-THAN BUT NOT EQUIVALENT TO;Sm;0;ON;;;;;Y;
//   UnicodeData.txt:E0026;TAG AMPERSAND;Cf;0;BN;;;;;N;;;;;
//   UnicodeData.txt:235E;APL FUNCTIONAL SYMBOL QUOTE QUAD;So;0;L;;;;;N;;;;; 
int32_t htmlDecode ( char *dst , char *src , int32_t srcLen , bool doSpecial ,
		  int32_t niceness ) {
	if ( srcLen == 0 ) return 0;
	char *start  = dst;
	char *srcEnd = src + srcLen;
	for ( ; src < srcEnd ; ) {
		// breathe
		QUICKPOLL(niceness);
		// utf8 support?
		char size = getUtf8CharSize(src);
		// all entities must start with '&'
		if ( *src != '&' ) { 
			if ( size == 1 ) { *dst++ = *src++; continue; }
			gbmemcpy ( dst , src , size );
			src += size;
			dst += size;
			continue;
			//*dst++ = *src++; continue; }
		}
		// TODO: avoid doSpecial by not decoding crap in tags...
		//if ( src[0] == '<' ) {
		//	// skip to tag end then!
		//	
		// store decoded entity char into dst[j]
		uint32_t c;
		// "skip" is how many bytes the entites was in "src"
		int32_t skip = getEntity_a (src, srcEnd-src, &c );
		// ignore the "entity" if it was invalid
		if ( skip == 0 ) { *dst++ = *src++ ; continue; }
		// force this now always since some tags contain &quot;
		// and it was causing the tags to be terminated too early
		// for richmondspca.org
		//if ( c == '\"' ) c = '\'';
		//if ( c == '<' ) c = '[';
		//if ( c == '>' ) c = ']';
		// . special mapping
		// . make &lt; and &gt; special so Xml::set() still works
		// . and make &amp; special so we do not screw up summaries
		if ( doSpecial ) {
			// no longer use this!
			//char *xx=NULL;*xx=0;
			if ( c == '<' ) {
				// using [ and ] looks bad in event titles...
				*dst = '|';
				dst++;
				src += skip;
				continue;
				gbmemcpy(dst,"+!-",3);
				//gbmemcpy(dst,"<gb",3); 
				dst += 3; 
				src += skip;
				continue;
				// paragraph sign:
				//c = 0xc2b6;
			}
			if ( c == '>' ) {
				// using [ and ] looks bad in event titles...
				*dst = '|';
				dst++;
				src += skip;
				continue;
				//gbmemcpy(dst,"gb>",3); 
				gbmemcpy(dst,"-!+",3); 
				dst += 3; 
				src += skip;
				continue;
				// high-rise hyphen:
				//c = 0xc2af;
			}
			// some tags have &quot; in their value strings
			// so we have to preserve that!
			// use curling quote:
			//http://www.dwheeler.com/essays/quotes-test-utf-8.html
			// curling double and single quotes resp:
			// &ldquo; &rdquo; &lsquo; &rdquo;
			if ( c == '\"' ) {
				//c = 0x201c; // 0x235e;
				*dst = '\'';
				dst++;
				src += skip;
				continue;
			}
			//if ( c == '<' ) c = 0x22d6; // e6;
			//if ( c == '>' ) c = 0x22d7; // e7;
			// this was working ok, but just code it to an 
			// ampersand. when displaying a page we can code all
			// ampersands back into &amp; i guess! that way
			// the check for a " & " in the place name in 
			// Address.cpp works out...
			//if ( c == '&' ) c = 0xff06; // full width ampersand
		}
		// . otherwise it was a legit entity
		// . store it into "dst" in utf8 format
		// . "numBytes" is how many bytes it stored into 'dst"
		int32_t numBytes = utf8Encode ( c , dst );
		// sanity check. do not eat our tail if dst == src
		if ( numBytes > skip ) { char *xx=NULL;*xx=0; }
		// advance dst ptr
		dst += numBytes;
		// skip over the encoded entity in the source string
		src += skip;
	}
	// NULL term
	*dst = '\0';
	return dst - start;
}

// cdata 
int32_t cdataDecode ( char *dst , char *src , int32_t niceness ) {
	if ( ! src ) return 0;
	char *start  = dst;
	for ( ; *src ; ) {
		// breathe
		QUICKPOLL(niceness);
		// utf8 support?
		char size = getUtf8CharSize(src);
		// see SafeBuf::cdataEncode() we do the opposite here
		if ( src[0] != ']' ||
		     src[1] != ']' ||
		     src[2] != '&' ||
		     src[3] != 'g' ||
		     src[4] != 't' ) {
			if ( size == 1 ) { *dst++ = *src++; continue; }
			gbmemcpy ( dst , src , size );
			src += size;
			dst += size;
			continue;
			//*dst++ = *src++; continue; }
		}
		// make it ]]>
		gbmemcpy ( dst , "]]>" , 3 );
		src += 5;
		dst += 3;
	}
	// NULL term
	*dst = '\0';
	return dst - start;
}

// . make something safe as an form input value by translating the quotes
// . store "t" into "s" and return bytes stored
// . does not do bounds checking
int32_t dequote ( char *s , char *send , char *t , int32_t tlen ) {
	char *start = s;
	char *tend = t + tlen;
	for ( ; t < tend && s < send ; t++ ) {
		if ( *t == '"' ) {
			if ( s + 5 >= send ) return 0;
			*s++ = '&';
			*s++ = '#';
			*s++ = '3';
			*s++ = '4';
			*s++ = ';';
			continue;
		}
		*s++ = *t;		
	}
	// all or nothing
	if ( s + 1 >= send ) return 0;
	*s = '\0';
	return s - start;
}

bool dequote ( SafeBuf* sb , char *t , int32_t tlen ) {
	char *tend = t + tlen;
	for ( ; t < tend; t++ ) {
		if ( *t == '"' ) {
			sb->safeMemcpy("&#34;", 5);
			continue;
		}
		*sb += *t;		
	}
	*sb += '\0';
	return true;
}

//int32_t dequote ( char *s , char *t ) {
//	return dequote ( s , t , gbstrlen ( t ) );
//}

// . entity-ize a string so it's safe for html output
// . store "t" into "s" and return bytes stored
// . does bounds checking
char *htmlEncode ( char *s , char *send , char *t , char *tend , bool pound ,
		   int32_t niceness ) {
	for ( ; t < tend ; t++ ) {
		QUICKPOLL(niceness);
		if ( s + 7 >= send ) { *s = '\0'; return s; }
		if ( *t == '"' ) {
			*s++ = '&';
			*s++ = '#';
			*s++ = '3';
			*s++ = '4';
			*s++ = ';';
			continue;
		}
		if ( *t == '<' ) {
			*s++ = '&';
			*s++ = 'l';
			*s++ = 't';
			*s++ = ';';
			continue;
		}
		if ( *t == '>' ) {
			*s++ = '&';
			*s++ = 'g';
			*s++ = 't';
			*s++ = ';';
			continue;
		}
		if ( *t == '&' ) {
			*s++ = '&';
			*s++ = 'a';
			*s++ = 'm';
			*s++ = 'p';
			*s++ = ';';
			continue;
		}
		if ( *t == '#' && pound ) {
			*s++ = '&';
			*s++ = '#';
			*s++ = '0';
			*s++ = '3';
			*s++ = '5';
			*s++ = ';';
			continue;
		}
		*s++ = *t;		
	}
	*s = '\0';
	return s;
}


// . entity-ize a string so it's safe for html output
// . store "t" into "s" and return true on success
bool htmlEncode ( SafeBuf* s , char *t , char *tend , bool pound ,
		  int32_t niceness ) {
	for ( ; t < tend ; t++ ) {
		QUICKPOLL(niceness);
		if ( *t == '"' ) {
			s->safeMemcpy("&#34;", 5);
			continue;
		}
		if ( *t == '<' ) {
			s->safeMemcpy("&lt;", 4);
			continue;
		}
		if ( *t == '>' ) {
			s->safeMemcpy("&gt;", 4);
			continue;
		}
		if ( *t == '&' ) {
			s->safeMemcpy("&amp;", 5);
			continue;
		}
		if ( *t == '#' && pound ) {
			s->safeMemcpy("&#035;", 6);
			continue;
		}
		// our own specially decoded entites!
		if ( *t == '+' && t[1]=='!' && t[2]=='-' ) {
			s->safeMemcpy("&lt;",4);
			continue;
		}
		// our own specially decoded entites!
		if ( *t == '-' && t[1]=='!' && t[2]=='+' ) {
			s->safeMemcpy("&gt;",4);
			continue;
		}
		*s += *t;
	}
	*s += '\0';
	return true;
}

// . convert "-->%22 , &-->%26, +-->%2b, space-->+, ?-->%3f is that it?
// . convert so we can display as a cgi PARAMETER within a url
// . used by HttPage2 (cached web page) to encode the query into a url
// . used by PageRoot to do likewise
// . returns bytes written into "d" not including terminating \0
int32_t urlEncode ( char *d , int32_t dlen , char *s , int32_t slen, bool requestPath ) {
	char *dstart = d;
	// subtract 1 to make room for a terminating \0
	char *dend = d + dlen - 1;
	char *send = s + slen;
	for ( ; s < send && d < dend ; s++ ) {
		if ( *s == '\0' && requestPath ) {
			*d++ = *s;
			continue;
		}
		// encode if not fit for display
		if ( ! is_ascii ( *s ) ) goto encode;
		switch ( *s ) {
		case ' ': goto encode;
		case '&': goto encode;
		case '"': goto encode;
		case '+': goto encode;
		case '%': goto encode;
		case '#': goto encode;
		// encoding < and > are more for displaying on an
		// html page than sending to an http server
		case '>': goto encode;
		case '<': goto encode;
		case '?': if ( requestPath ) break;
			  goto encode;
		}
		// otherwise, no need to encode
		*d++ = *s;
		continue;
	encode:
		// space to +
		if ( *s == ' ' && d + 1 < dend ) { *d++ = '+'; continue; }
		// break out if no room to encode
		if ( d + 2 >= dend ) break;
		*d++ = '%';
		// store first hex digit
		unsigned char v = ((unsigned char)*s)/16 ;
		if ( v < 10 ) v += '0';
		else          v += 'A' - 10;
		*d++ = v;
		// store second hex digit
		v = ((unsigned char)*s) & 0x0f ;
		if ( v < 10 ) v += '0';
		else          v += 'A' - 10;
		*d++ = v;
	}
	// NULL terminate it
	*d = '\0';
	// and return the length
	return d - dstart;
}

// determine the length of the encoded url, does NOT include NULL
int32_t urlEncodeLen ( char *s , int32_t slen , bool requestPath ) {
	int32_t  dLen = 0;
	char *send = s + slen;
	for ( ; s < send ; s++ ) {
		if ( *s == '\0' && requestPath ) {
			dLen++;
			continue;
		}
		// encode if not fit for display
		if ( ! is_ascii ( *s ) ) goto encode;
		switch ( *s ) {
		case ' ': goto encode;
		case '&': goto encode;
		case '"': goto encode;
		case '+': goto encode;
		case '%': goto encode;
		case '#': goto encode;
		// encoding < and > are more for displaying on an
		// html page than sending to an http server
		case '>': goto encode;
		case '<': goto encode;
		case '?': if ( requestPath ) break;
			  goto encode;
		}
		// otherwise, no need to encode
		dLen++;
		continue;
	encode:
		// space to +
		if ( *s == ' ' ) { dLen++; continue; }
		// hex code
		dLen += 3; // %XX
	}
	//dLen++; // NULL TERM
	// and return the length
	return dLen;
}

// . decodes "s/slen" and stores into "dest"
// . returns the number of bytes stored into "dest"
int32_t urlDecode ( char *dest , char *s , int32_t slen ) {
	int32_t j = 0;
	for ( int32_t i = 0 ; i < slen ; i++ ) {
		if ( s[i] == '+' ) { dest[j++]=' '; continue; }
		dest[j++] = s[i];
		if ( s[i]  != '%'  ) continue;
		if ( i + 2 >= slen ) continue;
		// if two chars after are not hex chars, it's not an encoding
		if ( ! is_hex ( s[i+1] ) ) continue;
		if ( ! is_hex ( s[i+2] ) ) continue;
		// convert hex chars to values
		unsigned char a = htob ( s[i+1] ) * 16; 
		unsigned char b = htob ( s[i+2] )     ;
		dest[j-1] = (char) (a + b);
		i += 2;
	}
	return j;
}


int32_t urlDecodeNoZeroes ( char *dest , char *s , int32_t slen ) {
	int32_t j = 0;
	for ( int32_t i = 0 ; i < slen ; i++ ) {
		if ( s[i] == '+' ) { dest[j++]=' '; continue; }
		dest[j++] = s[i];
		if ( s[i]  != '%'  ) continue;
		if ( i + 2 >= slen ) continue;
		// if two chars after are not hex chars, it's not an encoding
		if ( ! is_hex ( s[i+1] ) ) continue;
		if ( ! is_hex ( s[i+2] ) ) continue;
		// convert hex chars to values
		unsigned char a = htob ( s[i+1] ) * 16; 
		unsigned char b = htob ( s[i+2] )     ;
		// NO ZEROES! fixes &content= having decoded \0's in it
		// and setting our parms
		if ( a + b == 0 ) {
			log("fctypes: urlDecodeNoZeros encountered url "
			    "encoded zero. truncating http request.");
			return j; 
		}
		dest[j-1] = (char) (a + b);
		i += 2;
	}
	return j;
}

// . like above, but only decodes chars that should not have been encoded
// . will also encode binary chars
int32_t urlNormCode ( char *d , int32_t dlen , char *s , int32_t slen ) {
	// save start of detination buffer for returning the length
	char *dstart = d;
	// subtract 1 for NULL termination
	char *dend = d + dlen - 1;
	char *send = s + slen;
	for ( ; s < send && d < dend ; s++ ) {
		// if its non-ascii, encode it so it displays correctly
		if ( ! is_ascii ( *s ) ) {
			// break if no room to encode it
			if ( d + 2 >= dend ) break;
			// store it encoded
			*d++ = '%';
			// store first hex digit
			unsigned char v = ((unsigned char)*s)/16 ;
			if ( v < 10 ) v += '0';
			else          v += 'A' - 10;
			*d++ = v;
			// store second hex digit
			v = ((unsigned char)*s) & 0x0f ;
			if ( v < 10 ) v += '0';
			else          v += 'A' - 10;
			*d++ = v;
			continue;
		}
		// store it
		*d++ = *s;
		// but it might be something encoded that should not have been
		if ( *s != '%' ) continue;
		// it requires to following chars to decode
		if ( s + 2 >= send ) continue;
		// if two chars after are not hex chars, it's not an encoding
		if ( ! is_hex ( s[1] ) ) continue;
		if ( ! is_hex ( s[2] ) ) continue;
		// convert hex chars to values
		unsigned char a = htob ( s[1] ) * 16; 
		unsigned char b = htob ( s[2] )     ;
		unsigned char v = a + b;
		// don't decode if it decodes in these chars
		switch ( v ) {
		case ' ': continue;
		case '&': continue;
		case '"': continue;
		case '+': continue;
		case '%': continue;
		case '>': continue;
		case '<': continue;
		case '?': continue;
		case '=': continue;
		}
		// otherwise, it's fine to decode it
		d[-1] = (char) (a + b);
		// skip over those 2 chars as well as leading '%'
		s += 2;
	}
	// NULL terminate
	*d = '\0';
	// return length
	return d - dstart ;
}

// approximate # of non-punct words
int32_t getNumWords ( char *s ) {
	int32_t count = 0;
 loop:
	// skip punct
	while ( ! is_alnum_a(*s) ) s++;
	// bail if done
	if ( !*s ) return count;
	// count a word
	count++;
	// skip word
	while ( is_alnum_a(*s) ) s++;
	// watch for ' letter punct
	if ( *s=='\'' && is_alnum_a(*(s+1))  && !is_alnum_a(*(s+2)) ) {
		// skip apostrophe
		s++;
		// skip rest of word
		while ( is_alnum_a(*s) ) s++;
	}
	goto loop;
}

static int64_t s_adjustment = 0;

int64_t globalToLocalTimeMilliseconds ( int64_t global ) {
	// sanity check
	//if ( ! g_clockInSync ) 
	//	log("gb: Converting global time but clock not in sync.");
	return global - s_adjustment;
}

int64_t localToGlobalTimeMilliseconds ( int64_t local ) {
	// sanity check
	//if ( ! g_clockInSync ) 
	//	log("gb: Converting global time but clock not in sync.");
	return local + s_adjustment;
}

int32_t globalToLocalTimeSeconds ( int32_t global ) {
	// sanity check
	//if ( ! g_clockInSync ) 
	//	log("gb: Converting global time but clock not in sync.");
	return global - (s_adjustment/1000);
}

int32_t localToGlobalTimeSeconds ( int32_t local ) {
	// sanity check
	//if ( ! g_clockInSync ) 
	//	log("gb: Converting global time but clock not in sync.");
	return local + (s_adjustment/1000);
}

#include "Timedb.h"


static char s_tafile[1024];
static bool s_hasFileName = false;

// returns false and sets g_errno on error
bool setTimeAdjustmentFilename ( char *dir, char *filename ) {
	s_hasFileName = true;
	int32_t len1 = gbstrlen(dir);
	int32_t len2 = gbstrlen(filename);
	if ( len1 + len2 > 1000 ) { char *xx=NULL;*xx=0; }
	sprintf(s_tafile,"%s/%s",dir,filename);
	return true;
}

// returns false and sets g_errno on error
bool loadTimeAdjustment ( ) {
	// bail if no filename to read
	if ( ! s_hasFileName ) return true;
	// read it in
	// one line in text
	int fd = open ( s_tafile , O_RDONLY );
	if ( fd < 0 ) {
		log("util: could not open %s for reading",s_tafile);
		g_errno = errno;
		return false;
	}
	char rbuf[1024];
	// read in max bytes
	int nr = read ( fd , rbuf , 1000 );
	if ( nr <= 10 || nr > 1000 ) {
		log("util: reading %s had error: %s",s_tafile,
		    mstrerror(errno));
		close(fd);
		g_errno = errno;
		return false;
	}
	close(fd);
	// parse the text line
	int64_t stampTime = 0LL;
	int64_t clockAdj  = 0LL;
	sscanf ( rbuf , "%"UINT64" %"INT64"", &stampTime, &clockAdj );
	// get stamp age
	int64_t local = gettimeofdayInMillisecondsLocal();
	int64_t stampAge = local - stampTime;
	// if too old forget about it
	if ( stampAge > 2*86400 ) return true;
	// update adjustment
	s_adjustment = clockAdj;
	// if stamp in file is within 2 days old, assume its still good
	// this will prevent having to rebuild a sortbydatetable
	// and really slow down loadups
	g_clockInSync = true;
	// note it
	log("util: loaded %s and put clock in sync. age=%"UINT64" adj=%"INT64"",
	    s_tafile,stampAge,clockAdj);
	return true;
}

// . returns false and sets g_errno on error
// . saved by Process::saveBlockingFiles1()
bool saveTimeAdjustment ( ) {
	// fortget it if setTimeAdjustmentFilename never called
	if ( ! s_hasFileName ) return true;
	// must be in sync!
	if ( ! g_clockInSync ) return true;
	// store it
	int64_t local = gettimeofdayInMillisecondsLocal();
	char wbuf[1024];
	sprintf (wbuf,"%"UINT64" %"INT64"\n",local,s_adjustment);
	// write it out
	int fd = open ( s_tafile , O_CREAT|O_RDWR|O_TRUNC , 00666 );
	if ( fd < 0 ) {
		log("util: could not open %s for writing",s_tafile);
		g_errno = errno;
		return false;
	}
	// how many bytes to write?
	int32_t len = gbstrlen(wbuf);
	// read in max bytes
	int nw = write ( fd , wbuf , len );
	if ( nw != len ) {
		log("util: writing %s had error: %s",s_tafile,
		    mstrerror(errno));
		close(fd);
		g_errno = errno;
		return false;
	}
	close(fd);
	// note it
	log("util: saved %s",s_tafile);
	// it was written ok
	return true;
}

// a "fake" settimeofdayInMilliseconds()
void settimeofdayInMillisecondsGlobal ( int64_t newTime ) {
	// can't do this in sig handler
	if ( g_inSigHandler ) return;
	// this isn't async signal safe...
	struct timeval tv;
	gettimeofday ( &tv , NULL );
	int64_t now=(int64_t)(tv.tv_usec/1000)+((int64_t)tv.tv_sec)*1000;
	// bail if no change... UNLESS we need to sync clock!!
	if ( s_adjustment == newTime - now && g_clockInSync ) return;
	// log it, that way we know if there is another issue
	// with flip-flopping (before we synced with host #0 and also
	// with proxy #0)
	int64_t delta = s_adjustment - (newTime - now) ;
	if ( delta > 100 || delta < -100 )
		logf(LOG_INFO,"gb: Updating clock adjustment from "
		     "%"INT64" ms to %"INT64" ms", s_adjustment , newTime - now );
	// set adjustment
	s_adjustment = newTime - now;
	// return?
	if ( g_clockInSync ) return;
	// we are now in sync
	g_clockInSync = true;
	// log it
	if ( s_hasFileName )
		logf(LOG_INFO,"gb: clock is now synced with host #0. "
		     "saving to %s",s_tafile);
	else
		logf(LOG_INFO,"gb: clock is now synced with host #0.");
	// save
	saveTimeAdjustment();
	// force timedb to load now!
	//initAllSortByDateTables ( );
}

time_t getTimeGlobal() {
	return gettimeofdayInMillisecondsSynced() / 1000;
}

time_t getTimeGlobalNoCore() {
	return gettimeofdayInMillisecondsGlobalNoCore() / 1000;
}

time_t getTimeSynced() {
	return gettimeofdayInMillisecondsSynced() / 1000;
}

int64_t gettimeofdayInMillisecondsGlobal() {
	return gettimeofdayInMillisecondsSynced();
}

#include "Threads.h"

int64_t gettimeofdayInMillisecondsSynced() {
	// if in a sig handler then return g_now
	//if ( g_inSigHandler ) return g_nowGlobal;
	// i find that a pthread can call this function even though
	// a signal handler is underway in the main thread!
	if ( g_inSigHandler && ! g_threads.amThread() ) { 
		char *xx = NULL; *xx = 0; }
	// sanity check
	if ( ! isClockInSync() ) { 
		static int s_printed = 0;
		if ( (s_printed % 100) == 0 ) {
			s_printed++;
			log("xml: clock not in sync with host #0 yet!!!!!!");
		}
		//char *xx = NULL; *xx = 0; }
	}

	int64_t now;

	// the real tiem sigalrm interrupt in Loop.cpp sets this to
	// true once per millisecond
	if ( ! g_clockNeedsUpdate ) {
		now = g_now;
	}
	else {
		//if ( ! g_clockInSync ) 
		//	log("gb: Getting global time but clock not in sync.");
		// this isn't async signal safe...
		struct timeval tv;
		gettimeofday ( &tv , NULL );
		now = (int64_t)(tv.tv_usec/1000)+((int64_t)tv.tv_sec)*1000;
	}

	// update g_nowLocal
	if ( now > g_now ) g_now = now;

	g_clockNeedsUpdate = false;

	// adjust from Msg0x11 time adjustments
	now += s_adjustment;
	// update g_now if it is more accurate
	//if ( now > g_nowGlobal ) g_nowGlobal = now;
	return now;
}

int64_t gettimeofdayInMillisecondsGlobalNoCore() {
	// if in a sig handler then return g_now
	//if ( g_inSigHandler ) return g_nowGlobal;
	// i find that a pthread can call this function even though
	// a signal handler is underway in the main thread!
	if ( g_inSigHandler && ! g_threads.amThread() ) { 
		char *xx = NULL; *xx = 0; }
	// sanity check
	//if ( ! g_clockInSync ) { char *xx = NULL; *xx = 0; }
	//if ( ! g_clockInSync ) 
	//	log("gb: Getting global time but clock not in sync.");
	// this isn't async signal safe...
	struct timeval tv;
	gettimeofday ( &tv , NULL );
	int64_t now=(int64_t)(tv.tv_usec/1000)+((int64_t)tv.tv_sec)*1000;
	// update g_nowLocal
	if ( now > g_now ) g_now = now;
	// adjust from Msg0x11 time adjustments
	now += s_adjustment;
	// update g_now if it is more accurate
	//if ( now > g_nowGlobal ) g_nowGlobal = now;
	return now;
}

int64_t gettimeofdayInMillisecondsLocal() {
	return gettimeofdayInMilliseconds();
}

uint64_t gettimeofdayInMicroseconds(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return(((uint64_t)tv.tv_sec * 1000000LL) + (uint64_t)tv.tv_usec);
}

// "local" means the time on this machine itself, NOT a timezone thing.
int64_t gettimeofdayInMilliseconds() {
	// if in a sig handler then return g_now
	//if ( g_inSigHandler ) return g_now;
	// i find that a pthread can call this function even though
	// a signal handler is underway in the main thread!
	if ( g_inSigHandler && ! g_threads.amThread() ) { 
		char *xx = NULL; *xx = 0; }

	// the real tiem sigalrm interrupt in Loop.cpp sets this to
	// true once per millisecond
	if ( ! g_clockNeedsUpdate )
		return g_now;

	g_clockNeedsUpdate = false;

	// this isn't async signal safe...
	struct timeval tv;
	//g_loop.disableTimer();
	gettimeofday ( &tv , NULL );
	//g_loop.enableTimer();
	int64_t now=(int64_t)(tv.tv_usec/1000)+((int64_t)tv.tv_sec)*1000;
	// update g_nowLocal
	if ( now > g_now ) g_now = now;
	// adjust from Msg0x11 time adjustments
	//now += s_adjustment;
	// update g_now if it is more accurate
	// . or don't, bad to update it here because it could be very different
	//   from what it should be
	//if ( now > g_now ) g_now = now;
	return now;
}


int64_t gettimeofdayInMilliseconds_force ( ) {
  g_clockNeedsUpdate = true;
  return gettimeofdayInMilliseconds();
}

time_t getTime () {
	return getTimeLocal();
}

// . get time in seconds
// . use this instead of call to time(NULL) cuz it uses adjustment
time_t getTimeLocal () {
	// if in a sig handler then return g_now/1000
	//if ( g_inSigHandler ) return (time_t)(g_now / 1000);
	// i find that a pthread can call this function even though
	// a signal handler is underway in the main thread!
	if ( g_inSigHandler && ! g_threads.amThread() ) { 
		char *xx = NULL; *xx = 0; }
	// get time now
	uint32_t now = gettimeofdayInMilliseconds() / 1000;
	// and adjust it
	//now += s_adjustment / 1000;
	return (time_t)now;
}

// . make it so we can display the ascii string on an html browser
int32_t saftenTags2 ( char *s , int32_t slen , char *t , int32_t tlen ) {
	char *start = s ;
	// bail if slen is 0
	if ( slen <= 0 ) return 0;
	// leave a char for the \0
	char *send  = s + slen - 1;
	char *tend  = t + tlen;
	for ( ; t < tend && s + 6 < send ; t++ ) {
		if ( *t == '<' ) {
			*s++ = '&';
			*s++ = 'l';
			*s++ = 't';
			*s++ = ';';
			continue;
		}			
		if ( *t == '>' ) {
			*s++ = '&';
			*s++ = 'g';
			*s++ = 't';
			*s++ = ';';
			continue;
		}	
		if ( *t == '&' ) {
			*s++ = '&';
			*s++ = 'a';
			*s++ = 'm';
			*s++ = 'p';
			*s++ = ';';
			continue;
		}
		*s++ = *t;
	}
	// return NULL if we broke out because there was not enough room
	//if ( s + 6 >= send ) return NULL;
	// NULL terminate "s"
	*s = '\0';
	// return # of bytes, excluding \0, stored into s
	return s - start;
}

void getCalendarFromMs(int64_t ms, 
		       int32_t* days, 
		       int32_t* hours, 
		       int32_t* minutes, 
		       int32_t* secs,
		       int32_t* msecs) {
	int32_t s =     1000;
	int32_t m = s * 60;
	int32_t h = m * 60;
	int32_t d = h * 24;

	*days = ms / d;
	int64_t tmp = ms % d;
	*hours = tmp / h;
	tmp = tmp % h;
	*minutes = tmp / m;
	tmp = tmp % m;
	*secs = tmp / s;
	
	*msecs = tmp % s;
}

uint32_t calculateChecksum(char *buf, int32_t bufLen){
	uint32_t sum = 0;
	for(int32_t i = 0; i < bufLen>>2;i++)
		sum += ((uint32_t*)buf)[i];
	return sum;
}

bool anchorIsLink( char *tag, int32_t tagLen){
	if (strncasestr(tag, tagLen, "href")) return true;
	if (strncasestr(tag, tagLen, "onclick")) return true;
	return false;
}

bool has_alpha_a ( char *s , char *send ) {
	for ( ; s < send ; s++ ) 
		if (is_alpha_a(*s)) return true;
	return false;
}

bool has_alpha_utf8 ( char *s , char *send ) {
	char cs = 0;
	for ( ; s < send ; s += cs ) {
		cs = getUtf8CharSize ( s );
		if ( cs == 1 ) {
			if (is_alpha_a(*s)) return true;
			continue;
		}
		if ( is_alpha_utf8(s) ) return true;
	}
	return false;
}

//takes an input skips leading spaces 
//puts next nonspace char* in numPtr
//an returns the next space after that.
char* getNextNum(char* input, char** numPtr) {
	char* p = input;
	char* nextspace;
	while(*p && isspace(*p)) p++;
	nextspace = p;
	*numPtr = p;
	while(*nextspace && !isspace(*nextspace)) 
		nextspace++;
	return nextspace;
}

#include "HttpMime.h" // CT_HTML

// returns length of stripped content, but will set g_errno and return -1
// on error
int32_t stripHtml( char *content, int32_t contentLen, int32_t version, int32_t strip ) {
	if ( !strip ) {
		log( LOG_WARN, "query: html stripping not required!" );
		return contentLen;
	}
	if ( ! content )
		return 0;
	if ( contentLen == 0 )
		return 0;

	// filter content if we should
	// keep this on the big stack so "content" still references something
	Xml tmpXml;
	// . get the content as xhtml (should be NULL terminated)
	// . parse as utf8 since all we are doing is messing with 
	//   the tags...content manipulation comes later
	if ( ! tmpXml.set ( content , contentLen,
			    false, 0, false, version , true , 0 , CT_HTML ) )
		return -1;

	//if( strip == 4 )
	//	return tmpXml.getText( content, contentLen );

	// go tag by tag
	int32_t     n       = tmpXml.getNumNodes();
	XmlNode *nodes   = tmpXml.getNodes();
	// Xml class may have converted to utf16
	content    = tmpXml.getContent();
	contentLen = tmpXml.getContentLen();
	char    *x       = content;
	char    *xend    = content + contentLen;
	int32_t     stackid = -1;
	int32_t     stackc  =  0;
	char     skipIt  =  0;
	// . hack COL tag to NOT require a back tag
	// . do not leave it that way as it could mess up our parsing
	//g_nodes[25].m_hasBackTag = 0;
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// get id of this node
		int32_t id = nodes[i].m_nodeId;
		
		// if strip is 4, just remove the script tag
		if( strip == 4 ){
			if ( id ){
				if ( id == 83 ){
					skipIt ^= 1;
					continue;
				}
			}
			else if ( skipIt ) continue;
			goto keepit;
		}
		
		// if strip is 3, ALL tags will be removed!
		if( strip == 3 ) {
			if( id ) {
				// . we dont want anything in between:
				//   - script tags (83)
				//   - style tags  (111)
				if ((id == 83) || (id == 111)) skipIt ^= 1;
				// save img to have alt text kept.
				if ( id == 54  ) goto keepit;
				continue;
			}
			else {
				if( skipIt ) continue;
				goto keepit;
			}
		}
		// get it
		int32_t fk;
		if   ( strip == 1 ) fk = g_nodes[id].m_filterKeep1;
		else                fk = g_nodes[id].m_filterKeep2;
		// if tag is <link ...> only keep it if it has
		// rel="stylesheet" or rel=stylesheet
		if ( strip == 2 && id == 62 ) { // <link> tag id
			int32_t   fflen;
			char *ff = nodes[i].getFieldValue ( "rel" , &fflen );
			if ( ff && fflen == 10 &&
			     strncmp(ff,"stylesheet",10) == 0 )
				goto keepit;
		}
		// just remove just the tag if this is 2
		if ( fk == 2 ) continue;
		// keep it if not in a stack
		if ( ! stackc && fk ) goto keepit;
		// if no front/back for tag, just skip it
		if ( ! nodes[i].m_hasBackTag ) continue;
		// start stack if none
		if ( stackc == 0 ) {
			// but not if this is a back tag
			if ( nodes[i].m_node[1] == '/' ) continue;
			// now start the stack
			stackid = id;
			stackc  =  1;
			continue;
		}
		// skip if this tag does not match what is on stack
		if ( id != stackid ) continue;
		// if ANOTHER front tag, inc stack
		if ( nodes[i].m_node[1] != '/' ) stackc++;
		// otherwise, dec the stack count
		else                             stackc--;
		// . ensure not negative from excess back tags
		// . reset stackid to -1 to indicate no stack
		if ( stackc <= 0 ) { stackid= -1; stackc = 0; }
		// skip it
		continue;
	keepit:
		// replace images with their alt text
		int32_t vlen;
		char *v;
		if ( id == 54 ) {
			v = nodes[i].getFieldValue("alt", &vlen );
			// try title if no alt text
			if ( ! v )
				v = nodes[i].getFieldValue("title", &vlen );
			if ( v ) { gbmemcpy ( x, v, vlen ); x += vlen; }
			continue;
		}
		// remove background image from body,table,td tags
		if ( id == 19 || id == 93 || id == 95 ) {
			v = nodes[i].getFieldValue("background", &vlen);
			// remove background, just sabotage it
			if ( v ) v[-4] = 'x';
		}
		// store it
		gbmemcpy ( x , nodes[i].m_node , nodes[i].m_nodeLen );
		x += nodes[i].m_nodeLen;
		// sanity check
		if ( x > xend ) { char *xx=NULL;*xx=0;}
	}
	contentLen = x - content;
	content [ contentLen ] = '\0';
	// unhack COL tag
	//g_nodes[25].m_hasBackTag = 1;
	return contentLen;
}


bool is_urlchar(char s) {
	// [a-z0-9/:_-.?$,~=#&%+@]
	if(isalnum(s)) return true;
	if(s == '/' ||
	   s == ':' ||
	   s == '_' ||
	   s == '-' ||
	   s == '.' ||
	   s == '?' ||
	   s == '$' ||
	   s == ',' ||
	   s == '~' ||
	   s == '=' ||
	   s == '#' ||
	   s == '&' ||
	   s == '%' ||
	   s == '+' ||
	   s == '@') return true;
	return false;
}
// don't allow "> in our input boxes
int32_t cleanInput(char *outbuf, int32_t outbufSize, char *inbuf, int32_t inbufLen){
	char *p = outbuf;
	int32_t numQuotes=0;
	int32_t lastQuote = 0;
	for (int32_t i=0;i<inbufLen;i++){
		if (p-outbuf >= outbufSize-1) break;
			
		if (inbuf[i] == '"'){
			numQuotes++;
			lastQuote = i;
		}
		// if we have an odd number of quotes and a close angle bracket
		// it could be an xss attempt
		if (inbuf[i] == '>' && (numQuotes & 1)) {
			p = outbuf+lastQuote;
			break;
		}
		*p = inbuf[i];
		p++;
	}
	*p = '\0';
	return p-outbuf;
}


//
// get rid of the virtual Msg class because it screws up how we
// serialize/deserialize everytime we compile gb it seems
//

int32_t getMsgStoredSize ( int32_t baseSize, 
			int32_t *firstSizeParm, 
			int32_t *lastSizeParm ) {
	//int32_t size = (int32_t)sizeof(Msg);
	int32_t size = baseSize;//getBaseSize();
	// add up string buffer sizes
	int32_t *sizePtr = firstSizeParm;//getFirstSizeParm(); // &size_qbuf;
	int32_t *sizeEnd = lastSizeParm;//getLastSizeParm (); // &size_displayMeta
	for ( ; sizePtr <= sizeEnd ; sizePtr++ )
		size += *sizePtr;
	return size;
}

// . return ptr to the buffer we serialize into
// . return NULL and set g_errno on error
char *serializeMsg ( int32_t  baseSize ,
		     int32_t *firstSizeParm ,
		     int32_t *lastSizeParm ,
		     char **firstStrPtr ,
		     void *thisPtr ,
		     int32_t *retSize     ,
		     char *userBuf     ,
		     int32_t  userBufSize ,
		     bool  makePtrsRefNewBuf ) {
	// make a buffer to serialize into
	char *buf  = NULL;
	//int32_t  need = getStoredSize();
	int32_t need = getMsgStoredSize(baseSize,firstSizeParm,lastSizeParm);
	// big enough?
	if ( need <= userBufSize ) buf = userBuf;
	// alloc if we should
	if ( ! buf ) buf = (char *)mmalloc ( need , "Ra" );
	// bail on error, g_errno should be set
	if ( ! buf ) return NULL;
	// set how many bytes we will serialize into
	*retSize = need;
	// copy the easy stuff
	char *p = buf;
	gbmemcpy ( p , (char *)thisPtr , baseSize );//getBaseSize() );
	p += baseSize; // getBaseSize();
	// then store the strings!
	int32_t  *sizePtr = firstSizeParm;//getFirstSizeParm(); // &size_qbuf;
	int32_t  *sizeEnd = lastSizeParm;//getLastSizeParm (); // &size_displayMet
	char **strPtr  = firstStrPtr;//getFirstStrPtr  (); // &ptr_qbuf;
	for ( ; sizePtr <= sizeEnd ;  ) {
		// if we are NULL, we are a "bookmark", so
		// we alloc'd space for it, but don't copy into
		// the space until after this call toe serialize()
		if ( ! *strPtr ) goto skip;
		// sanity check -- cannot copy onto ourselves
		if ( p > *strPtr && p < *strPtr + *sizePtr ) {
			char *xx = NULL; *xx = 0; }
		// copy the string into the buffer
		gbmemcpy ( p , *strPtr , *sizePtr );
	skip:
		// . make it point into the buffer now
		// . MDW: why? that is causing problems for the re-call in
		//   Msg3a, it calls this twice with the same "m_r"
		if ( makePtrsRefNewBuf ) *strPtr = p;
		// advance our destination ptr
		p += *sizePtr;
		// advance both ptrs to next string
		sizePtr++;
		strPtr++;
	}
	return buf;
}

char *serializeMsg2 ( void *thisPtr ,
		      int32_t objSize ,
		      char **firstStrPtr ,
		      int32_t *firstSizeParm ,
		      int32_t *retSize ) {

	// make a buffer to serialize into
	char *buf  = NULL;
	int32_t baseSize = (char *)firstStrPtr - (char *)thisPtr;
	int nptrs=((char *)firstSizeParm-(char *)firstStrPtr)/sizeof(char *);
	int32_t need = baseSize;
	need += nptrs * sizeof(char *);
	need += nptrs * sizeof(int32_t);
	// tally up the string sizes
	int32_t  *srcSizePtr = (int32_t *)firstSizeParm;
	char **srcStrPtr  = (char **)firstStrPtr;
	int32_t totalStringSizes = 0;
	for ( int i = 0 ; i < nptrs ; i++ ) {
		if ( srcStrPtr[i] == NULL ) continue;
		totalStringSizes += srcSizePtr[i];

	}
	int32_t stringBufferOffset = need;
	need += totalStringSizes;
	// alloc if we should
	if ( ! buf ) buf = (char *)mmalloc ( need , "sm2" );
	// bail on error, g_errno should be set
	if ( ! buf ) return NULL;
	// set how many bytes we will serialize into
	*retSize = need;
	// copy everything over except strings themselves
	char *p = buf;
	gbmemcpy ( p , (char *)thisPtr , stringBufferOffset );//need );
	// point to the string buffer
	p += stringBufferOffset;
	// then store the strings!
	char **dstStrPtr = (char **)(buf + baseSize );
	int32_t *dstSizePtr = (int32_t *)(buf + baseSize+sizeof(char *)*nptrs);
	for ( int count = 0 ; count < nptrs ; count++ ) {
		// copy ptrs
		//*dstStrPtr = *srcStrPtr;
		//*dstSizePtr = *srcSizePtr;
		// if we are NULL, we are a "bookmark", so
		// we alloc'd space for it, but don't copy into
		// the space until after this call toe serialize()
		if ( ! *srcStrPtr )
			goto skip;
		// if this is valid then size can't be 0! fix upstream.
		if ( ! *srcSizePtr ) { char *xx=NULL;*xx=0; }
		// if size is 0 use gbstrlen. helps with InjectionRequest
		// where we set ptr_url or ptr_content but not size_url, etc.
		//if ( ! *srcSizePtr )
		//	*srcSizePtr = gbstrlen(*strPtr);
		// sanity check -- cannot copy onto ourselves
		if ( p > *srcStrPtr && p < *srcStrPtr + *srcSizePtr ) {
			char *xx = NULL; *xx = 0; }
		// copy the string into the buffer
		gbmemcpy ( p , *srcStrPtr , *srcSizePtr );
	skip:
		// point it now into the string buffer
		*dstStrPtr = p;
		// if it is 0 length, make ptr NULL in destination
		if ( *srcSizePtr == 0 || *srcStrPtr == NULL ) {
			*dstStrPtr = NULL;
			*dstSizePtr = 0;
		}
		// advance our destination ptr
		p += *dstSizePtr;
		// advance both ptrs to next string
		srcSizePtr++;
		srcStrPtr++;
		dstSizePtr++;
		dstStrPtr++;
	}
	return buf;
}


// convert offsets back into ptrs
int32_t deserializeMsg ( int32_t  baseSize ,
		      int32_t *firstSizeParm ,
		      int32_t *lastSizeParm ,
		      char **firstStrPtr ,
		      char *stringBuf ) {
	// point to our string buffer
	char *p = stringBuf;//getStringBuf(); // m_buf;
	// then store the strings!
	int32_t  *sizePtr = firstSizeParm;//getFirstSizeParm(); // &size_qbuf;
	int32_t  *sizeEnd = lastSizeParm;//getLastSizeParm (); // &size_displayMet
	char **strPtr  = firstStrPtr;//getFirstStrPtr  (); // &ptr_qbuf;
	for ( ; sizePtr <= sizeEnd ;  ) {
		// convert the offset to a ptr
		*strPtr = p;
		// make it NULL if size is 0 though
		if ( *sizePtr == 0 ) *strPtr = NULL;
		// sanity check
		if ( *sizePtr < 0 ) { g_errno = ECORRUPTDATA; return -1;}
		// advance our destination ptr
		p += *sizePtr;
		// advance both ptrs to next string
		sizePtr++;
		strPtr++;
	}
	// return how many bytes we processed
	return baseSize + (p - stringBuf);//getStringBuf());
}

bool deserializeMsg2 ( char    **firstStrPtr , // ptr_url
		       int32_t  *firstSizeParm ) { // size_url
	int nptrs=((char *)firstSizeParm-(char *)firstStrPtr)/sizeof(char *);
	// point to our string buffer
	char *p = ((char *)firstSizeParm + sizeof(int32_t)*nptrs);
	// then store the strings!
	int32_t  *sizePtr = firstSizeParm;//getFirstSizeParm(); // &size_qbuf;
	//int32_t  *sizeEnd = lastSizeParm;//getLastSizeParm (); // &size_displ
	char **strPtr  = firstStrPtr;//getFirstStrPtr  (); // &ptr_qbuf;
	int count = 0;
	for ( ; count < nptrs ; count++ ) { // sizePtr <= sizeEnd ;  ) {
		// convert the offset to a ptr
		*strPtr = p;
		// make it NULL if size is 0 though
		if ( *sizePtr == 0 ) *strPtr = NULL;
		// sanity check
		if ( *sizePtr < 0 ) return false;//{ char *xx = NULL; *xx =0; }
		// advance our destination ptr
		p += *sizePtr;
		// advance both ptrs to next string
		sizePtr++;
		strPtr++;
	}
	// return how many bytes we processed
	//return baseSize + (p - stringBuf);//getStringBuf());
	return true;
}

// print it to stdout for debugging Dates.cpp
int32_t printTime ( time_t ttt ) {
	//char *s = ctime(&ttt);
	// print in UTC!
	char *s = asctime ( gmtime(&ttt) );
	// strip \n
	s[gbstrlen(s)-1] = '\0';
	fprintf(stderr,"%s UTC\n",s);
	return 0;
}

// this uses our local timezone which is MST, so we need to tell
// it to use UTC somehow...
time_t mktime_utc ( struct tm *ttt ) {
	time_t local = mktime ( ttt );
	// bad?
	if ( local < 0 ) return local;
	/*
	// sanity check
	static char s_mm = 1;
	static int32_t s_localOff;
	if ( s_mm ) {
		s_mm = 0;
		struct tm ff;
		ff.tm_mon  = 0;
		ff.tm_year = 70;
		ff.tm_mday  = 1;
		ff.tm_hour = 0;
		ff.tm_min  = 0;
		ff.tm_sec  = 0;
		int32_t qq = mktime ( &ff );
		//fprintf(stderr,"qq=%"INT32"\n",qq);
		// . set this then
		// . we subtract s_localOff to further mktime() returns to
		//   get it into utc
		s_localOff = qq;
		// sanity
		if ( s_localOff != timezone ) { char *xx=NULL;*xx=0; }
	}
	*/
	// see what our timezone is!
	//fprintf(stderr,"%"INT32"=tz\n",timezone);
	// mod that
	return local - timezone;
}

bool verifyUtf8 ( char *txt , int32_t tlen ) {
	if ( ! txt  || tlen <= 0 ) return true;
	char size;
	char *p = txt;
	char *pend = txt + tlen;
	for ( ; p < pend ; p += size ) {
		size = getUtf8CharSize(p);
		// skip if ascii
		if ( ! (p[0] & 0x80) ) continue;
		// ok, it's a utf8 char, it must have both hi bits set
		if ( (p[0] & 0xc0) != 0xc0 ) return false;
		// if only one byte, we are done..  how can that be?
		if ( size == 1 ) return false;
		//if ( ! utf8IsSane ( p[0] ) ) return false;
		// successive utf8 chars must have & 0xc0 be equal to 0x80
		// but the first char it must equal 0xc0, both set
		if ( (p[1] & 0xc0) != 0x80 ) return false;
		if ( size == 2 ) continue;
		if ( (p[2] & 0xc0) != 0x80 ) return false;
		if ( size == 3 ) continue;
		if ( (p[3] & 0xc0) != 0x80 ) return false;
	}
	if ( p != pend ) return false;
	return true;
}

bool verifyUtf8 ( char *txt ) {
	int32_t tlen = gbstrlen(txt);
	return verifyUtf8(txt,tlen);
}
