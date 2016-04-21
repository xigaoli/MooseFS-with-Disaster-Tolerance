sbin_PROGRAMS=mfssserver

AM_CPPFLAGS=-I$(top_srcdir)/mfscommon -DAPPNAME=mfssserver
AM_LDFLAGS=$(ZLIB_LIBS)
AM_LDFLAGS = -lpthread

mfssserver_SOURCES= \
	masterconn.c masterconn.h \
	init.h \
	../mfscommon/main.c ../mfscommon/main.h \
	../mfscommon/cfg.c ../mfscommon/cfg.h \
	../mfscommon/crc.c ../mfscommon/crc.h \
	../mfscommon/sockets.c ../mfscommon/sockets.h \
	../mfscommon/strerr.c ../mfscommon/strerr.h \
	../mfscommon/datapack.h ../mfscommon/massert.h ../mfscommon/slogger.h \
	../mfscommon/MFSCommunication.h \
	servermap.c  servermap.h \
	work.c work.h    \
	libfile.c libfile.h    \
        pool.c pool.h
