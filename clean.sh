#!/bin/bash -evx

if [ -z "$RFS" ]; then
	RFS=$PWD/build/rfs
fi

BASE=$PWD
LIB32=$RFS/usr/lib/i386-linux-gnu
LIB64=$RFS/usr/lib/x86_64-linux-gnu
BINDIR=$RFS/usr/bin

rm -rf $PWD/build
# mkdir -p $LIB32
# mkdir -p $LIB64
# mkdir -p $BINDIR

# libcommon
echo "Entering common"
cd $BASE/src/common
make clean


# libvoxin
echo "Entering libvoxin"
cd $BASE/src/libvoxin
make clean

# voxind
echo "Entering voxind"
cd $BASE/src/voxind
make clean

# test
echo "Entering test"
cd $BASE/src/test
make clean

