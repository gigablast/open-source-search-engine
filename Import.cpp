//////
//
// BEGIN IMPORT TITLEDB FUNCTIONS
//
//////

// . injecting titledb files from other gb clusters into your collection
// . select the 'import' tab in the admin gui and enter the directory of
//   the titledb files you want to import/inject.
// . it will scan that directory for all titledb files.
// . you can also set max simultaneous injections. set to auto so it
//   will do 10 per host, up to like 100 max.

#define MAXINJECTSOUT 100

class ImportState {

public:

	// available msg7s to use
	Msg7 **m_ptrs;
	long   m_numPtrs;

	// collection we are importing INTO
	collnum_t collnum;

	long long m_numIn;
	long long m_numOut;

	ImportState() ;
	ImportState~() { reset(); }

	void reset();
};

ImportState::ImportState () {
	m_numIn = 0 ; 
	m_numOut = 0; 
	m_ptrs = NULL; 
	m_numPtrs=0;
}

ImportState::reset() {
	for ( long i = 0 ; i < numPtrs ; i++ ) {
		Msg7 *msg7 = m_ptrs[i];
		if ( ! msg7 ) continue;
		msg7->reset();
		delete ( msg7 );
		mdelete ( msg7 );
		m_ptrs[i] = NULL;
	}
	mfree ( m_ptrs , MAXINJECTSOUT * sizeof(Msg7 *) , "ism7f" );
	m_ptrs = NULL;
	m_numPtrs = 0;
}



// . call this when gb startsup
// . scan collections to see if any imports were active
bool resumeImports ( ) {

	for ( long i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		CollectionRec *cr = g_collectiondb.m_recs[i];
		if ( ! cr ) continue;
		if ( ! cr->m_importEnabled ) continue;
		// each import has its own state
		ImportState *is;
		// and collnum
		is->m_collnum = cr->m_collnum;
		// resume the import
		is->importLoop ( );
	}
}

void gotMsg7ReplyWrapper ( void *state ) {
	Msg7 *msg7 = (Msg7 *)state;
	msg7->m_numIn++;

	log("tdbinject: injected %lli docs",m_numIn);

	// if we were the least far ahead of scanning the files
	// then save our position in case server crashes so we can
	// resume
	saveFileBookMark ( msg7 ) ;

	importLoop();
}

//
// . ENTRY POINT FOR IMPORTING TITLEDB RECS FROM ANOTHER CLUSTER
// . when user clicks 'begin' in import page we come here..
// . so when that parm changes in Parms.cpp we sense that and call
//   beginImport(CollectionRec *cr)
// . or on startup we call resumeImports to check each coll for 
//   an import in progress.
// . search for files named titledb*.dat
// . if none found just return
// . when msg7 inject competes it calls this
// . call this from sleep wrapper in Process.cpp
bool ImportState::importLoop ( ) {

	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );

	if ( ! cr ) { 
		// if coll was deleted!
		log("import: collnum %li deleted while importing into",
		    (long)m_collnum);
		//if ( m_numOut > m_numIn ) return true;
		// delete the import state i guess
		delete ( this );
		mdelete ( this );
		return true;
	}

 INJECTLOOP:

	// scan each titledb file scanning titledb0001.dat first,
	// titledb0003.dat second etc.

	long long offset = -1;
	// when offset is too big for current s_bigFile file then
	// we go to the next and set offset to 0.
	BigFile *bf = getCurrentTitleFileAndOffset ( &offset );

	// this is -1 if none remain!
	if ( offset == -1 ) return true;


	long need = 12;
	long dataSize = -1;
	
	// read in title rec key and data size
	long n = bf->read ( &tkey, 12 , s_fileOffset );
	
	if ( n != 12 ) goto nextFile;

	// if non-negative then read in size
	if ( tkey.n0 & 0x01 ) {
		n = bf->read ( &dataSize , 4 , s_fileOffset );
		if ( n != 4 ) goto nextFile;
		need += 4;
		need += dataSize;
		if ( dataSize < 0 || dataSize > 500000000 ) {
			log("main: could not scan in titledb rec of "
			    "corrupt dataSize of %li. BAILING ENTIRE "
			    "SCAN of file %s",s_titFilename);
			goto nextFile;
		}
	}

	// point to start of buf
	sbuf.reset();

	// ensure we have enough room
	sbuf.reserve ( need );

	// store title key
	sbuf.safeMemcpy ( &tkey , sizeof(key_t) );

	// then datasize if any. neg rec will have -1 datasize
	if ( dataSize >= 0 ) 
		sbuf.pushLong ( dataSize );

	// then read data rec itself into it, compressed titlerec part
	if ( dataSize > 0 ) {
		// read in the titlerec after the key/datasize
		n = bf->read ( sbuf.m_buf + sbuf.m_length ,
				dataSize ,
				s_fileOffset );
		if ( n != dataSize ) {
			log("main: failed to read in title rec "
			    "file. %li != %li. Skipping file %s",
			    n,dataSize,s_titFilename);
			goto nextFile;
		}
		// it's good, count it
		sbuf.m_length += n;
	}

	//XmlDoc *xd = getAvailXmlDoc();
	Msg7 *msg7 = getAvailMsg7();

	// if none, must have to wait for some to come back to us
	if ( ! msg7 ) return false;
	
	// set xmldoc from the title rec
	//xd->set ( sbuf.getBufStart() );
	//xd->m_masterState = NULL;
	//xd->m_masterCallback ( titledbInjectLoop );

	msg7->m_hackFileOff = s_fileOffset;
	msg7->m_hackFileId  = s_fileId;

	GigablastRequest *gr = &msg7->m_gr;
	// inject a title rec buf!!
	gr->m_titleRecBuf = &sbuf;

	//
	// point to next doc in the titledb file
	//
	s_fileOffset += need;



	msg7->m_numOut++;

	// then index it. master callback will be called
	//if ( ! xd->index() ) return false;
	// TODO: make this forward the request to an appropriate host!!
	if ( msg7->inject ( msg7 , // state
			    gotMsg7ReplyWrapper ) ) // callback
		// it didn't block somehow...
		m_numIn++;

	goto INJECTLOOP;

 nextFile:
	// invalidate this flag
	s_offIsValid = false;
	// and call this function. we add one to s_bfFileId so we
	// do not re-get the file we just injected.
	bf = getCurrentTitleFileAndOffset ( &offset , s_bfFileId+1 );

	// still going on? bf should be new and offset should be 0
	if ( bf )
		goto INJECTLOOP;

	// if it returns NULL we are done!
	log("main: titledb injection loop completed. waiting for "
	    "outstanding injects to return.");
		
	if ( msg7->m_numOut > msg7->m_numIn )
		return false;

	log("main: all injects have returned. DONE.");

	// dummy return
	return true;
}

// return NULL with g_errno set on error
Msg7 *ImportLoop::getAvailMsg7 ( ) {

	static XmlDoc **s_ptrs = NULL;

	if ( ! s_ptrs ) {
		s_ptrs = mmalloc ( sizeof(XmlDoc *) * MAXINJECTSOUT,"sxdp");
		if ( ! s_ptrs ) return NULL;
	}

	// respect the user limit for this coll
	long long out = s_numOut - s_numIn;
	if ( out >= cr->m_importInjects ) {
		g_errno = 0;
		return NULL;
	}

	// find one not in use and return it
	for ( long i = 0 ; i < (long)MAXINJECTSOUT ; i++ ) {
		// point to it
		XmlDoc *xd = s_ptrs[i];
		// if one is there already and not in use, recycle it
		if ( xd ) {
			if ( xd->m_docInUse ) continue;
			xd->m_docInUse = true;
			return true;
		}
		// otherwise, make a new one
		try { xd = new (XmlDoc); }
		catch ( ... ) { 
			g_errno = ENOMEM;
			log("PageInject: new(%i): %s", 
			    (int)sizeof(XmlDoc),mstrerror(g_errno));
			return NULL;
		}
		mnew ( xd, sizeof(XmlDoc) , "PageImport" );
		s_ptrs[i] = xd;
		xd->m_docInUse = true;
		return xd;
	}
	// none avail
	g_errno = 0;
	return NULL;
}


static long s_fileId = -1;
static long long s_fileOffset = 0LL;

static long s_bfFileId = -2;
static BigFile s_bf;

static bool s_offIsValid = false;

BigFile *getCurrentTitleFileAndOffset ( long long *off , long minFileId ) {

	
	if ( s_offIsValid ) {
		*off = s_fileOffset;
		return &s_bf; 
	}

	s_offIsValid = true;

	// look for titledb0001.dat etc. files in the 
	// workingDir/inject/ subdir
	SafeBuf ddd;
	ddd.safePrintf("%sinject",cr->m_importDir);
	// now use the one provided. we should also provide the # of threads
	if ( g_conf.m_importDir && g_conf.m_importDir[0] ) {
		ddd.reset();
		ddd.safeStrcpy ( g_conf.m_importDir );
	}

	//
	// assume we are the first filename
	// set s_fileId to the minimum
	//
	Dir dir;
	dir.set(ddd.getBufStart());
	// getNextFilename() writes into this
	char pattern[8]; strcpy ( pattern , "titledb*.dat*" );
	char *filename;
	while ( ( filename = dir.getNextFilename ( pattern ) ) ) {
		// filename must be a certain length
		long filenameLen = gbstrlen(filename);
		// we need at least "titledb0001.dat"
		if ( filenameLen < 15 ) continue;
		// ensure filename starts w/ our m_dbname
		if ( strncmp ( filename , "titledb", 7 ) != 0 )
			continue;
		// then a 4 digit number should follow
		char *s = filename + 7;
		if ( ! isdigit(*(s+0)) ) continue;
		if ( ! isdigit(*(s+1)) ) continue;
		if ( ! isdigit(*(s+2)) ) continue;
		if ( ! isdigit(*(s+3)) ) continue;
		// convert digit to id
		long id = atol(s);
		// do not accept files we've already processed
		if ( id < minFileId ) continue;
		// the min of those we haven't yet processed/injected
		if ( id < s_fileId ) s_fileId = id;
	}

	// get where we left off
	static bool s_loadedPlaceHolder = false;
	if ( ! s_loadedPlaceHolder ) {
		// read where we left off from file if possible
		char fname[256];
		sprintf(fname,"%slasttitledbinjectinfo.dat",g_hostdb.m_dir);
		SafeBuf ff;
		ff.fillFromFile(fname);
		if ( ff.length() < 1 ) goto noplaceholder;
		// get the placeholder
		sscanf ( ff.getBufStart() 
			 , "%llu,%lu"
			 , &s_fileOffset
			 , &s_fileId
			 );
	}

	// if no files!
	if ( s_fileId == -1 ) return NULL;

	// set up s_bf then
	if ( s_bfFileId != s_fileId ) {
		SafeBuf tmp;
		tmp.safePrintf("%sinject/titledb%04li.dat");
		s_bf.set ( tmp );
		s_bfFileId = s_fileId;
	}

	return &s_bf;

}

// "xd" is the XmlDoc thtat just completed injecting
void saveFileBookMark ( XmlDoc *xd ) {

	long fileId  = xd->m_hackFileId;
	long fileOff = xd->m_hackFileOff;

	// if there is one outstanding the preceeded us, we can't update
	// the bookmark just yet.
	for ( long i = 0 ; i < (long)MAXINJECTSOUT ; i++ ) {
		XmlDoc *od = &s_xmlDocPtr[i];
		if ( od == xd ) continue;
		if ( ! od->m_docInUse ) continue;
		if ( od->m_hackFileId < fileId ) return;
		if ( od->m_hackFileId == fileId &&
		     od->m_hackFileOff < fileOff ) return;
	}

	char fname[256];
	sprintf(fname,"%slasttitledbinjectinfo.dat",g_hostdb.m_dir);
	SafeBuf ff;
	ff.safePrintf("%llu,%lu",s_fileOffset,s_fileId);
	ff.save ( fname );
}
