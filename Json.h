#ifndef JSON_H
#define JSON_H

#define JT_NULL 2
#define JT_NUMBER 3
#define JT_STRING 4
#define JT_ARRAY 5
#define JT_OBJECT 6
	
//#define JT_IsReference 256

#include "gb-include.h"
#include "Unicode.h"
#include "SafeBuf.h"

#define MAXJSONPARENTS 64

class JsonItem {

 public:
	// scan the linked list
	class JsonItem *m_next,*m_prev;
	class JsonItem *m_parent;//child;

	// the JT_* values above
	int m_type;

	// . the NAME of the item
	// . points into the ORIGINAL json string
	char *m_name;
	long m_nameLen;

	// for JT_NUMBER
	long m_valueLong;
	// for JT_NUMBER
	double m_valueDouble;

	// for JT_String
	long m_valueLen;

	// for JT_String
	char *getValue () { 
		// if value is another json object, then return NULL
		// must be string
		if ( m_type != JT_STRING ) return NULL;
		// otherwie return the string which is stored decoded
		// after this object in the same buffer
		return (char *)this + sizeof(JsonItem);
	};

};


class Json {
 public:

	void test();

	JsonItem *parseJsonStringIntoJsonItems ( char *json );

	JsonItem *getFirstItem ( ) ;

	JsonItem *getItem ( char *name );

	JsonItem *addNewItem ();

	Json() { m_stackPtr = 0; };
	
	SafeBuf m_sb;
	JsonItem *m_stack[MAXJSONPARENTS];
	long m_stackPtr;
	class JsonItem *m_prev;
};

#endif
