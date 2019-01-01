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

Options: 
-c, --clean		   clean-up: delete the build directory and object files
-d, --debug        compile with debug symbols 
-h, --help         display this help 
-m, --mach <arch>  target architecture of libvoxin and tests
		   		   possible value: x86; by default: current arch
-r, --release      build the tarballs in build/<arch>/release
-t, --test         build tests 

Example:
# compile libvoxin and voxind
 $0

# compile libvoxin/voxind and build the tarballs
 $0 -r

# compile libvoxin/voxind/tests with debug symbols
 $0 -dt

" 

}

unset CC CFLAGS CLEAN DBG_FLAGS HELP ARCH RELEASE STRIP TEST 

OPTIONS=`getopt -o cdhm:rt --long clean,debug,help,mach:,release,test \
             -n "$NAME" -- "$@"`
[ $? != 0 ] && usage && exit 1
eval set -- "$OPTIONS"

while true; do
  case "$1" in
    -c|--clean) CLEAN=1; shift;;
    -d|--debug) export DBG_FLAGS="-ggdb -DDEBUG"; export STRIP=test; shift;;
    -h|--help) HELP=1; shift;;
    -m|--mach) ARCH=$2; shift 2;;
    -R|--release) RELEASE=1; shift;;
    -t|--test) TEST=1; shift;;
    --) shift; break;;
    *) break;;
  esac
done

case "$ARCH" in
	x86|i386|i686) ARCH=i386;;	
	*)
		# [ -n "$DEB_HOST_ARCH" ] && ARCH="$DEB_HOST_ARCH"
		ARCH=$(uname -m);;
esac

[ -n "$HELP" ] && usage && exit 0

cleanup
[ -n "$CLEAN" ] && exit 0

cd "$BASE"
SRCDIR="$BASE/src"
ARCHDIR="$BASE/build/$ARCH"
RELDIR="$ARCHDIR/release"
RFSDIR="$ARCHDIR/rfs"
VOXINDIR=opt/oralux/voxin
export DESTDIR="$RFSDIR/$VOXINDIR"
RFS32="$DESTDIR/rfs32"
DESTDIR_RFS32="$RFS32/usr"
IBMTTSDIR=$RFSDIR/opt/IBM/ibmtts

mkdir -p "$DESTDIR_RFS32"

export CFLAGS="$DBG_FLAGS"
export CXXFLAGS="$DBG_FLAGS"
unset LDFLAGS
if [ "$ARCH" = "i386" ]; then
	CFLAGS="$CFLAGS -m32"
	CXXFLAGS="$CXXFLAGS -m32"
	LDFLAGS="-m32"
fi

# libcommon
echo "Entering common"
cd "$SRCDIR"/common
DESTDIR="$DESTDIR_RFS32" make clean
DESTDIR="$DESTDIR_RFS32" CFLAGS="$CFLAGS -m32" LDFLAGS="-m32" make all
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

echo "Entering api"
cd "$SRCDIR"/api
make clean
make install

# libibmeci (moc)
echo "Entering libibmeci"
cd "$SRCDIR"/libibmeci
DESTDIR=$IBMTTSDIR make clean
DESTDIR=$IBMTTSDIR make all
DESTDIR=$IBMTTSDIR make install

# voxind
echo "Entering voxind"
cd "$SRCDIR"/voxind
DESTDIR=$DESTDIR_RFS32 IBMTTSDIR=$IBMTTSDIR make clean
DESTDIR=$DESTDIR_RFS32 IBMTTSDIR=$IBMTTSDIR make all
DESTDIR=$DESTDIR_RFS32 IBMTTSDIR=$IBMTTSDIR make install

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
mkdir -p usr/{bin,lib,include/voxin}
ln -s ../../$VOXINDIR/lib/libvoxin.so usr/lib/libibmeci.so
ln -s ../../$VOXINDIR/lib/libvoxin.so usr/lib/libvoxin.so
ln -s ../../$VOXINDIR/include/voxin.h usr/include/voxin/voxin.h
ln -s ../../$VOXINDIR/include/eci.h usr/include/voxin/eci.h
ln -s ../../$VOXINDIR/bin/voxin-say usr/bin/voxin-say
ln -s ../../../../var/opt/IBM/ibmtts/cfg/eci.ini $VOXINDIR/rfs32/eci.ini

if [ -n "$RELEASE" ]; then
	mkdir -p "$RELDIR"
	fakeroot bash -c "\
tar -C \"$RFSDIR\" \
	   -Jcf \"$RELDIR/voxin-pkg_$VERMAJ_$VERSION.any.txz\" \
	   usr/lib/libvoxin.so usr/lib/libibmeci.so \
	   usr/include/voxin/eci.h usr/include/voxin/voxin.h \
	   usr/bin/voxin-say && \
tar -C \"$RFSDIR\" \
	   -Jcf \"$RELDIR/libibmeci-fake_$VERMAJ_$VERSION.any.txz\" \
	   opt/IBM  && \
tar -C \"$RFSDIR\" \
	   --exclude=libibmeci.so --exclude \"*.a\" \
	   -Jcf \"$RELDIR/libvoxin_$VERMAJ_$VERSION.$ARCH.txz\" $VOXINDIR \
"

#tar -C \"$RFSDIR\" -Jcf \"$RELDIR/voxin-update_$VERMAJ_$VERSION.$ARCH.txz\" $VOXINDIR/lib/libvoxin.so* $VOXINDIR/rfs32/usr/bin/voxind $VOXINDIR/bin/voxin-say && \
	#tar -C \"$RFSDIR\" -Jcf \"$RELDIR/voxin-say_$VERSION.$ARCH.txz\" usr/bin/voxin-say && \


	#tar -C \"$RFSDIR\" -Jcf \"$RELDIR/libvoxin$VERMAJ-dev_$VERSION.all.txz\" opt/IBM/ibmtts/inc/eci.h


	printf "\nTarballs available in $RELDIR\n"	
fi

