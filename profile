#!/bin/bash
set -x
gcc -Wall -march=native $1.c -o bin/$1 -pg -g
rm grpof.out
./bin/$1
gprof bin/$1 | more
