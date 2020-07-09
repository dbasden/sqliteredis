CC=gcc
CPP=g++

INCLUDES=-I../sqlite/build
LDFLAGS=-L../sqlite/build/.libs -lsqlite3

CPPFLAGS=-Wall -O2 -ggdb ${INCLUDES} ${LDFLAGS}

default: sqlitedis
