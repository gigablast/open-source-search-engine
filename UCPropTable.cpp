#include "gb-include.h"

#include "Mem.h"
#include "UCPropTable.h"

UCPropTable::UCPropTable(u_char valueSize, 
			 u_char tableBits) {
	m_valueSize = valueSize;
	m_tableBits = tableBits;
	m_numTables = 0xF0000 >> m_tableBits;
	m_tableSize = (1 << m_tableBits) * m_valueSize;
	m_tableMask = (1 << m_tableBits) - 1;
	m_data = NULL;
}

UCPropTable::~UCPropTable() {
	reset();
}

void UCPropTable::reset() {
	if (m_data) {
		for (u_long i=0;i<m_numTables;i++) {
			if (m_data[i])
				mfree(m_data[i], m_tableSize , "UCPropTable");
		}
		mfree(m_data, m_numTables*sizeof(u_char**),
			"UCPropTable");
		m_data = NULL;
	}	
}

/*
void *UCPropTable::getValue(unsigned long c){
	unsigned long prefix = c >> m_tableBits;
	unsigned long key = c & m_tableMask;
	if (prefix >= m_numTables) return NULL;
	if (m_data[prefix] == NULL) return NULL;
	return (void*) (m_data[prefix] + key*m_valueSize);
}
*/

bool UCPropTable::setValue(u_long c, void* value) {
	u_long prefix = c >> m_tableBits;
	unsigned short key = c & m_tableMask;
	if (prefix >= m_numTables) return false; // invalid plane
	if (m_data == NULL){
		m_data = (u_char**)
			mmalloc(m_numTables * sizeof(u_char*), 
				"UCPropTable");
		if (m_data == NULL) 
			return log(LOG_WARN, "UCPropTable: out of memory");
		memset(m_data, '\0', m_numTables*sizeof(u_char**));
	}
	if (m_data[prefix] == NULL){
		m_data[prefix] = (u_char*) 
			mmalloc(m_tableSize, "UCPropTable");
		if (m_data[prefix] == NULL) 
			return log(LOG_WARN, "UCPropTable: out of memory");
		
		memset(m_data[prefix], '\0', m_tableSize);
	}
	memcpy(m_data[prefix] +key*m_valueSize, value, m_valueSize);
	return true;
	
}

size_t UCPropTable::getStoredSize() {
	// Header
	u_long size = sizeof(u_long) // record size
		+ sizeof(u_char) // value size
		+ sizeof(u_char);  // number of table bits

	if (m_data)
		for (u_long i=0 ; i < m_numTables ; i++) {
			if (m_data[i]) {
				size += sizeof(long) + // table #
					m_tableSize;
				
			}
		}
	size += sizeof (u_long);
	return size;
}
#define RECORD_END (u_long)0xdeadbeef

size_t UCPropTable::serialize(char *buf, size_t bufSize) {
	u_long size = getStoredSize();
	if (bufSize < size) return 0;
	char *p = buf;
	// Header
	*(u_long*)p = size; p += sizeof(u_long);
	*(u_char*)p = m_valueSize; p += sizeof(u_char);
	*(u_char*)p = m_tableBits; p += sizeof(u_char);
	if (m_data)
		for (u_long i=0; i<m_numTables ; i++) {
			if (m_data[i]) {
				*(u_long*)p = i; p += sizeof(u_long);
				memcpy(p, m_data[i], m_tableSize);
				p += m_tableSize;
			}
		}
	*(u_long*)p = RECORD_END; p += sizeof(u_long);
	// sanity check
	if (p != buf + size)
		return log(LOG_WARN,
			   "UCPropTable: size mismatch: expected %ld bytes, "
			   "but wrote %d instead", size, p-buf);
	return p-buf;
}

size_t UCPropTable::deserialize(char *buf, size_t bufSize) {
	reset();
	char *p = buf;
	u_long size = *(u_long*)p; p+=sizeof(u_long);
	//printf("Expecting %d bytes (buffer size: %d)\n", size, bufSize);
	if (bufSize < size) return 0;

	m_valueSize = *(u_char*)p++;
	m_tableBits = *(u_char*)p++;
	//printf ("Read %d bytes after header\n", p-buf);
	m_tableSize = (1 << m_tableBits) * m_valueSize;
	m_tableMask = (1 << m_tableBits) - 1;

	m_numTables = 0xF0000 >> m_tableBits;
	// allocate main table
	m_data = (u_char**)
		mmalloc(m_numTables * sizeof(u_char*), 
			"UCPropTable");
	if (m_data == NULL) 
		return log(LOG_WARN, "UCPropTable: out of memory");
	memset(m_data, '\0', m_numTables*sizeof(u_char**));
	
	//load tables
	while (p < buf+size) {
		u_long prefix = *(u_long*)p; p += sizeof(u_long);
		if ( prefix == RECORD_END ){
			if (p != buf+size )
				return log(LOG_WARN, 
					   "UCPropTable: "
					   "unexpected end of record");
			//printf ("Read %d bytes after footer\n", p-buf);
			return size;

		}
		m_data[prefix] = (u_char*) 
			mmalloc(m_tableSize, "UCPropTable");
		if (m_data[prefix] == NULL) 
			return log(LOG_WARN, "UCPropTable: out of memory");
		memcpy(m_data[prefix], p, m_tableSize); p += m_tableSize;
		//printf ("Read %d bytes after table %d\n", p-buf, prefix);
	}
	// shouldn't get here
	log("UCPropTable: read %d too many bytes\n", p-(buf+size));
	return 0;
}
