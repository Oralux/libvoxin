
[ -z "$BASE" ] && exit 1

SRC=src/libvoxin

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

	for i in common libvoxin voxind test; do
		echo "Entering $i"
		cd "$BASE/src/$i"
		make clean
	done
}

