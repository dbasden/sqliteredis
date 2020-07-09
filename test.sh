#!/bin/bash

set -e -o pipefail

(./sqlitedis 2>&1 || true )| fgrep vfs # Dump list of VFSs

./sqlitedis '
select 1+2;
'

./sqlitedis '
CREATE TABLE fish (a,b,c);
INSERT INTO fish VALUES (1,2,3);
SELECT * FROM fish;
'

export SQLITE_DB='file:?vfs=redisvfs'
export SQLITE_LOADEXT=./redisvfs

echo ---
(./sqlitedis 2>&1 || true )| fgrep vfs # Dump list of VFSs

./sqlitedis '
select 1+2;
'

./sqlitedis '
CREATE TABLE fish (a,b,c);
INSERT INTO fish VALUES (1,2,3);
SELECT * FROM fish;
'
