#ifndef UCPROPTABLE_H___
#define UCPROPTABLE_H___

#include <sys/types.h>
#include <stdlib.h> //NULL

class UCPropTable {
public:
	UCPropTable(unsigned char valueSize = 1, 
		    unsigned char tableBits = 16) ;

	virtual ~UCPropTable() ;
	void reset();

	//void *getValue(unsigned long c);
	void *getValue(unsigned long c){
		unsigned long prefix = c >> m_tableBits;
		unsigned long key = c & m_tableMask;
		if (prefix >= m_numTables) return NULL;
		if (m_data[prefix] == NULL) return NULL;
		return (void*) (m_data[prefix] + key*m_valueSize);
	};

	bool setValue(unsigned long c, void *value);
	
	size_t getSize() {return getStoredSize() + m_numTables*sizeof(char*);};
	size_t getStoredSize() ;
	size_t serialize(char *buf, size_t bufSize);
	size_t deserialize(char *buf, size_t bufSize);

private:
	unsigned char **m_data;

	unsigned char m_valueSize;
	unsigned char m_tableBits;
	unsigned long m_tableSize;
	unsigned long m_tableMask;
	unsigned long m_numTables;
};
#endif
