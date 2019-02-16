
[ -z "$BASE" ] && exit 1

SRC=src/libvoxin

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

getVersion() {
	local value
	local i
	for i in VERMAJ VERMIN VERMIC; do
		value=$(awk "/$i/{print \$3}" src/common/conf.h)
		eval export $i=$value
	done
	export VERSION=$VERMAJ.$VERMIN.$VERMIC
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
	   -Jcf \"$RELDIR/libvoxin_$VERMAJ_$VERSION.$ARCH.txz\" $VOXINDIR \
"
}

