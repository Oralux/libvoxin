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

Build libvoxin and its associated voxind binary.

Voxind will be installed in a copy of the supplied
rootfilesystem. This helps to isolate the proprietary 32 bits
text-to-speech inside a small rootfilesystem and avoid dependencies
issues with the host filesystem.

Options: 
-c, --clean		   clean-up: delete the build directory, object files
-d, --debug        compile with debug symbols 
-h, --help         display this help 
-l, --libvoxin     build libvoxin 
-m, --mach <arch>  architecture of the native binaries (libvoxin,
                   tests). arch=i386, otherwise current arch 
-r, --rfs32 <dir>  path of a 32 bits rootfilesystem
-R, --release      build the tarballs in build/arch/release:
                   - libvoxin and voxind_init:
                     libvoxin-${VERSION}-arch.tgz
                   - voxind: voxind-${VERSION}-x86_64.tgz  
                   - 32 bits rootfilesystem:
                     voxin-rfs32-${VERSION}-x86_64.tgz
-t, --test         build tests 
-v, -voxind        build voxind

Example:
# build libvoxin, voxind, then build the tarballs
 $0 -Rr ../rfs32/

# build libvoxin, voxind and tests with debug symbols
 $0 -dlvtr ../rfs32/

" 

}

unset CC CFLAGS CLEAN DBG_FLAGS HELP LIBVOXIN ARCH RELEASE RFS32REF STRIP TEST VOXIND 

OPTIONS=`getopt -o cdhlm:r:Rtv --long clean,debug,help,libvoxin,mach:,rfs32:,release,test,voxind \
             -n "$NAME" -- "$@"`
[ $? != 0 ] && usage && exit 1
eval set -- "$OPTIONS"

while true; do
  case "$1" in
    -c|--clean) CLEAN=1; shift;;
    -d|--debug) export DBG_FLAGS="-ggdb -DDEBUG"; export STRIP=test; shift;;
    -h|--help) HELP=1; shift;;
    -l|--libvoxin) LIBVOXIN=1; shift;;
    -m|--mach) ARCH=$2; shift 2;;
    -r|--rfs32) RFS32REF=$2; shift 2;;
    -R|--release) RELEASE=1; shift;;
    -t|--test) TEST=1; shift;;
    -v|--voxind) VOXIND=1; shift;;
    --) shift; break;;
    *) break;;
  esac
done

case "$ARCH" in
	i386) ;;	
	*)
		# [ -n "$DEB_HOST_ARCH" ] && ARCH="$DEB_HOST_ARCH"
		ARCH=$(uname -m);;
esac

if [ -z "$LIBVOXIN" ] && [ -z "$VOXIND" ] && [ -z "$TEST" ] && [ -z "$CLEAN" ]; then
	LIBVOXIN=1
	VOXIND=1
fi

if [ -z "$RFS32REF" ]; then
	[ -n "$VOXIND" ] && HELP=1
elif [ ! -d "$RFS32REF" ]; then
	HELP=1
else
	VOXIND=1
fi

[ -n "$HELP" ] && usage && exit 0

cleanup
[ -n "$CLEAN" ] && exit 0

cd "$BASE"
SRCDIR="$BASE/src"
ARCHDIR="$BASE/build/$ARCH"
RELDIR="$ARCHDIR/release"
RFSDIR="$ARCHDIR/rfs"
export DESTDIR="$RFSDIR/opt/voxin/$VERSION"
RFS32="$DESTDIR/rfs32"
DESTDIR_RFS32="$RFS32/usr"
IBMTTSLIB=/opt/IBM/ibmtts/lib

mkdir -p "$DESTDIR"
if [ -n "$VOXIND" ]; then
	mkdir -p "$DESTDIR_RFS32"
	cp -a "$RFS32REF"/* "$RFS32"
fi

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
if [ -n "$VOXIND" ]; then
	DESTDIR="$DESTDIR_RFS32" make clean
	DESTDIR="$DESTDIR_RFS32" CFLAGS="$CFLAGS -m32" LDFLAGS="$LDFLAGS -m32" make all
	DESTDIR="$DESTDIR_RFS32" make install
fi

make clean
make all
make install

# libvoxin
if [ -n "$LIBVOXIN" ]; then
	echo "Entering puncfilter"
	cd "$SRCDIR"/puncfilter
	make clean
	make all

	echo "Entering libvoxin"
	cd "$SRCDIR"/libvoxin
	make clean
	make all
	make install
fi

# voxind
if [ -n "$VOXIND" ]; then
	echo "Entering voxind"
	cd "$SRCDIR"/voxind
	DESTDIR=$DESTDIR_RFS32 make clean
	DESTDIR=$DESTDIR_RFS32 make all
	DESTDIR=$DESTDIR_RFS32 make install
fi

# voxind_init/test
echo "Entering voxind_init"
cd "$SRCDIR"/voxind_init
make clean
make all
make install

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
ln -s /opt/voxin/"$VERSION"/bin/voxind_init usr/bin/voxind_init
ln -s /opt/voxin/"$VERSION"/lib/libvoxin.so."$VERMAJ" usr/lib/libibmeci.so

if [ -n "$RELEASE" ]; then
	mkdir -p "$RELDIR"
	fakeroot bash -c "\
tar -C \"$RFSDIR\" -zcf \"$RELDIR/libvoxin-$VERSION-$ARCH.tgz\" usr/lib/libibmeci.so opt/voxin/\"$VERSION\"/lib/libvoxin.so* && \
tar -C \"$RFSDIR\" -zcf \"$RELDIR/voxind-$VERSION-$ARCH.tgz\" usr/bin/voxind_init opt/voxin/\"$VERSION\"/bin/voxind_init opt/voxin/\"$VERSION\"/rfs32/usr/bin/voxind && \
tar -C \"$RFSDIR\" --exclude \"libvoxin*\" --exclude \"voxind*\" -zcf \"$RELDIR/voxin-rfs32-$VERSION-$ARCH.tgz\" opt/voxin/\"$VERSION\"/rfs32"

fi

