#include "gb-include.h"

#include "Msg1f.h"

static void handleRequest ( UdpSlot *slot , long netnice );
#define  LOG_WINDOW 2048



bool Msg1f::init() {
        if ( ! g_udpServer.registerHandler ( 0x1f, handleRequest )) {
		log(LOG_WARN, "db: logview initialization failed"); 
		return false;
	}
	return true;
}


bool Msg1f::getLog(long hostId, 
		   long numBytes, 
		   void *callbackState, 
		   void ( *callback) (void *state, UdpSlot* slot)) {
	
	char* sendBuf = (char*)mmalloc(sizeof(long), "Msg1fA");
	char* p = sendBuf;
	*(long*)p = numBytes;
	p += sizeof(long);
	UdpSlot *slot;
	g_udpServer.sendRequest(sendBuf,
				p - sendBuf,
				0x1f,
				g_hostdb.m_hostPtrs[hostId]->m_ip,
				g_hostdb.m_hostPtrs[hostId]->m_port,
				g_hostdb.m_hostPtrs[hostId]->m_hostId,
				&slot,
				callbackState,
				callback,
				5);

	return false;
}


void handleRequest ( UdpSlot *slot , long netnice ) {
	char *p = slot->m_readBuf;

	long numBytes = *(long*)p;
	p += sizeof(long);


	char *filename = g_hostdb.m_logFilename;
	long fd = open ( filename , O_RDONLY,
			 S_IRUSR |S_IWUSR |S_IRGRP |S_IWGRP| S_IROTH );
	if ( ! fd ) {
		log(LOG_DEBUG, "logviewer: Failed to open %s for reading: ",
		    filename);
		g_errno = EBADFILE;
		g_udpServer.sendErrorReply ( slot, g_errno ); 
		return;
	}

	char stackSpace[LOG_WINDOW];
	char *buf = stackSpace;
	char *allocBuf = NULL;
	long allocBufSize = 0;

	if(numBytes > LOG_WINDOW) {
		buf = (char*)mmalloc(numBytes, "Msg1fA");
		if(!buf) {
			log(LOG_INFO, 
			    "admin: malloc of %li bytes failed "
			    "for logview,"
			    " falling back on stack buffer.",
			    numBytes);
			buf = stackSpace;
			numBytes = LOG_WINDOW;
		}
		else {
			allocBuf     = buf;
			allocBufSize = numBytes;
		}
	}

	lseek(fd, -1 * numBytes, SEEK_END);
	if(errno == EINVAL) {
		//oops! we seeked to before the begining of the file
		//log(LOG_WARN, "bad seek!");
		lseek(fd, 0, SEEK_SET);
	}

	long numRead = read(fd, buf, numBytes-1);
	close(fd);
	if(numRead > 0)	buf[numRead-1] = '\0';
	else          {  
		buf[0] = '\0'; 
		numRead = 0;
		if(allocBuf) mfree(allocBuf,	allocBufSize, "Msg1fA");
		allocBufSize = 0;
		allocBuf = NULL;
		g_udpServer.sendErrorReply ( slot, EBADFILE ); 
		return;
	}
	//log(LOG_DEBUG, "bytes read! %li ", numRead);

	g_udpServer.sendReply_ass (buf, numRead, allocBuf,allocBufSize, slot); //send
}
