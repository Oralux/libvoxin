#!/bin/bash -e

BASE=$(dirname $(realpath "$0"))
NAME=$(basename "$0")

. ./conf.inc
getVersion
[ -z "$VERMAJ" ] && exit 1


usage() {
	echo "
Usage: 
 $NAME [options]

Build the libvoxin binaries (libvoxin, voxind, tests).

Voxind will be installed in a 32 bits root filesystem to lower
dependencies issues with the host filesystem.

Note that a minimal 32 bits root filesystem is needed as input to this
script (see README.org).

Options: 
-c, --clean		   clean-up: delete the build directory and object files
-d, --debug        compile with debug symbols 
-h, --help         display this help 
-m, --mach <arch>  architecture of the native binaries (libvoxin,
                   tests). arch=i386, otherwise current arch 
-r, --rfs32 <dir>  path of a 32 bits root filesystem.
                   default: /opt/oralux/voxin/rfs32
-R, --release      build the tarballs in build/<arch>/release:
                   - libvoxin: libvoxin-${VERSION}-<arch>.tgz
                   - voxind: voxind-${VERSION}-<arch>.tgz  
-t, --test         build tests 

Example:
# compile libvoxin and voxind using the default 32 bits root
# filesystem present in /opt/oralux/voxin/rfs32
 $0

# compile libvoxin/voxind and build the tarballs
 $0 -R

# compile libvoxin/voxind/tests with debug symbols
 $0 -dt

" 

}

unset CC CFLAGS CLEAN DBG_FLAGS HELP ARCH RELEASE RFS32REF STRIP TEST 

OPTIONS=`getopt -o cdhm:r:Rt --long clean,debug,help,mach:,rfs32:,release,test \
             -n "$NAME" -- "$@"`
[ $? != 0 ] && usage && exit 1
eval set -- "$OPTIONS"

while true; do
  case "$1" in
    -c|--clean) CLEAN=1; shift;;
    -d|--debug) export DBG_FLAGS="-ggdb -DDEBUG"; export STRIP=test; shift;;
    -h|--help) HELP=1; shift;;
    -m|--mach) ARCH=$2; shift 2;;
    -r|--rfs32) RFS32REF=$2; shift 2;;
    -R|--release) RELEASE=1; shift;;
    -t|--test) TEST=1; shift;;
    --) shift; break;;
    *) break;;
  esac
done

IBMTTSLIB=/opt/IBM/ibmtts/lib
IBMTTSCONF=/eci.ini

case "$ARCH" in
	i386) ;;	
	*)
		# [ -n "$DEB_HOST_ARCH" ] && ARCH="$DEB_HOST_ARCH"
		ARCH=$(uname -m);;
esac

[ -z "$RFS32REF" ] && RFS32REF=/opt/oralux/voxin/rfs32

if [[ -z "$CLEAN" && ( ! -e "$RFS32REF/$IBMTTSLIB" || ! -e "$RFS32REF/$IBMTTSCONF") ]]; then
	echo "No $RFS32REF/$IBMTTSLIB or $RFS32REF/$IBMTTSCONF"
	HELP=1
fi

[ -n "$HELP" ] && usage && exit 0

cleanup
[ -n "$CLEAN" ] && exit 0

cd "$BASE"
SRCDIR="$BASE/src"
ARCHDIR="$BASE/build/$ARCH"
RELDIR="$ARCHDIR/release"
RFSDIR="$ARCHDIR/rfs"
export DESTDIR="$RFSDIR/opt/oralux/voxin"
RFS32="$DESTDIR/rfs32"
DESTDIR_RFS32="$RFS32/usr"

mkdir -p "$DESTDIR"
mkdir -p "$DESTDIR_RFS32"
cp -a "$RFS32REF"/* "$RFS32"

export CFLAGS="$DBG_FLAGS"
export CXXFLAGS="$DBG_FLAGS"
unset LDFLAGS
if [ "$ARCH" = "i386" ]; then
	CFLAGS="$CFLAGS -m32"
	CXXFLAGS="$CXXFLAGS -m32"
	LDFLAGS="$LDFLAGS -m32"
fi

# libcommon
echo "Entering common"
cd "$SRCDIR"/common
DESTDIR="$DESTDIR_RFS32" make clean
DESTDIR="$DESTDIR_RFS32" CFLAGS="$CFLAGS -m32" LDFLAGS="$LDFLAGS -m32" make all
DESTDIR="$DESTDIR_RFS32" make install

make clean
make all
make install

# libvoxin
echo "Entering puncfilter"
cd "$SRCDIR"/puncfilter
make clean
make all

echo "Entering libvoxin"
cd "$SRCDIR"/libvoxin
make clean
make all
make install

# voxind
echo "Entering voxind"
cd "$SRCDIR"/voxind
DESTDIR=$DESTDIR_RFS32 make clean
DESTDIR=$DESTDIR_RFS32 make all
DESTDIR=$DESTDIR_RFS32 make install

#say
echo "Entering say"
cd "$SRCDIR"/say
make clean
make all
make install

# test
if [ -n "$TEST" ]; then
	echo "Entering test"
	cd "$SRCDIR"/test
	make clean
	make all
	make install
fi

# symlinks for a global install (to be adapted according to the distro)
cd $RFSDIR
mkdir -p usr/{bin,lib}
ln -s ../../opt/oralux/voxin/rfs32/usr/bin/voxind usr/bin/voxind
ln -s ../../opt/oralux/voxin/bin/voxin-say usr/bin/voxin-say
ln -s ../../opt/oralux/voxin/lib/libvoxin.so."$VERMAJ" usr/lib/libibmeci.so
ln -s ../../opt/oralux/voxin/lib/libvoxin.so."$VERMAJ" usr/lib/libvoxin.so."$VERMAJ"
# compat ibmtts clients
mkdir -p opt/IBM/ibmtts/inc
ln -s ../../../oralux/voxin/rfs32/opt/IBM/ibmtts/inc/eci.h opt/IBM/ibmtts/inc/eci.h


if [ -n "$RELEASE" ]; then
	mkdir -p "$RELDIR"
	fakeroot bash -c "\
tar -C \"$RFSDIR\" -zcf \"$RELDIR/libvoxin$VERMAJ-$VERSION-$ARCH.tgz\" usr/lib/libibmeci.so usr/lib/libvoxin.so."$VERMAJ" opt/oralux/voxin/lib/libvoxin.so* && \
tar -C \"$RFSDIR\" -zcf \"$RELDIR/voxind-$VERSION-all.tgz\" usr/bin/voxind opt/oralux/voxin/rfs32/usr/bin/voxind && \
tar -C \"$RFSDIR\" -zcf \"$RELDIR/voxin-say-$VERSION-$ARCH.tgz\" usr/bin/voxin-say opt/oralux/voxin/bin/voxin-say && \
tar -C \"$RFSDIR\" -zcf \"$RELDIR/libvoxin$VERMAJ-dev-$VERSION-all.tgz\" opt/IBM/ibmtts/inc/eci.h
"
	printf "\nTarballs available in $RELDIR\n"	
fi

