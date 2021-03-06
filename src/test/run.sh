#!/bin/bash 

NBARGS=$#

usage() {
	echo -e "usage: run.sh [-g|-v] <test_number>\n" \
		 "\t-g: gdb\n" \
		 "\t-v: valgrind"
}


unset cmd
while getopts :gv opt; do
      case $opt in
	  g) cmd=gdb;;
	  v) cmd="valgrind -v --leak-check=full --show-leak-kinds=all --track-origins=yes";;
	  *)
		  usage
	      exit 1;;
      esac
done

shift $(($OPTIND - 1))
TEST_NUMBER=$1

[ -z "$TEST_NUMBER" ] && usage && exit 1


rm -f /tmp/libvoxin.log* /tmp/test_libvoxin*wav /tmp/test_libvoxin*raw
echo 2 > /tmp/libvoxin.ok
cd ../../build/x86_64/test/rfs/opt/oralux/voxin/bin
ln -sf test$TEST_NUMBER client
export LD_LIBRARY_PATH=../lib
#export PATH=.:$PATH
$cmd ./test$TEST_NUMBER
RES=$?
if [ "$RES" = "0" ]; then
    echo -e "$TEST_NUMBER\t[OK]"
else
    echo -e "$TEST_NUMBER\t[KO] ($RES)"
fi
# ls /tmp/libvoxin.log.*

exit $RES
