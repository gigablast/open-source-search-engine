// Matt Wells, copyright May 2003

// convert the old 1.0 index format to the new 2.0 compressed index format

#include "BigFile.h"
#include "RdbMap.h"
#include "Mem.h"

bool mainShutdown ( bool urgent ) { return true; }

int main ( int argc , char *argv[] ) {

	// must have big filename
	if ( argc != 3 ) {
		printf("Usage: convert [dir] [N]\n");
		printf("       Converts [dir]/indexdb000N.dat from 1.0 "
		       "to 2.0 format.\n");
		printf("       Stores new index in [dir]/newindex\n");
		exit(-1);
	}

	// point to old dir
	char *olddir = argv[1];

	// store the index index in this directory
	char newdir[256];
	sprintf ( newdir , "%s/newindex/", olddir );

	// index filename
	char iname [ 256 ];
	sprintf ( iname , "indexdb%04i.dat", atoi(argv[2]) );

	// big filename is provided
	BigFile f1;
	f1.set ( olddir , iname );
	// get the file size
	int64_t fsize = f1.getFileSize();
	// open it
	if ( ! f1.open ( O_RDONLY ) ) {
		printf("convert: error opening %s/%s\n",olddir,iname);
		exit(-1);
	}

	// ...insert old commented out code at bottom here...

	BigFile f2;
	f2.set ( newdir , iname );
	// open it
	if ( ! f2.open ( O_RDWR | O_CREAT ) ) {
		printf("convert: error opening %s/%s\n",newdir,iname);
		exit(-1);
	}

	// RdbMap needs like 14 bytes per page
	int32_t maxmem = (int32_t) ((fsize / PAGE_SIZE) * 14LL + 5000000LL);
	g_mem.init ( maxmem ); 

	fprintf(stderr,"convert: converting %s/%s to %s/%s (maxmem=%"INT32")\n",
		olddir,iname,newdir,iname,maxmem);
	
	// always make the hash table
	if ( ! hashinit() ) {
		log("






























convert: hashinit failed" ); return 1; }

	// read in 12 byte key, 4 byte size then data of that size
	int64_t offset  = 0;
	int64_t woffset = 0;
	unsigned char ophi[6];
	memset ( ophi , 0 , 6 );

	// read into this buf
	unsigned char buf  [ 32000*sizeof(key_t) ];
	// output into this one
	unsigned char obuf [ 32000*sizeof(key_t) ];

	// trash, at least as big as a truncated termlist, plus a lil more
	// for error over truncation
	unsigned char *trash = (unsigned char *)malloc (220000*sizeof(key_t));
	if ( ! trash ) {
		fprintf(stderr,"malloc failed\n");
		exit(-1);
	}
	unsigned char *trashEnd = trash + 220000*sizeof(key_t) ;
	unsigned char *trashTop = trash;
	unsigned char *trashBot = trash;

	int32_t trashed = 0;
	int32_t recycled = 0;
	int32_t removed = 0;

	RdbMap map;
	char mname[256];
	sprintf (mname ,"%s/indexdb%04i.map",newdir,atoi(argv[2]));
	map.set (mname, 0);

 loop:
	int32_t toRead = sizeof(key_t) * 32000;
	// truncate by file size
	if ( offset + toRead > fsize ) toRead = fsize - offset;

	// . begin the loop
	// . read up 6 bytes in case we start on half key
	if ( ! f1.read ( buf , toRead , offset ) ) {
		printf("convert: error reading file\n");
		exit(-1);
	}

	// input buffer ptrs
	unsigned char *p    ;
	unsigned char *pend = buf + toRead;

	// quickly check for keys out of order... that is disk corruption
 again:
	for ( p = buf + 12 ; p < pend ; p += 12 ) {
		// skip if order is ok
		if ( *(key_t *)(p-12) <= *(key_t *)(p) ) continue;
		// remove with radius of 2 keys
		unsigned char *a = p - 2*12;
		unsigned char *b = p + 2*12;
		if ( a < buf  ) a = buf;
		if ( b > pend ) b = pend;
		memmove ( a , b , pend - b );
		pend -= (b - a);
		// count it
		removed += (b-a)/12;
		//fprintf(stderr,"removed %"INT32" bad keys\n",(int32_t)(b-a)/12);
		goto again;
	}
	// reset p
	p = buf;

	// output buffer ptr
	unsigned char *op = obuf;

	// save hi key if any
	char savedhi[6];
	gbmemcpy ( savedhi , ophi , 6 );

	for ( ; p < pend ; p += 12 ) {
		// shift the lower 5 bytes up 1 bit to cover the old
		// punish bit (the bit after the 8 score bits)

		// if his punish bit is clear, fix it and store in trash
		if ( (p[4] & 0x80) == 0 ) {

			// if a corrupt key gets thrown in the trash
			// he might act as a damn and never let anyone out!!
			


			// turn on his punish bit --> fixing him
			p[4] |= 0x80; 
			// store in trash
			gbmemcpy ( trashTop , p , 12 );
			trashTop += 12;
			// count it
			trashed++;
			// wrap around if we need too
			if ( trashTop >= trashEnd ) trashTop = trash;
			// might be full
			if ( trashTop == trashBot ) {
				//fprintf(stderr,"TRASH FULL!\n");
				//fprintf(stderr,"skip n1=%08"XINT32" n0=%016"XINT64"\n",
				//	*(int32_t *)(p+8),*(int64_t *)p);
				trashTop -= 12;
				if ( trashTop < trash )
					trashTop = trashEnd - 12;
			}
			continue;
		}

		// save p
		unsigned char *psaved = p;

		// is key @ p BIGGER than key in trash?
		if ( trashBot != trashTop &&
		     *(key_t *)p >= *(key_t *)trashBot ) {
			// skip dups -- dedup
			// i think the ad collection will use this one
			if ( *(key_t *)p == *(key_t *)trashBot ) 
				continue;
			// temporarily assign to us
			p = trashBot;
			// advance trash
			trashBot += 12;
			// count it
			recycled++;
			// wrap it
			if ( trashBot >= trashEnd ) trashBot = trash;
		}
			

		// shift & carry
		p[4] <<= 1; if ( p[3] & 0x80 ) p[4] |= 0x01;
		// shift & carry
		p[3] <<= 1; if ( p[2] & 0x80 ) p[3] |= 0x01;
		// shift & carry
		p[2] <<= 1; if ( p[1] & 0x80 ) p[2] |= 0x01;
		// shift & carry
		p[1] <<= 1; if ( p[0] & 0x80 ) p[1] |= 0x01;
		// just shift
		p[0] <<= 1;
		// what was his delbit?
		if ( p[0] & 0x02 ) p[0] |= 0x01;
		else               p[0] &= 0xfe;

		// . add to new file
		// . should we only add low 6 bytes?
		if ( *(int32_t  *) ophi    == *(int32_t  *)(p+6 ) &&
		     *(int16_t *)(ophi+4) == *(int16_t *)(p+10)  ) {
			// set half bit
			p[0] |= 0x02;
			// store low 6 bytes
			*(int32_t  *) op    = *(int32_t  *) p;
			*(int16_t *)(op+4) = *(int16_t *)(p+4);
			op += 6;
		}
		else {
			// clear half bit
			p[0] &= 0xfd;
			// store low 6 bytes
			*(int32_t *) op    = *(int32_t *)p;
			*(int32_t *)(op+4) = *(int32_t *)(p+4);
			*(int32_t *)(op+8) = *(int32_t *)(p+8);
			// if we're the first 12, save hi 6 bytes
			*(int32_t  *) ophi    = *(int32_t  *)(p+6);
			*(int16_t *)(ophi+4) = *(int16_t *)(p+10);
			//ophi = op + 6;
			op += 12;
		}

		// if p referenced trash, go back!
		p = psaved;

	}
	// dump it out
	if ( ! f2.write ( obuf , op - obuf , woffset ) ) {
		log("convert: write failed");
		return -1;
	}

	// add to map
	key_t startKey ;
	key_t endKey   ;
	startKey.n0 = 0LL;
	startKey.n1 = 0;
	endKey.n0   = 0xffffffffffffffffLL;
	endKey.n1   = 0xffffffff;
	RdbList list;
	list.set ( (char *)obuf ,
		   op - obuf ,
		   (char *)obuf ,
		   op - obuf ,
		   startKey  ,
		   endKey    ,
		   0         ,
		   false     ,
		   true      );
	// HACK: if first key is only 6 bytes, set hi ptr to top 6
	if ( (*obuf & 0x02) == 0x02 )
		list.setListPtrs ( (char *)obuf , (char *)&savedhi );
	map.addList ( &list );
		   
	// advance write offset
	woffset += op - obuf ;
	// advance read offset
	offset += toRead;
	// bail if we need to
	if ( offset < fsize ) goto loop;

	map.close();
	f2.close();
	f1.close();

	// print final message
	fprintf(stderr,"convert: done converting %s/%s\n",olddir,iname);
	fprintf(stderr,"convert: recycled %"INT32" of the %"INT32" trashed\n",
		recycled,trashed);
	fprintf(stderr,"convert: removed %"INT32" bad keys\n",removed);
}

/*
	// init our new indexdb
	Rdb newdb;
	if ( ! newdb.init ( newdir       ,
			    "indexdb"    ,
			    true         ,  // dedup?
			    0            ,  // fixed rec size
			    minToMerge   ,  // min files to merge
			    maxTreeMem   ,
			    maxTreeNodes ,
			    true         ,  // balance tree?
			    0            ,  // max cache mem
			    0            ,  // max cache nodes
			    true         )){// use half keys?
		log("convert: rdb init failed");
		return -1;
	}

	// read in 12 byte key, 4 byte size then data of that size
	int64_t offset = 0;

	// start and end keys
	key_t startKey ;
	key_t endKey   ;
	startKey.n0 = 0LL;
	startKey.n1 = 0;
	endKey.n0   = 0xffffffffffffffffLL;
	endKey.n1   = 0xffffffff;

 loop:
	int32_t toRead = sizeof(key_t) * 32000;
	// truncate by file size
	if ( offset + toRead > fsize ) toRead = fsize - offset;

	// read into this buf
	char buf [ 32000*sizeof(key_t) ];

	// . begin the loop
	// . read up 6 bytes in case we start on half key
	if ( ! f.read ( buf , toRead , offset ) ) {
		printf("dump: error reading file\n");
		exit(-1);
	}

	// use this just for parsing
	IndexList ilist;

	char *p = buf;
	char *pend = buf + toRead;

	for ( ; p < pend ; p += 12 ) {
		// it is same format as new format, but no punish bit
		// after the score... that is now half bit and moved down, too
		// extract the score from key, it is complemented in old ones
		char score = 255 - ((unsigned char)p[5]);
		// extract termid from the key
		// get the upper 32 bits of the termId
		int64_t termId = ilist.getTermId12 ( p );
		// remove date codes for now
		//if ( termId == 0xdadadadaLL || 
		//     termId == 0xdadadad2LL  ) continue;
		// extract docid from the old format key
		uint64_t docId = *(int64_t *)p;
		docId >>= 1;
		docId &= DOCID_MASK;
		// was it a delete key?
		bool isDelKey ;
		if ( (*p & 0x01) == 0x00 ) isDelKey = true;
		else                       isDelKey = false;
		// make it all into a new indexdb key
		key_t k = g_indexdb.makeKey ( termId   , 
					      score    ,
					      docId    ,
					      isDelKey );
		// add new key to new indexdb
		// from Msg1.cpp:55
		uint32_t groupId = k.n1 & g_conf.m_groupMask;
		// bitch if it's not us!
		if ( groupId != g_conf.m_groupId ) {
			log("convert: termId would escape!");
			sleep(50000);
		}
		// do a non-blocking dump of tree if it's 90% full now
		if ( newdb.m_mem.is90PercentFull() || 
		     newdb.m_tree.is90PercentFull() ) {
			if ( ! newdb.dumpTree ( 0 ) ) { // niceness
				log("convert: dumpTree failed" );
				sleep(50000);
			}
		}
		// now add it
		if ( newdb.addRecord ( k , NULL , 0 ) < 0 ) {
			log("convert: addRecord failed" ); 
			sleep(50000);
		}
	}
	// advance offset
	offset += toRead;
	// bail if we need to
	if ( offset < fsize ) goto loop;
	// done
	// force dump of tree
        if ( ! newdb.dumpTree ( 0 ) ) { // niceness
		log("convert: final tree dump failed" );
		sleep(50000);
	}
	// print final message
	log("convert: done converting %s/%s",olddir,iname);
}
*/
