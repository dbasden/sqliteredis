CC=gcc
CPP=g++

SQLITE_SRC=../sqlite
SQLITE_BUILD=../sqlite/build
INCLUDES=-I${SQLITE_BUILD}
LDFLAGS=-L${SQLITE_BUILD}/.libs
LDLIBS=-l:libsqlite3.a -ldl -lpthread

#CPPFLAGS=-Wall -O2 -ggdb ${INCLUDES}
CPPFLAGS=-Wall -ggdb ${INCLUDES}

default: sqlitedis memvfs.so redisvfs.so static-sqlitedis

clean:
	rm -f memvfs.so redisvfs.so redisvfs.o sqlitedis static-sqlitedis static-sqlite3.o

# Example to check linking
memvfs.so:
	gcc ${CPPFLAGS} ${INCLUDES} -fPIC -shared ${SQLITE_SRC}/ext/misc/memvfs.c -o memvfs.so

# sqlite extension module
redisvfs.so: redisvfs.c redisvfs.h
	gcc ${CPPFLAGS} ${INCLUDES} -fPIC -shared redisvfs.c -o redisvfs.so


sqlitedis: sqlitedis.cc


# test tool with statically linked redisvfs

#redisvfs.o: redisvfs.c redisvfs.h

static-sqlite3.o:
	gcc $(CPPFLAGS) $(INCLUDES) -o static-sqlite3.o -c ${SQLITE_BUILD}/sqlite3.c
static-sqlitedis: CPPFLAGS=-Wall -ggdb -DSTATIC_REDISVFS
static-sqlitedis: LDLIBS=-ldl -lpthread
static-sqlitedis: sqlitedis.cc redisvfs.c redisvfs.h static-sqlite3.o
	g++ $(CPPFLAGS) $(INCLUDES) $(LDLIBS) -o static-sqlitedis redisvfs.c sqlitedis.cc static-sqlite3.o
