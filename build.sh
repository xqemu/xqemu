#!/bin/bash
./configure \
--target-list=i386-softmmu \
--enable-hvf \
--disable-curl \
--disable-vnc \
--disable-docs \
--disable-hax \
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
