#!/bin/bash

set -e -o pipefail

unset SQLITE_DB
unset SQLITE_LOADEXT

echo --- no sqliteredis

(./sqlitedis 2>&1 || true )| fgrep vfs # Dump list of VFSs

export SQLITE_DB=":memory:"
./sqlitedis '
select 1+2;
'

./sqlitedis '
DROP TABLE IF EXISTS fish;
CREATE TABLE fish (a,b,c);
INSERT INTO fish VALUES (1,2,3);
SELECT * FROM fish;
'

echo
echo --- static sqliteredis
export SQLITE_DB='file:database?vfs=redisvfs'
(./static-sqlitedis 2>&1 || true )| fgrep vfs # Dump list of VFSs

./static-sqlitedis '
select 1+2;
'

./static-sqlitedis '
DROP TABLE IF EXISTS fish;
CREATE TABLE fish (a,b,c);
INSERT INTO fish VALUES (1,2,3);
SELECT * FROM fish;
'

echo
echo --- dynload sqliteredis
export SQLITE_DB='file:database?vfs=redisvfs'
export SQLITE_LOADEXT=./redisvfs

(./sqlitedis 2>&1 || true )| fgrep vfs # Dump list of VFSs

./sqlitedis '
select 1+2;
'

./sqlitedis '
DROP TABLE IF EXISTS fish;
CREATE TABLE fish (a,b,c);
INSERT INTO fish VALUES (1,2,3);
INSERT INTO fish VALUES ("Yep","This","Works")
SELECT * FROM fish;
'
