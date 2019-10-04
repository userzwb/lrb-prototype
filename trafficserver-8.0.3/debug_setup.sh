#!/bin/bash
# call at traffic server dir
#autoreconf -if
make clean
#./configure  --prefix=/opt/ts --disable-hwloc
#./configure  --prefix=/opt/ts --disable-hwloc --enable-debug
./configure  --prefix=/opt/ts --enable-asan --enable-debug
#./configure --prefix=/opt/ts --enable-debug
make -j32
make install
