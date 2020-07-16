#!/bin/bash

set -e -o pipefail

unset SQLITE_DB
unset SQLITE_LOADEXT

echo --- no sqliteredis

(./sqlitedis 2>&1 || true )| fgrep vfs # Dump list of VFSs

(
	set -x
	export SQLITE_DB=":memory:"
	./sqlitedis 'select 1+2;'
	./sqlitedis '
	DROP TABLE IF EXISTS fish;
	CREATE TABLE fish (a,b,c);
	INSERT INTO fish VALUES (1,2,3);
	INSERT INTO fish VALUES (4,5,6);
	SELECT * FROM fish;
	DROP TABLE fish;'
)

echo
echo --- static sqliteredis
(
	(./static-sqlitedis 2>&1 || true )| fgrep vfs # Dump list of VFSs

	export SQLITE_DB='file:database?vfs=redisvfs'
	set -x
	./static-sqlitedis 'select 1+2'
	./static-sqlitedis 'DROP TABLE IF EXISTS fish'
	./static-sqlitedis 'CREATE TABLE fish (a,b,c)'
	./static-sqlitedis 'INSERT INTO fish VALUES (1,2,3)'
	./static-sqlitedis 'INSERT INTO fish VALUES (4,5,6)'
	./static-sqlitedis 'SELECT * FROM fish'
	./static-sqlitedis 'DROP TABLE fish'
)

echo
echo --- dynload sqliteredis
(
	export SQLITE_LOADEXT=./redisvfs
	(./sqlitedis 2>&1 || true )| fgrep vfs # Dump list of VFSs

	export SQLITE_DB='file:database?vfs=redisvfs'
	set -x
	./sqlitedis 'select 1+2'
	./sqlitedis 'DROP TABLE IF EXISTS fish'
	./sqlitedis 'CREATE TABLE fish (a,b,c)'
	./sqlitedis 'INSERT INTO fish VALUES (1,2,3)'
	./sqlitedis 'INSERT INTO fish VALUES (4,5,6)'
	./sqlitedis 'SELECT * FROM fish'
	./sqlitedis 'DROP TABLE fish'
)
