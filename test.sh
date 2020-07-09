#!/bin/bash

set -e -o pipefail

./sqlitedis 'SELECT 1;'
