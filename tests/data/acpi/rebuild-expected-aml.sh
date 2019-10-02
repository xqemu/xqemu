#!/usr/bin/env bash

#
# Rebuild expected AML files for acpi unit-test
#
# Copyright (c) 2013 Red Hat Inc.
#
# Authors:
#  Marcel Apfelbaum <marcel.a@redhat.com>
#  Igor Mammedov <imammedo@redhat.com>
#
# This work is licensed under the terms of the GNU GPLv2.
# See the COPYING.LIB file in the top-level directory.

qemu_bins="x86_64-softmmu/qemu-system-x86_64 aarch64-softmmu/qemu-system-aarch64"

if [ ! -e "tests/bios-tables-test" ]; then
    echo "Test: bios-tables-test is required! Run make check before this script."
    echo "Run this script from the build directory."
    exit 1;
fi

for qemu in $qemu_bins; do
    if [ ! -e $qemu ]; then
        echo "Run 'make' to build the following QEMU executables: $qemu_bins"
        echo "Also, run this script from the build directory."
        exit 1;
    fi
    TEST_ACPI_REBUILD_AML=y QTEST_QEMU_BINARY=$qemu tests/bios-tables-test
done

eval `grep SRC_PATH= config-host.mak`

echo '/* List of comma-separated changed AML files to ignore */' > ${SRC_PATH}/tests/bios-tables-test-allowed-diff.h

echo "The files were rebuilt and can be added to git."
