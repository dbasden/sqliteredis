CC=gcc
CPP=g++
SHELL=bash

SQLITE_BUILD=../sqlite/build
INCLUDES=-I${SQLITE_BUILD} -I/usr/include/hiredis
LDFLAGS=-L${SQLITE_BUILD}/.libs
LDLIBS=-l:libsqlite3.a -ldl -lpthread -lhiredis

#CPPFLAGS=-Wall -O2 -ggdb ${INCLUDES}
CPPFLAGS=-Wall -ggdb ${INCLUDES}

default: sqlitedis redisvfs.so static-sqlitedis static-redisvfs.o

clean:
	rm -f redisvfs.so sqlitedis static-sqlitedis static-sqldis.o

# sqlite extension module
redisvfs.so: redisvfs.c redisvfs.h
	gcc ${CPPFLAGS} ${INCLUDES} -fPIC -shared redisvfs.c -l hiredis -o redisvfs.so

# test tool with statically linked redisvfs

static-sqlitedis: CFLAGS=-Wall -ggdb -DSTATIC_REDISVFS
static-sqlitedis: CPPFLAGS=${CFLAGS}
static-sqlitedis: sqlitedis.cc redisvfs.c redisvfs.h
	gcc ${CFLAGS} ${INCLUDES} -o static-redisvfs.o -c redisvfs.c
	g++ $(CPPFLAGS) $(INCLUDES) $(LDFLAGS) -o static-sqlitedis static-redisvfs.o sqlitedis.cc $(LDLIBS)

# Link vfsstat module
# (of limited use as it uses the VFS it's shadowing to write it's log)
vfsstat.so: SQLITE_SRC=../sqlite
vfsstat.so:
	gcc ${CPPFLAGS} ${INCLUDES} -fPIC -shared ${SQLITE_SRC}/ext/misc/vfsstat.c -o vfsstat.so
