#!/bin/bash -ex

BASE=$(dirname $(realpath "$0"))
NAME=$(basename "$0")

. ./conf.inc
getVersion

ARCH=$(uname -m)
ARCHDIR="$BASE/build/$ARCH"
LOGDIR="$BASE/build/log"
RFSDIR="$ARCHDIR/test/rfs"
export DESTDIR="$RFSDIR/opt/oralux/voxin"
RFS32="$DESTDIR/rfs32"
DESTDIR_RFS32="$RFS32/usr"

mkdir -p "$LOGDIR"

usage() {
	echo "
Usage: 
 $NAME [options]

Build a dedicated directory to test locally libvoxin.
Run test, and finally delete the directory.

Options: 
-h, --help          display this help 
-b, --build <file>  build the test directory and extracts there the
                    supplied tarballs (voxin-rfs32.txz,
                    voxin-viavoice-all.txz,...)
                    <file> contains one tarball per line 
-c, --clean         clean-up: delete the dedicated directory
-g, --gdb           with gdb on libvoxin
-G, --GDB           with gdb on voxind
-s, --strace <log>  with strace using <log> as logfile prefix
                    by default the log files are created under build/log
-S, --system        test using the libvoxin library installed system wide
-t, --test <num>    run test number <num>

Example:
# build the testing directory
# in $RFSDIR
 $0 -b voxin-rfs32.txz voxin-viavoice.txz

# run test 1
 $0 -t 1

# test with strace and store log files in build/log/test1.pid
 $0 -s test1 -t1

# test using the system wide libvoxin
./test.sh -St1

# delete the directory 
 $0 -c

" 

}

unset ARCH BUILD CLEAN GDB_LIBVOXIN GDB_VOXIND HELP STRACE SYS TEST 

OPTIONS=`getopt -o b:cgGhs:St: --long build:,clean,gdb,GDB,help,strace:,system,test: \
             -n "$NAME" -- "$@"`
[ $? != 0 ] && usage && exit 1
eval set -- "$OPTIONS"

while true; do
  case "$1" in
    -h|--help) HELP=1; shift;;
    -b|--build) BUILD=$2; shift;;
    -c|--delete) CLEAN=1; shift;;
    -g|--gdb) GDB_LIBVOXIN=1; shift;;
    -G|--GDB) GDB_VOXIND=1; shift;;
    -s|--strace) STRACE=$2; shift 2;;
    -S|--system) SYS=1; shift;;
    -t|--test) TEST=$2; shift 2;;
    --) shift; break;;
    *) break;;
  esac
done

[ -z "$BUILD" ] && [ -z "$CLEAN" ] && [ -z "$TEST" ] && HELP=1

# 
# /tmp/test_voxind: voxind waits the deletion of test_voxind to continue
# /tmp/libvoxin.ok : enable libvoxin/voxind logs
# /tmp/libvoxin.log.pid: libvoxin/voxind strace logs
# /tmp/test_libvoxin.{raw,wav}: resulting audio files
#
[ -n "$HELP" ] && usage && exit 0
if [ -n "$CLEAN" ]; then
	rm -rf "$RFSDIR"
	rm -f /tmp/libvoxin* /tmp/test_voxind /tmp/test_libvoxin*
	exit 0
fi
if [ -n "$TEST" ]; then
	set +e
	rm -f /tmp/test_libvoxin* /tmp/libvoxin.log.*
	if [ -z "$SYS" ]; then
#		export PATH="$RFSDIR"/usr/bin:$PATH
		export LD_LIBRARY_PATH="$DESTDIR"/lib
		# unset pathname
		# for i in $(grep Path=/ $RFS32/eci.ini ); do
		# 	while read line; do
		# 		[ -e $line ] || pathname=$line;
		# 	done <<< "$(echo $i | sed 's+Path=/+/+')"
		# 	if [ -n "$pathname" ]; then
		# 		echo "File not found: $pathname"
		# 		exit 1
		# 	fi
		# done
	fi		
	
	pushd "$DESTDIR"/bin
	if [ -n "$STRACE" ]; then
		[ $(dirname "$STRACE") = "." ] && STRACE=$LOGDIR/$STRACE
		strace -s300 -tt -ff -o "$STRACE" ./test$TEST
#		strace -s300 -tt -ff -o /tmp/voxind -p $(pidof voxind) &
#		rm /tmp/test_voxind
	elif [ -n "$GDB_LIBVOXIN" ]; then
		gdb -ex 'set follow-fork-mode child' -ex 'b child' ./test$TEST
	elif [ -n "$GDB_VOXIND" ]; then
		touch /tmp/test_voxind
		./test$TEST &
		gdb -p $(pidof voxind)
		rm /tmp/test_voxind
	else
#		pushd "$DESTDIR"/bin
#		"$DESTDIR"/bin/test"$TEST"
		./test"$TEST"
	fi
	popd
	src/test/play.sh 	
	exit 0
fi

# BUILD
#[ ! -d "$RFSDIR" ] && mkdir -p "$RFSDIR"
rm -rf "$RFSDIR"
mkdir -p "$RFSDIR" 

touch /tmp/libvoxin.ok
rsync -av --delete "$ARCHDIR"/rfs/ "$RFSDIR"

for i in $(cat $BUILD); do
	tar -C "$RFSDIR" -xf $i
done

ECI="$RFSDIR"/var/opt/IBM/ibmtts/cfg/eci.ini
cat "$RFSDIR"/opt/IBM/ibmtts/etc/*.ini > "$ECI"
sed -i "s#=/opt/#=$RFSDIR/opt/#" "$ECI"

#sudo bash -c "echo 0 > /proc/sys/kernel/yama/ptrace_scope"

