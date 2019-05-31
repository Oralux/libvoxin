#!/bin/bash -ex

BASE=$(dirname $(realpath "$0"))
NAME=$(basename "$0")

cd "$BASE"
. ./conf.inc
getVersion

ARCH=$(uname -m)
ARCHDIR="$BASE/build/$ARCH"
LOGDIR="$BASE/build/log"
RFSDIR="$ARCHDIR/test/rfs"
export DESTDIR="$RFSDIR/opt/oralux/voxin"
RFS32="$DESTDIR/rfs32"
DESTDIR_RFS32="$RFS32/usr"
DWLDIR=$BASE/download

# testFileUrl="http://abu.cnam.fr/cgi-bin/donner_unformated?nddp1"
# testFileDest=$DWLDIR/nddp1.txt.iso-8859-1
# file_utf_8=${testFileDest%.iso-8859-1}.utf-8
# getFile() {
# 	if [ ! -e "$file_utf_8" ]; then
# 		wget -O - "$testFileUrl" | sed -e 's/[\r]//g' -e "/^$/d" > "$testFileDest"
# 		iconv -f iso-8859-1 -t utf-8 -o "$file_utf_8" "$testFileDest"  
# 	fi
# }

file_utf_8=$DWLDIR/text1.utf-8
getFile() {
	cat <<EOF>"$file_utf_8"
So long as there shall exist, by virtue of law and custom, decrees of
damnation pronounced by society, artificially creating hells amid the
civilization of earth, and adding the element of human fate to divine
destiny; so long as the three great problems of the century--the
degradation of man through pauperism, the corruption of woman through
hunger, the crippling of children through lack of light--are unsolved;
so long as social asphyxia is possible in any part of the world;--in
other words, and with a still wider significance, so long as ignorance
and poverty exist on earth, books of the nature of Les Miserables
cannot fail to be of use.
 
HAUTEVILLE HOUSE, 1862.

EOF

}

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
-f, --freq			play raw audio as 22050Hz (11025Hz by default)
-g, --gdb           with gdb on libvoxin
-G, --GDB           with gdb on voxind
-p, --play          play file via voxin-say
-s, --strace <log>  with strace using <log> as logfile prefix
                    by default the log files are created under build/log
-S, --system        test using the libvoxin library installed system wide
-t, --test <num>    run test number <num>

Example:
# build the testing directory
# in $RFSDIR
 $0 -b src/list

# this 'list' file provides the paths to the necessary tarballs.
# For example to test English and French, these paths would be:
#
# /home/user1/voxin-2.0/voxin-enu-2.0/packages/all/rfs_2.0.all.txz
# /home/user1/voxin-2.0/voxin-enu-2.0/packages/all/voxin-viavoice-all_6.7-1.0-1.txz
# /home/user1/voxin-2.0/voxin-enu-2.0/packages/all/voxin-viavoice-enu_6.7-1.0-1.txz
# /home/user1/voxin-2.0/voxin-fra-2.0/packages/all/voxin-viavoice-fra_6.7-1.0-1.txz

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

unset ARCH BUILD CLEAN GDB_LIBVOXIN GDB_VOXIND HELP PLAY STRACE SYS TEST 
FREQ=11025

OPTIONS=`getopt -o b:cfgGhps:St: --long build:,clean,freq,gdb,GDB,help,play,strace:,system,test: \
             -n "$NAME" -- "$@"`
[ $? != 0 ] && usage && exit 1
eval set -- "$OPTIONS"

while true; do
  case "$1" in
    -h|--help) HELP=1; shift;;
    -b|--build) BUILD=$2; shift;;
    -c|--delete) CLEAN=1; shift;;
    -f|--freq) FREQ=22050; shift;;
    -g|--gdb) GDB_LIBVOXIN=1; shift;;
    -G|--GDB) GDB_VOXIND=1; shift;;
    -p|--play) PLAY=1; TEST=1; shift;;
    -s|--strace) STRACE=$2; shift 2;;
    -S|--system) SYS=1; shift;;
    -t|--test) TEST=$2; shift 2;;
    --) shift; break;;
    *) break;;
  esac
done

[ -z "$BUILD" ] && [ -z "$CLEAN" ] && [ -z "$TEST" ]  && HELP=1

# 
# /tmp/test_voxind: voxind waits the deletion of test_voxind to continue
# $HOME/libvoxin.ok : enable libvoxin/voxind logs
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
	rm -f /tmp/test_libvoxin* /tmp/libvoxin.log.* /tmp/libinote.log.*
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
		gdb -ex 'b inote_convert_text_to_tlv' ./test$TEST
#		gdb -ex 'set follow-fork-mode child' -ex 'b child' ./test$TEST
	elif [ -n "$GDB_VOXIND" ]; then
		#sudo bash -c "echo 0 > /proc/sys/kernel/yama/ptrace_scope"
		touch /tmp/test_voxind
		./test$TEST &
		sleep 2
		gdb -ex 'b unserialize' -p $(pidof voxind)
		rm /tmp/test_voxind
	elif [ "$PLAY" ]; then
		getFile
		./voxin-say -f "$file_utf_8" -s 500 -w "$file_utf_8.$$.wav"		

#		touch /tmp/test_voxind
#		./voxin-say -f "$file_utf_8" -s 500 -w "$file_utf_8.$$.wav" &
#		sleep 2
#		gdb -ex 'b unserialize' -p $(pidof voxind)
#		rm /tmp/test_voxind

#		gdb -ex "b eciAddText" -ex "b replayText" -ex "set args -f $file_utf_8 -s 500 -w $file_utf_8.$$.wav" ./voxin-say
		
		echo "Wav File: $file_utf_8.$$.wav"
#		aplay "$file_utf_8.$$.wav"
		exit 0
	else
		./test"$TEST"
	fi
	popd
	src/test/play.sh $FREQ 	
	exit 0
fi

# BUILD
if [ -f "$BUILD" ]; then
	#[ ! -d "$RFSDIR" ] && mkdir -p "$RFSDIR"
	rm -rf "$RFSDIR"
	mkdir -p "$RFSDIR" 
	
	touch $HOME/libvoxin.ok $HOME/libinote.ok
	rsync -av --delete "$ARCHDIR"/rfs/ "$RFSDIR"

	if [ -n "$BUILD" ]; then
		t=$(readlink -e "$BUILD") || leave "Error: the list file does not exist (-b $BUILD)" 1
		BUILD=$t
	fi
	for i in $(cat "$BUILD"); do
		tarball=$(eval echo $i)
		tar --exclude "libvoxin.so*" --exclude "voxind*" -C "$RFSDIR" -xf $tarball
	done
	
	ECI="$RFSDIR"/var/opt/IBM/ibmtts/cfg/eci.ini
	cat "$RFSDIR"/opt/IBM/ibmtts/etc/*.ini > "$ECI"
	sed -i "s#=/opt/#=$RFSDIR/opt/#" "$ECI"
fi


