CC=gcc
CPP=g++

INCLUDES=-I../sqlite/build
LDFLAGS=-L../sqlite/build/.libs
LDLIBS=-l:libsqlite3.a -ldl -lpthread

CPPFLAGS=-Wall -O2 -ggdb ${INCLUDES}

default: sqlitedis memvfs.so redisvfs.so

clean:
	rm memvfs.so redisvfs.so redisvfs.o sqlitedis

# Example to check linking
memvfs.so:
	gcc ${CPPFLAGS} ${INCLUDES} -fPIC -shared ../sqlite/ext/misc/memvfs.c -o memvfs.so

# sqlite extension module
redisvfs.so: redisvfs.c redisvfs.h
	gcc ${CPPFLAGS} ${INCLUDES} -fPIC -shared redisvfs.c -o redisvfs.so


# test tool with statically linked redisvfs
CPPFLAGS+= -DSTATIC_REDISVFS

sqlitedis: redisvfs.o
