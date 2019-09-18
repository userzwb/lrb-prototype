#!/bin/bash
# call at traffic server dir
autoreconf -if
./configure --prefix=/opt/ts --enable-debug
make -j32
make install
