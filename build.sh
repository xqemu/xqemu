#! /usr/bin/env bash

set -e # exit if a command fails
set -o pipefail # Will return the exit status of make if it fails

user_configure_options=''
user_make_options=''
debug_preset_enabled='false'
post_build_task=''

print_help() {
	tab="$(printf '\t')"
	cat <<-EOF
	Usage: $(basename ${0}) [-t|--help] [-j<NUMBER>] [--enable-debug] [--<OPTION>]…

	${tab}-h, --help
	${tab}${tab}print this help

	${tab}-j<NUMBER>, --jobs=<NUMBER>
	${tab}${tab}set make job number

	${tab}--enable-debug
	${tab}${tab}enable debug build

	${tab}--<OPTION>
	${tab}${tab}set configure option, see below

	$(./configure --help | grep '^Usage: ' -A9999 | tail -n +2)
	EOF
	exit
}

print_error() {
	printf 'ERROR: %s\n' "${1}" >&2
	exit 1
}

package_windows() { # Script to prepare the windows exe
	mkdir -p dist
	cp i386-softmmu/qemu-system-i386.exe dist/xqemu.exe
	cp i386-softmmu/qemu-system-i386w.exe dist/xqemuw.exe
	python2 ./get_deps.py dist/xqemu.exe dist
	strip dist/xqemu.exe
	strip dist/xqemuw.exe
}

if echo "${@}" | tr ' ' '\n' | grep -q '^-h$\|--help$'
then
	print_help
fi

while [ ! -z "${1}" ]
do
	case "${1}" in
	'-j'*|'--jobs='*)
		user_make_options="${user_make} ${1}"
		;;
	'--enable-debug')
		user_configure_options="${user_configure_options} ${1}"
		debug_preset_enabled='true'
		;;
	'--'*)
		user_configure_options="${user_configure_options} ${1}"
		;;
	*)
		print_error "unknown option: ${1}"
		;;
	esac
	shift
done

if "${debug_preset_enabled}"
then
	preset_build_cflags='-O0 -g'
else
	preset_build_cflags='-O3'
fi

case "$(uname -s)" in # adjust compilation option based on platform
	Linux)
		echo 'Compiling for Linux…'
		system_build_cflags='-march=native -Wno-error=redundant-decls -Wno-error=unused-but-set-variable'
		system_configure_options='--enable-kvm --disable-xen --disable-werror'
		;;
	Darwin)
		echo 'Compiling for MacOS…'
		system_build_cflags='-march=native'
		system_configure_options='--disable-cocoa'
		;;
	CYGWIN*|MINGW*|MSYS*)
		echo 'Compiling for Windows…'
		system_build_cflags='-Wno-error'
		system_configure_options='--python=python2 --disable-cocoa --disable-opengl'
		post_build_task='package_windows' # set the above function to be called after build
		;;
	*)
		print_error "could not detect OS $(uname -s), aborting"
		;;
esac

if command -v 'nproc' >/dev/null
then
	job_count="$(nproc)"
elif command -v 'sysctl' >/dev/null
then
	job_count="$(sysctl -n hw.ncpu)"
else
	job_count='4'
fi

set -x # Print commands from now on

./configure \
	--extra-cflags="-DXBOX=1 ${preset_build_cflags} ${system_build_cflags} ${CFLAGS}" \
	${system_configure_options} \
	--target-list=i386-softmmu \
	--enable-sdl \
	--with-sdlabi=2.0 \
	--disable-curl \
	--disable-vnc \
	--disable-docs \
	--disable-tools \
	--disable-guest-agent \
	--disable-tpm \
	--disable-live-block-migration \
	--disable-replication \
	--disable-capstone \
	--disable-fdt \
	--disable-libiscsi \
	--disable-spice \
	--disable-user \
	--disable-stack-protector \
	${user_configure_options}

time make -j"${job_count}" ${user_make_options} 2>&1 | tee build.log

${post_build_task} # call post build functions

