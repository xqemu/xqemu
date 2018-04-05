#!/bin/bash
./configure \
	--enable-debug \
	--extra-cflags="-march=native -g3 -O0 -Wno-error" \
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
