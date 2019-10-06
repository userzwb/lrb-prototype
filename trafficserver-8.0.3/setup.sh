#!/bin/bash
# call at traffic server dir
#autoreconf -if
make clean
./configure --prefix=/opt/ts
make -j32
make install
