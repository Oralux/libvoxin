#!/bin/bash -ex

if [ -n "$DEB_HOST_ARCH" ]; then
	ARCH="$DEB_HOST_ARCH"
else
	ARCH=$(uname -m)
fi

function usage()
{
	echo "build.sh [libvoxin|voxind|all [rfs32] [debug]]"
}

function check_arg()
{
    case "$1" in
	"debug")	    
	    export DBG_FLAGS="-ggdb -DDEBUG"
	    export STRIP=test
	    ;;
	"rfs32")	    
	    RFS32=-DWITH_RFS_32
	    ;;
	*)
	    ;;
    esac
}

if [ -z "$RFS" ]; then
	RFS=$PWD/build/rfs
fi


unset LIBVOXIN VOXIND TEST CC CFLAGS DBG_FLAGS STRIP RFS32
if [ "$#" -ge "1" ]; then
	case "$1" in
		-h|--help) usage; exit 0;;
		libvoxin) LIBVOXIN=1;;
		voxind) VOXIND=1;;
		*) LIBVOXIN=1; VOXIND=1; TEST=1;;
	esac

	check_arg $2
	check_arg $3
else
	LIBVOXIN=1
	VOXIND=1	
	TEST=1
fi

BASE=$PWD
LIB32=$RFS/usr/lib/i386-linux-gnu
LIB64=$RFS/usr/lib/x86_64-linux-gnu
BINDIR=$RFS/usr/bin

rm -rf $RFS
install -d -m 755 $BINDIR

# libcommon
echo "Entering common"
cd $BASE/src/common
if [ "$ARCH" = "i386" ] || [ "$VOXIND" = "1" ]; then
	install -d -m 755 $LIB32
	make clean
	CFLAGS="-m32 $DBG_FLAGS" LDFLAGS=-m32 make all
	INSTALL_DIR=$LIB32 make install
fi

if [ "$ARCH" != "i386" ]; then
	install -d -m 755 $LIB64
	make clean
	CFLAGS="$DBG_FLAGS $RFS32" make all
	INSTALL_DIR=$LIB64 make install
fi

# libvoxin
if [ -n "$LIBVOXIN" ]; then
	if [ "$ARCH" = "i386" ]; then
		CFLAGS="-m32 $DBG_FLAGS"
		CXXFLAGS="-m32 $DBG_FLAGS"
		LDFLAGS=-m32
		LIB=$LIB32
	else
		CFLAGS="$DBG_FLAGS $RFS32"
		CXXFLAGS="$DBG_FLAGS $RFS32"
		LDFLAGS=
		LIB=$LIB64
	fi
	echo "Entering puncfilter"
	cd $BASE/src/puncfilter
	make clean
	LIBDIR=$LIB CFLAGS="$CFLAGS" LDFLAGS=$LDFLAGS make all
	echo "Entering libvoxin"
	cd $BASE/src/libvoxin
	make clean
	LIBDIR=$LIB CFLAGS="$CFLAGS" LDFLAGS=$LDFLAGS make all
	INSTALL_DIR=$LIB make install
fi

# voxind
if [ -n "$VOXIND" ]; then
	echo "Entering voxind"
	cd $BASE/src/voxind
	make clean
	LIBDIR=$LIB32 make all
	INSTALL_DIR=$BINDIR make install
fi

# test
if [ "$ARCH" = "i386" ]; then
	CFLAGS="-m32 $DBG_FLAGS"
	LDFLAGS=-m32
	LIB=$LIB32
else
	CFLAGS="$DBG_FLAGS $RFS32"
	LDFLAGS=
	LIB=$LIB64
fi

if [ -n "$TEST" ]; then
	echo "Entering test"
	cd $BASE/src/test
	make clean
	LIBDIR=$LIB CFLAGS="$CFLAGS" LDFLAGS=$LDFLAGS make all
	INSTALL_DIR=$BINDIR NAME=voxind64 make install
fi

