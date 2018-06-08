#!/bin/bash
set -e
set -x
./configure \
	--python=python2 \
	--enable-debug \
	--extra-cflags="-march=native -g -O0 -Wno-error -DXBOX=1" \
	--target-list=i386-softmmu \
	--enable-sdl \
	--disable-cocoa \
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
	--disable-opengl \

time make -j4 | tee build.log
