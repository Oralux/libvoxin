#!/bin/bash -e

BASE=$(dirname $(realpath "$0"))
NAME=$(basename "$0")

. ./conf.inc
getVersion

ARCH=$(uname -m)
SRCDIR="$BASE/src"
ARCHDIR="$BASE/build/$ARCH"
RFSDIR="$ARCHDIR/test/rfs"
export DESTDIR="$RFSDIR/opt/voxin/$VERSION"
RFS32="$DESTDIR/rfs32"
DESTDIR_RFS32="$RFS32/usr"
IBMTTSLIB=/opt/IBM/ibmtts/lib
SRC32DIR=$DESTDIR_RFS32/src/libvoxin

usage() {
	echo "
Usage: 
 $NAME [options]

Build a dedicated directory to test locally libvoxin.
Run test, and finally delete the directory.

Options: 
-h, --help          display this help 
-b, --build         build the directory
-c, --clean         clean-up: delete the dedicated directory
-s, --strace <log>  with strace using <log> as logfile prefix
-t, --test <num>    run test number <num>

Example:
# build the dedicated directory in
# $RFSDIR
 $0 -b

# run test 1
 $0 -t 1

# delete the directory 
 $0 -c

" 

}

# umountAll() {
# 	local rfs32
# 	local dir
# 	if [ -d "$BASE/build" ]; then
# 		rfs32=$(find $BASE/build -mindepth 7 -maxdepth 7 -path "*/rfs32" -type d)
# 		for dir in /dev/pts /dev /sys /proc; do
# 			mountpoint -q $rfs32/$dir && sudo umount $rfs32/$dir || true
# 		done
# 	fi
# }

unset HELP ARCH BUILD CLEAN STRACE TEST 

OPTIONS=`getopt -o bchs:t: --long build,clean,help,strace:,test: \
             -n "$NAME" -- "$@"`
[ $? != 0 ] && usage && exit 1
eval set -- "$OPTIONS"

while true; do
  case "$1" in
    -h|--help) HELP=1; shift;;
    -b|--build) BUILD=1; shift;;
    -c|--delete) CLEAN=1; shift;;
    -s|--strace) STRACE=$2; shift 2;;
    -t|--test) TEST=$2; shift 2;;
    --) shift; break;;
    *) break;;
  esac
done

[ -z "$BUILD" ] && [ -z "$CLEAN" ] && [ -z "$TEST" ] && HELP=1

[ -n "$HELP" ] && usage && exit 0
if [ -n "$CLEAN" ]; then
	sudo rm -rf "$RFSDIR"
	exit 0
fi
if [ -n "$TEST" ]; then
	pushd $DESTDIR/bin
	export LD_LIBRARY_PATH=../lib
	export PATH=.:$PATH
	sudo rm -f "$RFS32"/tmp/test_voxind
	set +e
	if [ -n "$STRACE" ]; then
		touch "$RFS32"/tmp/test_voxind
		./test"$TEST" &
		sudo chroot "$RFS32" /bin/sh -c "strace -s300 -tt -ff -o /tmp/strace -p $(pidof voxind)" &
		sudo rm "$RFS32"/tmp/test_voxind
	else
	rm -f /tmp/test_libvoxin*
		./test"$TEST"
	fi
	popd
	src/test/play.sh 	
	exit 0
fi

[ ! -d "$RFSDIR" ] && mkdir -p "$RFSDIR"

rm -f /tmp/libvoxin.log.*
touch /tmp/libvoxin.ok

sudo bash -c " \
rsync -av --no-o --no-g --exclude src --delete \"$ARCHDIR\"/rfs/ \"$RFSDIR\";	\
[ -d \"$SRC32DIR\" ] || mkdir -p \"$SRC32DIR\";	  	\
rsync -av --no-o --no-g --exclude .git --exclude \"*o\" --delete src/ \"$SRC32DIR\";	 \
touch \"$RFS32\"/tmp/test_voxind \"$RFS32\"/tmp/libvoxin.ok;	\
rm -f \"$RFS32\"/tmp/libvoxin.log.* "

#sudo bash -c "echo 0 > /proc/sys/kernel/yama/ptrace_scope"
# for i in /dev /sys /proc /dev/pts; do
# 	sudo mount -o bind $i $RFS32$i
# done	

