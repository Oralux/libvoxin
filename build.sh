#!/bin/bash -ex

BASE=$(dirname $(realpath "$0"))
NAME=$(basename "$0")

cd "$BASE"
. ./conf.inc
getVersion
[ -z "$LIBVOXIN_VERSION_MAJOR" ] && exit 1

unset ARCH CC CFLAGS CLEAN DBG_FLAGS HELP RELEASE STRIP TEST WITH_DBG

OPTIONS=`getopt -o cdhm:rt --long clean,debug,help,mach:,release,test \
             -n "$NAME" -- "$@"`
[ $? != 0 ] && usage && exit 1
eval set -- "$OPTIONS"

while true; do
  case "$1" in
    -c|--clean) CLEAN=1; shift;;
    -d|--debug) WITH_DBG=1; shift;;
    -h|--help) HELP=1; shift;;
    -m|--mach) ARCH=$2; shift 2;;
    -r|--release) RELEASE=1; shift;;
    -t|--test) TEST=1; shift;;
    --) shift; break;;
    *) break;;
  esac
done

if [ -n "$WITH_DBG" ]; then
	export DBG_FLAGS="$DBG_FLAGS -ggdb -DDEBUG"
	export STRIP=test;
fi

getArch

[ -n "$HELP" ] && usage && exit 0

cd "$BASE"
SRCDIR="$BASE/src"
ARCHDIR="$BASE/build/$ARCH"
TMPDIR=$BASE/build/tmp
DWLDIR=$BASE/download
RELDIR="$ARCHDIR/release"
RFSDIR="$ARCHDIR/rfs"
SOUNDS_DIR="$BASE/sounds"
VOXINDIR=opt/oralux/voxin
export DESTDIR="$RFSDIR/$VOXINDIR"
RFS32="$DESTDIR/rfs32"
DESTDIR_RFS32="$RFS32/usr"
IBMTTSDIR=$RFSDIR/opt/IBM/ibmtts
DOCDIR=$DESTDIR/share/doc/libvoxin

cleanup
[ -n "$CLEAN" ] && exit 0

mkdir -p "$DESTDIR_RFS32"

export CFLAGS="$DBG_FLAGS"
export CXXFLAGS="$DBG_FLAGS"
unset LDFLAGS
if [ "$ARCH" = "i686" ]; then
	export CFLAGS="$CFLAGS -m32 -I/usr/i686-linux-gnu/include"
	export CXXFLAGS="$CXXFLAGS -m32 -I/usr/i686-linux-gnu/include"
	export LDFLAGS="-m32"
fi


#libinih
getInih
buildInih $ARCH "$DESTDIR" "$WITH_DBG"

#libinote
getLibinote
buildLibinote $ARCH "$DESTDIR" "$WITH_DBG"
case $ARCH in
    aarch64|arm*) ;;
    *) buildLibinote i686 "$DESTDIR_RFS32" "$WITH_DBG";;
esac

# add access to inote.h
CFLAGS="$CFLAGS -I$DESTDIR/include"

# libcommon
echo "Entering common"
cd "$SRCDIR"/common
case $ARCH in
    aarch64|arm*) ;;
    *) 
	DESTDIR="$DESTDIR_RFS32" make clean
	DESTDIR="$DESTDIR_RFS32" CFLAGS="$CFLAGS -m32 -I/usr/i686-linux-gnu/include" LDFLAGS="-m32" make all
	DESTDIR="$DESTDIR_RFS32" make install
	;;
esac
make clean
make all
make install

# libvoxin
echo "Entering libvoxin"
cd "$SRCDIR"/libvoxin
make clean
make all
make install

echo "Entering api"
cd "$SRCDIR"/api
make clean
make install

# voxind
case $ARCH in
    aarch64|arm*) ;;
    *) 
	# libibmeci (moc)
	echo "Entering libibmeci"
	cd "$SRCDIR"/libibmeci
	DESTDIR=$IBMTTSDIR make clean
	DESTDIR=$IBMTTSDIR make all
	DESTDIR=$IBMTTSDIR make install

	echo "Entering voxind"
	cd "$SRCDIR"/voxind
	DESTDIR=$DESTDIR_RFS32 IBMTTSDIR=$IBMTTSDIR make clean
	DESTDIR=$DESTDIR_RFS32 IBMTTSDIR=$IBMTTSDIR make all
	DESTDIR=$DESTDIR_RFS32 IBMTTSDIR=$IBMTTSDIR make install
	;;
esac

# sounds
echo "Entering sounds"
cd "$SOUNDS_DIR"
DESTDIR=$DESTDIR make install

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
cd "$RFSDIR"
mkdir -p usr/{bin,lib,include}
ln -sf ../../$VOXINDIR/lib/libvoxin.so.$LIBVOXIN_VERSION_MAJOR usr/lib/libibmeci.so
ln -sf ../../$VOXINDIR/lib/libvoxin.so.$LIBVOXIN_VERSION_MAJOR usr/lib/libvoxin.so
ln -sf ../../$VOXINDIR/lib/libvoxin.so.$LIBVOXIN_VERSION_MAJOR usr/lib/libvoxin.so.$LIBVOXIN_VERSION_MAJOR
ln -sf ../../$VOXINDIR/include usr/include/voxin
ln -sf ../../$VOXINDIR/bin/voxin-say usr/bin/voxin-say

case $ARCH in
    aarch64|arm*) unset LIBIBMECI;;
    *) LIBIBMECI=usr/lib/libibmeci.so
	ln -sf ../../../../var/opt/IBM/ibmtts/cfg/eci.ini $VOXINDIR/rfs32/eci.ini
       ;;
esac
if [ -n "$RELEASE" ]; then
	mkdir -p "$RELDIR"

	# doc
	mkdir -p $DOCDIR
	cp -a "$BASE"/doc/LGPL.txt "$DOCDIR"
	touch "$DOCDIR"/list
	buildLibVoxinTarball
	tar -tf "$RELDIR/libvoxin_$VERSION.$ARCH.txz" > "$DOCDIR"/list

	fakeroot bash -c "\
tar -C \"$RFSDIR\" \
	   -Jcf \"$RELDIR/libvoxin-pkg_$VERSION.all.txz\" \
	   usr/lib/libvoxin.so usr/lib/libvoxin.so.$LIBVOXIN_VERSION_MAJOR \
	   $LIBIBMECI usr/include/voxin \
	   usr/bin/voxin-say
"
	[ -n "$LIBIBMECI" ] && fakeroot bash -c "\
tar -C \"$RFSDIR\" \
	   -Jcf \"$RELDIR/libibmeci-fake_$VERSION.all.txz\" \
	   opt/IBM
"

	buildLibVoxinTarball
	
	printf "\nTarballs available in $RELDIR\n"	
fi

