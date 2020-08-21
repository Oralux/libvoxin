
[ -z "$BASE" ] && exit 1

SRC=src/libvoxin

LIBINOTE_URL=https://github.com/Oralux/libinote/archive
# LIBINOTE_VERSION indicates the git tag of libinote to use; if unset, its git HEAD is chosen 
#LIBINOTE_VERSION=
LIBINOTE_VERSION=1.0.4
LIBINOTE_SHA512=4e5544d14289963edb6b9b2de184332a5ce31d3310fedb149027b10d5f0d0e38366c619ff9292896af562f375dbc3aef2a1f5a513363924c1fed180e99ac8d1b

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
		   		   possible value: i686; by default: current arch
-n, --no-pipe      for test: load libvoxind instead of launching voxind
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

getArch() {
    ARCH=$(uname -m)
    case "$ARCH" in
	x86|i386|i586|i686) ARCH=i686;;	
	armv7l) ARCH=armhf;;
    esac
}

getVersion() {
	local value
	local i
	for i in LIBVOXIN_VERSION_MAJOR LIBVOXIN_VERSION_MINOR LIBVOXIN_VERSION_PATCH; do
		value=$(awk "/$i/{print \$3}" src/api/voxin.h)
		eval export $i=$value
	done
	export VERSION=$LIBVOXIN_VERSION_MAJOR.$LIBVOXIN_VERSION_MINOR.$LIBVOXIN_VERSION_PATCH
}

cleanup() {
	local d=$(realpath "$BASE/build")
	local i	
	case "$d" in
		*/build) ;;
		*) exit 1;;
	esac	
	rm -rf "$BASE/build"

	for i in common libibmeci libvoxin voxind test; do
		echo "Entering $i"
		cd "$BASE/src/$i"
		make clean
	done
}

buildLibVoxinTarball() {
	fakeroot bash -c "\
tar -C \"$RFSDIR\" \
	   --exclude=libibmeci.so --exclude \"*.a\" \
	   -Jcf \"$RELDIR/libvoxin_$VERSION.$ARCH.txz\" $VOXINDIR \
"
}

# download, build and install libinote
getLibinote() {
	local workdir=$TMPDIR/libinote
	local archive	
	local archivep
	local dev
	local version
	local pwd=$PWD
	
	if [ -n "$LIBINOTE_VERSION" ]; then
		unset dev
		version=$LIBINOTE_VERSION
		archive=$version.tar.gz		
	else
		version=1.x.x
		dev=1
		archive=master.zip
#TODO		[ -e "$DWLDIR"/$archive ] && mv "$DWLDIR"/$archive "$DWLDIR"/$archive.old
	fi
	archivep="$DWLDIR/$archive"
	
	rm -rf "$workdir"
	mkdir -p "$workdir"
	cd "$workdir"
	
	# download
	if [ ! -e "$archivep" ]; then
		wget -P "$DWLDIR" "$LIBINOTE_URL/$archive"
		if [ ! -e "$archivep" ]; then
			leave "$archivep not found" 1
		fi
	fi

	if [ -n "$dev" ]; then
		[ ! -d libinote-master ] && unzip "$archivep" || true
	else
		local a=$(sha512sum "$archivep" | cut -f1 -d" ")
		if [ "$a" != "$LIBINOTE_SHA512" ]; then
			echo "$archivep: sha512 mismatch !"
			exit 1
		fi
		[ ! -d libinote-$version ] && tar -zxf "$archivep" || true
	fi	
	##
	
	unlink current 2>/dev/null || true
	if [ -n "$dev" ]; then
		dir=libinote-master
	else
		dir=libinote-$version
	fi

	[ ! -d "$dir" ] && leave "Can't cd to $dir" 1
	ln -sf "$dir" current
	cd "$pwd"
	return 0
}

buildLibinote() {
	[ "$#" = 0 ] && return 1
	local arch=$1
	local destdir=$2
	local withdbg=$3
	local workdir=$TMPDIR/libinote/current
	local flag
	local pwd=$PWD

	cd $workdir
	[ -n "$withdbg" ] && flag=-d
	./build.sh $flag -m "$arch" -i "$destdir"
}
