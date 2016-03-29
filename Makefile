SHELL = /bin/bash

uname_m = $(shell uname -m)
ARCH=$(uname_m)

# for building packages
VERSION=20

CC=g++

# remove dlstubs.o for CYGWIN
OBJS =  UdpSlot.o Rebalance.o \
	Msg13.o Mime.o \
	PageGet.o PageHosts.o \
	PageParser.o PageInject.o PagePerf.o PageReindex.o PageResults.o \
	PageAddUrl.o PageRoot.o PageSockets.o PageStats.o \
	PageTitledb.o \
	PageAddColl.o \
	hash.o Domains.o \
	Collectiondb.o \
	linkspam.o ip.o sort.o \
	fctypes.o XmlNode.o XmlDoc.o Xml.o \
	Words.o Url.o UdpServer.o \
	Threads.o Titledb.o HashTable.o \
	TcpServer.o Summary.o \
	Spider.o \
	Catdb.o \
	RdbTree.o RdbScan.o RdbMerge.o RdbMap.o RdbMem.o RdbBuckets.o \
	RdbList.o RdbDump.o RdbCache.o Rdb.o RdbBase.o \
	Query.o Phrases.o Multicast.o Msg9b.o\
	Msg8b.o Msg5.o \
	Msg39.o Msg3.o \
	Msg22.o \
	Msg20.o Msg2.o \
	Msg1.o \
	Msg0.o Mem.o Matches.o Loop.o \
	Log.o Lang.o \
	Indexdb.o Posdb.o Clusterdb.o IndexList.o Revdb.o \
	HttpServer.o HttpRequest.o \
	HttpMime.o Hostdb.o \
	Highlight.o File.o Errno.o Entities.o \
	Dns.o Dir.o Conf.o Bits.o \
	Stats.o BigFile.o Msg17.o \
	Speller.o \
	PingServer.o StopWords.o TopTree.o \
	Parms.o Pages.o \
	Unicode.o iana_charset.o Iso8859.o \
	SearchInput.o \
	Categories.o Msg2a.o PageCatdb.o PageDirectory.o \
	SafeBuf.o Datedb.o \
	UCNormalizer.o UCPropTable.o UnicodeProperties.o \
	Pops.o Title.o Pos.o LangList.o \
	Profiler.o \
	AutoBan.o Msg3a.o HashTableT.o HashTableX.o \
	PageLogView.o Msg1f.o Blaster.o MsgC.o \
	PageSpam.o Proxy.o PageThreads.o Linkdb.o \
	matches2.o LanguageIdentifier.o \
	Language.o Repair.o Process.o \
	Abbreviations.o \
	RequestTable.o TuringTest.o Msg51.o geo_ip_table.o \
	Msg40.o Msg4.o SpiderProxy.o \
	LanguagePages.o \
	Statsdb.o PageStatsdb.o \
	PostQueryRerank.o Msge0.o Msge1.o \
	CountryCode.o DailyMerge.o CatRec.o Tagdb.o \
	Users.o Images.o Wiki.o Wiktionary.o Scraper.o \
	Dates.o Sections.o SiteGetter.o Syncdb.o qa.o \
	Placedb.o Address.o Test.o GeoIP.o GeoIPCity.o Synonyms.o \
	Cachedb.o Monitordb.o dlstubs.o PageCrawlBot.o Json.o PageBasic.o \
	Punycode.o Version.o

CHECKFORMATSTRING = -D_CHECK_FORMAT_STRING_

DEFS = -D_REENTRANT_ $(CHECKFORMATSTRING) -I.

HOST=$(shell hostname)

#print_vars:
#	$(HOST)

FFF = /etc/redhat-release

ifneq ($(wildcard $(FFF)),)
OS_RHEL := true
STATIC :=
XMLDOCOPT := -O2
else
OS_DEB := true
# let's remove static now by default to be safe because we don't always
# detect red hat installs like on aws. do 'make static' to make as static.
#STATIC := -static
STATIC :=
# MDW: i get some parsing inconsistencies when running the first qa injection
# test if this is -O3. strange.
# now debian jesse doesn't like -O3, it will core right away when spidering
# so change this to -O2 from -O3 as well.
XMLDOCOPT := -O2
endif


ifeq ("titan","$(HOST)")
# my machine, titan, runs the old 2.4 kernel, it does not use pthreads because
# they were very buggy in 1999. Plus they are still kind of slow even today,
# in 2013. So it just uses clone() and does its own "threading". Unfortunately,
# the way it works is not even possible on newer kernels because they no longer
# allow you to override the _errno_location() function. -- matt
# -DMATTWELLS
# turn off stack smash detection because it won't save and dump core when
# stack gets smashed like it normally would when it gets a seg fault signal.
CPPFLAGS = -m32 -g -Wall -pipe -Wno-write-strings -Wstrict-aliasing=0 -Wno-uninitialized $(STATIC) -DTITAN
LIBS = ./libz.a ./libssl.a ./libcrypto.a ./libiconv.a ./libm.a

# are we a 32-bit architecture? use different libraries then
else ifeq ($(ARCH), i686)
CPPFLAGS= -m32 -g -Wall -pipe -fno-stack-protector -Wno-write-strings -Wstrict-aliasing=0 -Wno-uninitialized -DPTHREADS -Wno-unused-but-set-variable $(STATIC)
#LIBS= -L. ./libz.a ./libssl.a ./libcrypto.a ./libiconv.a ./libm.a ./libstdc++.a -lpthread
LIBS=  -lm -lpthread -lssl -lcrypto ./libiconv.a ./libz.a

else ifeq ($(ARCH), i386)
CPPFLAGS= -m32 -g -Wall -pipe -fno-stack-protector -Wno-write-strings -Wstrict-aliasing=0 -Wno-uninitialized -DPTHREADS -Wno-unused-but-set-variable $(STATIC)
#LIBS= -L. ./libz.a ./libssl.a ./libcrypto.a ./libiconv.a ./libm.a ./libstdc++.a -lpthread
LIBS=  -lm -lpthread -lssl -lcrypto ./libiconv.a ./libz.a

else
#
# Use -Wpadded flag to indicate padded structures.
#
CPPFLAGS = -g -Wall -pipe -fno-stack-protector -Wno-write-strings -Wstrict-aliasing=0 -Wno-uninitialized -DPTHREADS -Wno-unused-but-set-variable $(STATIC)
#LIBS= -L. ./libz.a ./libssl.a ./libcrypto.a ./libiconv.a ./libm.a ./libstdc++.a -lpthread
# apt-get install libssl-dev (to provide libssl and libcrypto)
# to build static libiconv.a do a './configure --enable$(STATIC)' then 'make'
# in the iconv directory
LIBS=  -lm -lpthread -lssl -lcrypto ./libiconv64.a ./libz64.a

endif

# if you have seo.cpp link that in. This is not part of the open source
# distribution but is available for interested parties.
ifneq ($(wildcard seo.cpp),) 
OBJS:=$(OBJS) seo.o
endif



# let's keep the libraries in the repo for easier bug reporting and debugging
# in general if we can. the includes are still in /usr/include/ however...
# which is kinda strange but seems to work so far.
#LIBS= -L. ./libz.a ./libssl.a ./libcrypto.a ./libiconv.a ./libm.a ./libgcc.a ./libpthread.a ./libc.a ./libstdc++.a 



#SRCS := $(OBJS:.o=.cpp) main.cpp


all: gb

#g8: gb
#	scp gb g8:/p/gb.new
#	ssh g8 'cd /p/ ; ./gb stop ; ./gb installgb ; sleep 4 ; ./gb start'

utils: addtest blaster2 dump hashtest makeclusterdb makespiderdb membustest monitor seektest urlinfo treetest dnstest dmozparse gbtitletest

# version:
# 	echo -n "#define GBVERSION \"" > vvv
# 	#date --utc >> vvv
# 	date +%Y.%M.%d-%T-%Z >> vvv
# 	head -c -1 vvv > Version.h
# 	echo "\"" >> Version.h
# 	#for Version.h dependency
# 	#rm main.o
# 	#rm PingServer.o

vclean:
	rm -f Version.o
	@echo ""
	@echo "*****"
	@echo ""
	@echo "If make fails on Ubuntu then first run:"
	@echo ""
	@echo "sudo apt-get update ; sudo apt-get install make g++ libssl-dev"
	@echo ""
	@echo ""
	@echo "If make fails on RedHat then first run:"
	@echo ""
	@echo "sudo yum install gcc-c++"
	@echo ""
	@echo ""
	@echo "If make fails on CentOS then first run:"
	@echo ""
	@echo "sudo yum install gcc-c++ openssl-devel"
	@echo ""
	@echo "*****"
	@echo ""

gb: vclean $(OBJS) main.o $(LIBFILES)
	$(CC) $(DEFS) $(CPPFLAGS) -o $@ main.o $(OBJS) $(LIBS)

static: vclean $(OBJS) main.o $(LIBFILES)
	$(CC) $(DEFS) $(CPPFLAGS) -static -o gb main.o $(OBJS) $(LIBS)


# use this for compiling on CYGWIN: 
# only for 32bit cygwin right now and
# you have to install the packages that have these libs.
# you have to get these packages from cygwin:
# 1. LIBS  > zlib-devel: Gzip de/compression library (development)
# 2. LIBS  > libiconv: GNU character set conversion library and utlities

# 3. DEVEL > openssl: cygwin32-openssl: OpenSSL for Cygwin 32bit toolchain

# 3. NET   > openssl: A general purpose cryptographt toolkit with TLS impl...

# 4. DEVEL > mingw-pthreads: Libpthread for MinGW.org Wind32 toolchain
# 5. DEVEL > gcc-g++: GNU Compiler Collection (C++)
# 6. DEVEL > make: The GNU version of the 'make' utility
# 7. DEVEL > git: Distributed version control system
# 8. EDITORS > emacs
cygwin:
	make DEFS="-DCYGWIN -D_REENTRANT_ $(CHECKFORMATSTRING) -I." LIBS=" -lz -lm -lpthread -lssl -lcrypto -liconv" gb



gb32:
	make CPPFLAGS="-m32 -g -Wall -pipe -fno-stack-protector -Wno-write-strings -Wstrict-aliasing=0 -Wno-uninitialized -DPTHREADS -Wno-unused-but-set-variable $(STATIC)" LIBS=" -L. ./libz.a ./libssl.a ./libcrypto.a ./libiconv.a ./libm.a ./libstdc++.a -lpthread " gb

#iana_charset.cpp: parse_iana_charsets.pl character-sets supported_charsets.txt
#	./parse_iana_charsets.pl < character-sets

#iana_charset.h: parse_iana_charsets.pl character-sets supported_charsets.txt
#	./parse_iana_charsets.pl < character-sets

run_parser: test_parser
	./test_parser ~/turkish.html

test_parser: $(OBJS) test_parser.o Makefile 
	g++ $(DEFS) $(CPPFLAGS) -o $@ test_parser.o $(OBJS) $(LIBS)
test_parser2: $(OBJS) test_parser2.o Makefile 
	g++ $(DEFS) $(CPPFLAGS) -o $@ test_parser2.o $(OBJS) $(LIBS)

test_hash: test_hash.o $(OBJS)
	g++ $(DEFS) $(CPPFLAGS) -o $@ test_hash.o $(OBJS) $(LIBS)
test_norm: $(OBJS) test_norm.o
	g++ $(DEFS) $(CPPFLAGS) -o $@ test_norm.o $(OBJS) $(LIBS)
test_convert: $(OBJS) test_convert.o
	g++ $(DEFS) $(CPPFLAGS) -o $@ test_convert.o $(OBJS) $(LIBS)

supported_charsets: $(OBJS) supported_charsets.o supported_charsets.txt
	g++ $(DEFS) $(CPPFLAGS) -o $@ supported_charsets.o $(OBJS) $(LIBS)
gbchksum: gbchksum.o
	g++ -g -Wall -o $@ gbchksum.o
create_ucd_tables: $(OBJS) create_ucd_tables.o
	g++ $(DEFS) $(CPPFLAGS) -o $@ create_ucd_tables.o $(OBJS) $(LIBS)

ucd.o: ucd.cpp ucd.h

ucd.cpp: parse_ucd.pl
	./parse_ucd.pl UNIDATA/UnicodeData.txt ucd


ipconfig: ipconfig.o
	$(CC) $(DEFS) $(CPPFLAGS) -o $@ $@.o -lc
blaster2: $(OBJS) blaster2.o
	$(CC) $(DEFS) $(CPPFLAGS) -o $@ $@.o $(OBJS) $(LIBS)
udptest: $(OBJS) udptest.o
	$(CC) $(DEFS) $(CPPFLAGS) -o $@ $@.o $(OBJS) $(LIBS)
dnstest: $(OBJS) dnstest.o
	$(CC) $(DEFS) $(CPPFLAGS) -o $@ $@.o $(OBJS) $(LIBS)
thunder: thunder.o
	$(CC) $(DEFS) $(CPPFLAGS) $(STATIC) -o $@ $@.o
threadtest: threadtest.o
	$(CC) $(DEFS) $(CPPFLAGS) -o $@ $@.o -lpthread
memtest: memtest.o
	$(CC) $(DEFS) $(CPPFLAGS) -o $@ $@.o 
hashtest: hashtest.cpp
	$(CC) -O3 -o hashtest hashtest.cpp
hashtest0: hashtest
	scp hashtest gb0:/a/
membustest: membustest.cpp
	$(CC) -O0 -o membustest membustest.cpp $(STATIC) -lc
mergetest: $(OBJS) mergetest.o
	$(CC) $(DEFS) $(CPPFLAGS) -o $@ $@.o $(OBJS) $(LIBS)
addtest: $(OBJS) addtest.o
	$(CC) $(DEFS) $(CPPFLAGS) -o $@ $@.o $(OBJS) $(LIBS)
addtest0: $(OBJS) addtest
	bzip2 -fk addtest
	scp addtest.bz2 gb0:/a/
seektest: seektest.cpp
	$(CC) -o seektest seektest.cpp -lpthread
treetest: $(OBJ) treetest.o
	$(CC) $(DEFS) -O2 $(CPPFLAGS) -o $@ $@.o $(OBJS) $(LIBS)
treetest0: treetest
	bzip2 -fk treetest
	scp treetest.bz2 gb0:/a/
	ssh gb0 'cd /a/ ; rm treetest ; bunzip2 treetest.bz2'
nicetest: nicetest.o
	$(CC) -o nicetest nicetest.cpp


monitor: $(OBJS) monitor.o
	$(CC) $(DEFS) $(CPPFLAGS) -o $@ monitor.o $(OBJS) $(LIBS)
reindex: $(OBJS) reindex.o
	$(CC) $(DEFS) $(CPPFLAGS) -o $@ $@.o $(OBJS) $(LIBS)
convert: $(OBJS) convert.o
	$(CC) $(DEFS) $(CPPFLAGS) -o $@ $@.o $(OBJS) $(LIBS)
maketestindex: $(OBJS) maketestindex.o
	$(CC) $(DEFS) $(CPPFLAGS) -o $@ $@.o $(OBJS) $(LIBS)
makespiderdb: $(OBJS) makespiderdb.o
	$(CC) $(DEFS) $(CPPFLAGS) -o $@ $@.o $(OBJS) $(LIBS)
makespiderdb0: makespiderdb
	bzip2 -fk makespiderdb
	scp makespiderdb.bz2 gb0:/a/
makeclusterdb: $(OBJS) makeclusterdb.o
	$(CC) $(DEFS) $(CPPFLAGS) -o $@ $@.o $(OBJS) $(LIBS)
makeclusterdb0: makeclusterdb
	bzip2 -fk makeclusterdb
	scp makeclusterdb.bz2 gb0:/a/
	ssh gb0 'cd /a/ ; rm makeclusterdb ; bunzip2 makeclusterdb.bz2'
makefix: $(OBJS) makefix.o
	$(CC) $(DEFS) $(CPPFLAGS) -o $@ $@.o $(OBJS) $(LIBS)
makefix0: makefix
	bzip2 -fk makefix
	scp makefix.bz2 gb0:/a/
urlinfo: $(OBJS) urlinfo.o
	$(CC) $(DEFS) $(CPPFLAGS) -o $@ $(OBJS) urlinfo.o $(LIBS)

dmozparse: $(OBJS) dmozparse.o
	$(CC) $(DEFS) $(CPPFLAGS) -o $@ $@.o $(OBJS) $(LIBS)
gbfilter: gbfilter.cpp
	g++ -g -o gbfilter gbfilter.cpp $(STATIC) -lc
gbtitletest: gbtitletest.o
	$(CC) $(DEFS) $(CPPFLAGS) -o $@ $@.o $(OBJS) $(LIBS)


# comment this out for faster deb package building
clean:
	-rm -f *.o gb *.bz2 blaster2 udptest memtest hashtest membustest mergetest seektest addtest monitor reindex convert maketestindex makespiderdb makeclusterdb urlinfo gbfilter dnstest thunder dmozparse gbtitletest gmon.* quarantine core core.*

#.PHONY: GBVersion.cpp

convert.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

StopWords.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

Places.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

Loop.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

hash.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

fctypes.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

IndexList.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

Matches.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

Highlight.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

matches2.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

linkspam.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

Matchers.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

HtmlParser.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 


# Url::set() seems to take too much time
Url.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

# Sitedb has that slow matching code
Sitedb.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

Catdb.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

# when making a new file, add the recs to the map fast
RdbMap.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

# this was getting corruption, was it cuz we used -O2 compiler option?
# RdbTree.o:
# 	$(CC) $(DEFS) $(CPPFLAGS) -O3 -c $*.cpp 

RdbBuckets.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O3 -c $*.cpp 

Linkdb.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O3 -c $*.cpp 

XmlDoc.o:
	$(CC) $(DEFS) $(CPPFLAGS) $(XMLDOCOPT) -c $*.cpp 

# final gigabit generation in here:
Msg40.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

seo.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O3 -c $*.cpp 

TopTree.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O3 -c $*.cpp 

UdpServer.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

RdbList.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

Rdb.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

# take this out. seems to not trigger merges when percent of
# negative titlerecs is over 40.
RdbBase.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

# RdbCache.cpp gets "corrupted" with -O2... like RdbTree.cpp
#RdbCache.o:
#	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

# fast dictionary generation and spelling recommendations
#Speller.o:
#	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

# O2 seems slightly faster than O2 on this for some reason
# O2 is almost twice as fast as no O
IndexTable.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

IndexTable2.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

Posdb.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

# Query::setBitScores() needs this optimization
#Query.o:
#	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

# Msg3's should calculate the page ranges fast
#Msg3.o:
#	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

# fast parsing
Xml.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 
XmlNode.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 
Words.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 
Unicode.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 
UCWordIterator.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 
UCPropTable.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 
UnicodeProperties.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 
UCNormalizer.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 
Pos.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 
Pops.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 
Bits.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 
Scores.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 
Sections.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 
Weights.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 
neighborhood.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 
TermTable.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 
# why was this commented out?
Summary.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 
Title.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

# fast relate topics generation
Msg24.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

Msg1a.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

Msg1b.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

SafeBuf.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O3 -c $*.cpp 

Msg1c.o:
	$(CC) $(DEFS) $(CPPFLAGS)  -c $*.cpp 

Msg1d.o:
	$(CC) $(DEFS) $(CPPFLAGS)  -c $*.cpp 

AutoBan.o:
	$(CC) $(DEFS) $(CPPFLAGS)  -O2 -c $*.cpp 

Profiler.o:
	$(CC) $(DEFS) $(CPPFLAGS)  -O2 -c $*.cpp 

HtmlCarver.o:
	$(CC) $(DEFS) $(CPPFLAGS)  -O2 -c $*.cpp 

HashTableT.o:
	$(CC) $(DEFS) $(CPPFLAGS)  -O2 -c $*.cpp 

Timedb.o:
	$(CC) $(DEFS) $(CPPFLAGS)  -O2 -c $*.cpp 

HashTableX.o:
	$(CC) $(DEFS) $(CPPFLAGS)  -O2 -c $*.cpp 

# getUrlFilterNum2()
Spider.o:
	$(CC) $(DEFS) $(CPPFLAGS)  -O2 -c $*.cpp 

SpiderCache.o:
	$(CC) $(DEFS) $(CPPFLAGS)  -O2 -c $*.cpp 

DateParse.o:
	$(CC) $(DEFS) $(CPPFLAGS)  -O2 -c $*.cpp 

#DateParse2.o:
#	$(CC) $(DEFS) $(CPPFLAGS)  -O2 -c $*.cpp 

test_parser2.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O2 -c $*.cpp 

Language.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O3 -c $*.cpp 

WordsWindow.o:
	$(CC) $(DEFS) $(CPPFLAGS)  -O2 -c $*.cpp 

AppendingWordsWindow.o:
	$(CC) $(DEFS) $(CPPFLAGS)  -O2 -c $*.cpp 

PostQueryRerank.o:
	$(CC) $(DEFS) $(CPPFLAGS)  -O2 -c $*.cpp 

sort.o:
	$(CC) $(DEFS) $(CPPFLAGS) -O3 -c $*.cpp 

# SiteBonus.o:
# 	$(CC) $(DEFS) $(CPPFLAGS)  -O3 -c $*.cpp 
Msg6a.o:
	$(CC) $(DEFS) $(CPPFLAGS)  -O3 -c $*.cpp 

# Stupid gcc-2.95 stabs debug can't handle such a big file.
# add -m32 flag to this line if you need to make a 32-bit gb.
#geo_ip_table.o: geo_ip_table.cpp geo_ip_table.h
#	$(CC) $(DEFS) -Wall -pipe -c $*.cpp 

# dpkg-buildpackage calls 'make binary' to create the files for the deb pkg
# which must all be stored in ./debian/gb/

install:
# gigablast will copy over the necessary files. it has a list of the
# necessary files and that list changes over time so it is better to let gb
# deal with it.
	mkdir -p $(DESTDIR)/var/gigablast/data0/
	mkdir -p $(DESTDIR)/usr/bin/
	mkdir -p $(DESTDIR)/etc/init.d/
	mkdir -p $(DESTDIR)/etc/init/
	mkdir -p $(DESTDIR)/etc/rc3.d/
	mkdir -p $(DESTDIR)/lib/init/
	./gb copyfiles $(DESTDIR)/var/gigablast/data0/
# if user types 'gb' it will use the binary in /var/gigablast/data0/gb
	rm -f $(DESTDIR)/usr/bin/gb
	ln -s /var/gigablast/data0/gb $(DESTDIR)/usr/bin/gb
# if machine restarts...
# the new way that does not use run-levels anymore
#	rm -f $(DESTDIR)/etc/init.d/gb
#	ln -s /lib/init/upstart-job $(DESTDIR)/etc/init.d/gb
# initctl upstart-job conf file (gb stop|start|reload)
#	cp init.gb.conf $(DESTDIR)/etc/init/gb.conf
	cp S99gb $(DESTDIR)/etc/init.d/gb
	ln -s /etc/init.d/gb $(DESTDIR)/etc/rc3.d/S99gb

.cpp.o:
	$(CC) $(DEFS) $(CPPFLAGS) -c $*.cpp 

.c.o:
	$(CC) $(DEFS) $(CPPFLAGS) -c $*.c 

#.cpp: $(OBJS)
#	$(CC) $(DEFS) $(CPPFLAGS) -o $@ $@.o $(OBJS) $(LIBS)
#	$(CC) $(DEFS) $(CPPFLAGS) -o $@ $@.cpp $(OBJS) $(LIBS)

##
## Auto dependency generation
## This is broken, if you edit Version.h and recompile it doesn't work. (mdw)
#%.d: %.cpp	
#	@echo "generating dependency information for $<"
#	@set -e; $(CC) -M $(DEFS) $(CPPFLAGS) $< \
#	| sed 's/\($*\)\.o[ :]*/\1.o $@ : /g' > $@; \
#	[ -s $@ ] || rm -f $@
#-include $(SRCS:.cpp=.d)

depend: 
	@echo "generating dependency information"
	( $(CC) -MM $(DEFS) $(DPPFLAGS) *.cpp > Make.depend ) || \
	$(CC) -MM $(DEFS) $(DPPFLAGS) *.cpp > Make.depend 

-include Make.depend

# REDHAT PACKAGE SECTION BEGIN

# try building the .deb and then running 'alien --to-rpm gb_1.0-1_i686.deb'
# to build the .rpm

# move this tarball into ~/rpmbuild/?????
# then run rpmbuild -ba gb-1.0.spec to build the rpms
# rpm -ivh gb-1.0-...  to install the pkg
# testing-rpm:
# 	git archive --format=tar --prefix=gb-1.0/ testing > gb-1.0.tar
# 	mv gb-1.0.tar /home/mwells/rpmbuild/SOURCES/
# 	rpmbuild -bb gb-1.0.spec
# 	scp /home/mwells/rpmbuild/RPMS/x86_64/gb-*rpm www.gigablast.com:/w/html/

# master-rpm:
# 	git archive --format=tar --prefix=gb-1.0/ master > gb-1.0.tar
# 	mv gb-1.0.tar /home/mwells/rpmbuild/SOURCES/
# 	rpmbuild -bb gb-1.0.spec
# 	scp /home/mwells/rpmbuild/RPMS/x86_64/gb-*rpm www.gigablast.com:/w/html/

# REDHAT PACKAGE SECTION END

# DEBIAN PACKAGE SECTION BEGIN

# need to do 'apt-get install dh-make'
# deb-master
master-deb32:
	git archive --format=tar --prefix=gb-1.$(VERSION)/ master > ../gb_1.$(VERSION).orig.tar
	rm -rf debian
# change "-p gb_1.0" to "-p gb_1.1" to update version for example
	dh_make -y -s -e gigablast@mail.com -p gb_1.$(VERSION) -f ../gb_1.$(VERSION).orig.tar
# zero this out, it is just filed with the .txt files erroneously and it'll
# try to automatiicaly install in /usr/docs/
	rm debian/docs
	touch debian/docs
# make the debian/copyright file contain the license
	cp copyright.head  debian/copyright
#	cat LICENSE | awk -Fxvcty '{print " "$1}' >> debian/copyright
	cat LICENSE >> debian/copyright
	cat copyright.tail >> debian/copyright
# the control file describes the package
	cp control.deb debian/control
# try to use our own rules so we can override dh_shlibdeps and others
	cp gb.deb.rules debian/rules
# make our own changelog file
	echo "gb (1."$(VERSION)"-1) unstable; urgency=low" > changelog
	echo "" >> changelog
	echo "  * More bug fixes." >> changelog
	echo "" >> changelog
	echo -n " -- mwells <gigablast@mail.com>  " >> changelog	
	date +"%a, %d %b %Y %T %z" >> changelog
	echo "" >> changelog
	cp changelog debian/changelog
# fix dh_shlibdeps from bitching about dependencies on shared libs
# YOU HAVE TO RUN THIS before you run 'make'
#	export LD_LIBRARY_PATH=./debian/gb/var/gigablast/data0
# build the package now
#	dpkg-buildpackage -j6 -nc -ai386 -ti386 -b -uc -rfakeroot
	dpkg-buildpackage -j6 -nc -b -uc -rfakeroot

# move to current dur
	mv ../gb_*.deb .	
# upload deb
	scp gb_1.$(VERSION)*.deb gk268:/w/html/	
# alien it
	sudo alien --to-rpm gb_1.$(VERSION)-1_i386.deb
# upload rpm
	scp gb-1.$(VERSION)*.rpm gk268:/w/html/	


master-deb64:
	git archive --format=tar --prefix=gb-1.$(VERSION)/ master > ../gb_1.$(VERSION).orig.tar
	rm -rf debian
# change "-p gb_1.0" to "-p gb_1.1" to update version for example
	dh_make -y -s -e gigablast@mail.com -p gb_1.$(VERSION) -f ../gb_1.$(VERSION).orig.tar
# zero this out, it is just filed with the .txt files erroneously and it'll
# try to automatiicaly install in /usr/docs/
	rm debian/docs
	touch debian/docs
# make the debian/copyright file contain the license
	cp copyright.head  debian/copyright
#	cat LICENSE | awk -Fxvcty '{print " "$1}' >> debian/copyright
	cat LICENSE >> debian/copyright
	cat copyright.tail >> debian/copyright
# the control file describes the package
	cp control.deb debian/control
# try to use our own rules so we can override dh_shlibdeps and others
	cp gb.deb.rules debian/rules
# make our own changelog file
	echo "gb (1.$(VERSION)-1) unstable; urgency=low" > changelog
	echo "" >> changelog
	echo "  * More bug fixes." >> changelog
	echo "" >> changelog
	echo -n " -- mwells <gigablast@mail.com>  " >> changelog	
	date +"%a, %d %b %Y %T %z" >> changelog
	echo "" >> changelog
	cp changelog debian/changelog
# fix dh_shlibdeps from bitching about dependencies on shared libs
# YOU HAVE TO RUN THIS before you run 'make'
#	export LD_LIBRARY_PATH=./debian/gb/var/gigablast/data0
# build the package now
	dpkg-buildpackage -nc -aamd64 -tamd64 -b -uc -rfakeroot
# move to current dur
	mv ../gb_*.deb .	
# upload deb
	scp gb_1.$(VERSION)*.deb gk268:/w/html/	
# alien it
	sudo alien --to-rpm gb_1.$(VERSION)-1_amd64.deb
# upload rpm
	scp gb-1.$(VERSION)*.rpm gk268:/w/html/	



#deb-testing
#testing-deb:
#	git archive --format=tar --prefix=gb-1.5/ testing > ../gb_1.5.orig.tar
#	rm -rf debian
# change "-p gb_1.0" to "-p gb_1.1" to update version for example
#	dh_make -e gigablast@mail.com -p gb_1.5 -f ../gb_1.5.orig.tar
# zero this out, it is just filed with the .txt files erroneously and it'll
# try to automatiicaly install in /usr/docs/
#	rm debian/docs
#	touch debian/docs
# make the debian/copyright file contain the license
#	cp copyright.head  debian/copyright
##	cat LICENSE | awk -Fxvcty '{print " "$1}' >> debian/copyright
#	cat LICENSE >> debian/copyright
#	cat copyright.tail >> debian/copyright
# the control file describes the package
#	cp control.deb debian/control
# try to use our own rules so we can override dh_shlibdeps and others
#	cp gb.deb.rules debian/rules
#	cp changelog debian/changelog
# make the pkg dependencies file ourselves since we overrode dh_shlibdeps
# with our own debian/rules file. see that file for more info.
##	echo  "shlibs:Depends=libc6 (>= 2.3)" > debian/gb.substvars 
##	echo  "shlibs:Depends=netpbm (>= 0.0)" > debian/gb.substvars 
##	echo  "misc:Depends=netpbm (>= 0.0)" > debian/gb.substvars 
# fix dh_shlibdeps from bitching about dependencies on shared libs
# YOU HAVE TO RUN THIS before you run 'make'
##	export LD_LIBRARY_PATH=./debian/gb/var/gigablast/data0
# build the package now. if we don't specify -ai386 -ti386 then some users
# get a wrong architecture msg and 'dpkg -i' fails
#	dpkg-buildpackage -nc -ai686 -ti686 -b -uc -rfakeroot
##	dpkg-buildpackage -nc -b -uc -rfakeroot
# move to current dur
#	mv ../gb_*.deb .	

install-pkgs-local:
	sudo alien --to-rpm gb_1.5-1_i686.deb
# upload
	scp gb*.deb gb*.rpm gk268:/w/html/


# DEBIAN PACKAGE SECTION END


# You may need:
# sudo apt-get install libffi-dev libssl-dev
warcinjector: 
	-rm -r /home/zak/.pex/build/inject-*
	-rm -r /home/zak/.pex/install/inject-*
	cd script && pex -v . gevent gevent-socketio requests pyopenssl ndg-httpsclient pyasn1 multiprocessing -e inject -o warc-inject --inherit-path --no-wheel

