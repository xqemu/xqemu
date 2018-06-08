#!/bin/bash
set -e
set -x
./configure \
	--enable-debug \
	--extra-cflags="-march=native -g -O0 -Wno-error=redundant-decls -Wno-error=unused-but-set-variable -DXBOX=1" \
	--disable-werror \
	--target-list=i386-softmmu \
	--enable-sdl \
	--enable-kvm \
	--disable-xen \
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

time make -j4 | tee build.log

