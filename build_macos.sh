#!/bin/bash
./configure \
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

time make -j4 | tee build.log
