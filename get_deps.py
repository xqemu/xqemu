#!/usr/bin/env python
"""
This script is used to collect all available DLLs
"""
from __future__ import print_function
import os
import os.path
import platform
import subprocess
import shutil
import sys
import argparse

def main():
	ap = argparse.ArgumentParser()
	ap.add_argument('prog')
	ap.add_argument('dest')
	args = ap.parse_args()

	if not os.path.exists(args.dest):
		os.mkdir(args.dest)
	elif not os.path.isdir(args.dest):
		print('File exists with destination name')
		sys.exit(1)

	sout = subprocess.check_output(['ldd', args.prog])
	for line in sout.splitlines():
		line = line.strip().split()
		dll_name, dll_path, dll_load_addr = line[0], line[2], line[3]
		if dll_name.startswith('???'):
			print('Unknown DLL?')
			continue
		try:
			# ldd on msys2 gives Unix-style paths, but Python wants them Windows-style
			# Try cygpath to convert them
			dll_path = subprocess.check_output(['cygpath', '-w', dll_path]).strip()
		except:
			# cygpath doesn't exist or failed. Carry on and hope for the best
			pass
		if dll_path.lower().startswith('c:\\windows'):
			print('Skipping system DLL %s' % dll_path)
			continue

		print('Copying %s...' % dll_path)
		try:
			shutil.copyfile(dll_path, os.path.join(args.dest, dll_name))
		except shutil.Error as err:
			# copyfile produces an exception if the files are the same
			print('Copying failed: %s' % err)

if __name__ == '__main__':
	main()
