#!/bin/bash

set -e -o pipefail

./sqlitedis '
select 1+2;
'

./sqlitedis '
CREATE TABLE fish (a,b,c);
INSERT INTO fish VALUES (1,2,3);
SELECT * FROM fish;
'
