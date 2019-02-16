#!/bin/bash -ex

BASE=$(dirname $(realpath "$0"))
NAME=$(basename "$0")

cd "$BASE"
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
		   		   possible value: i386; by default: current arch
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
    -r|--release) RELEASE=1; shift;;
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
DOCDIR=$DESTDIR/share/doc/libvoxin

mkdir -p "$DESTDIR_RFS32"

export CFLAGS="$DBG_FLAGS"
export CXXFLAGS="$DBG_FLAGS"
unset LDFLAGS
if [ "$ARCH" = "i386" ]; then
	export CFLAGS="$CFLAGS -m32"
	export CXXFLAGS="$CXXFLAGS -m32"
	export LDFLAGS="-m32"
fi

# libcommon
echo "Entering common"
cd "$SRCDIR"/common
DESTDIR="$DESTDIR_RFS32" make clean
DESTDIR="$DESTDIR_RFS32" CFLAGS="$CFLAGS -m32" LDFLAGS="-m32" make all
DESTDIR="$DESTDIR_RFS32" make install

make clean
CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" make all
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

buildLibVoxinTarball() {
	fakeroot bash -c "\
tar -C \"$RFSDIR\" \
	   --exclude=libibmeci.so --exclude \"*.a\" \
	   -Jcf \"$RELDIR/libvoxin_$VERMAJ_$VERSION.$ARCH.txz\" $VOXINDIR \
"
}

# symlinks for a global install (to be adapted according to the distro)
cd $RFSDIR
mkdir -p usr/{bin,lib,include}
ln -sf ../../$VOXINDIR/lib/libvoxin.so.$VERMAJ usr/lib/libibmeci.so
ln -sf ../../$VOXINDIR/lib/libvoxin.so.$VERMAJ usr/lib/libvoxin.so
ln -sf ../../$VOXINDIR/lib/libvoxin.so.$VERMAJ usr/lib/libvoxin.so.$VERMAJ
ln -sf ../../$VOXINDIR/include usr/include/voxin
ln -sf ../../$VOXINDIR/bin/voxin-say usr/bin/voxin-say
ln -sf ../../../../var/opt/IBM/ibmtts/cfg/eci.ini $VOXINDIR/rfs32/eci.ini

if [ -n "$RELEASE" ]; then
	mkdir -p "$RELDIR"

	# doc
	mkdir -p $DOCDIR
	cp -a "$BASE"/doc/LGPL.txt "$DOCDIR"
	touch "$DOCDIR"/list
	buildLibVoxinTarball
	tar -tf "$RELDIR/libvoxin_$VERMAJ_$VERSION.$ARCH.txz" > "$DOCDIR"/list

	fakeroot bash -c "\
tar -C \"$RFSDIR\" \
	   -Jcf \"$RELDIR/libvoxin-pkg_$VERMAJ_$VERSION.all.txz\" \
	   usr/lib/libvoxin.so usr/lib/libvoxin.so.$VERMAJ \
	   usr/lib/libibmeci.so usr/include/voxin \
	   usr/bin/voxin-say && \
tar -C \"$RFSDIR\" \
	   -Jcf \"$RELDIR/libibmeci-fake_$VERMAJ_$VERSION.all.txz\" \
	   opt/IBM
"
	buildLibVoxinTarball
	
	printf "\nTarballs available in $RELDIR\n"	
fi

