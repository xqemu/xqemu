2.x Rebase
==========

Hello! This is the bleeding edge development branch focused on rebasing the
xqemu project onto the latest [Qemu](https://github.com/qemu/qemu) tag, which
at the time of writing is
[v2.12.0-rc1](https://github.com/qemu/qemu/tree/v2.12.0-rc1). This will bring
many years of performance enhancements to xqemu including support for native
virtualization APIs. If you are interested in helping to create a functional,
accurate, and **performant** Xbox emulator, you are **most welcome** to
contribute.

Status
------
Booting to kernel! The very basics are ported. Everything should (hopefully)
require minimal effort to get working. See TODO list and add to it if I've
forgotton something. Cromwell is booting! Getting 3D graphics running is now
top priority.

![Cromwell](https://cdn.discordapp.com/attachments/428359372842991616/429166779651325973/Screen_Shot_2018-03-29_at_11.34.46_PM.png)

Chat
----
Keep up with the latest developments. Chat with us on #xqemu on irc.freenode.net or on [Discord](https://discord.gg/WxJPPyz). - [@mborgerson](https://github.com/mborgerson)

macOS Dev
---------
Use the build script to build:

	./build_macos.sh
	
Then run with something like:
	
	./i386-softmmu/qemu-system-i386 \
		-cpu pentium3 \
		-machine xbox,bootrom=$MCPX \
		-m 64 \
		-bios $BIOS \
		-drive file=$HDD,index=0,media=disk \
		-monitor stdio \
		-s

This will start the Qemu monitor on the command line, which includes lots of
really helpful debugging commands!

### Debugging

If your build of Qemu is crashing, I've found it easiest to use the Xcode
debugger to look at stack traces. Fire up Xcode, create a project, edit the
"Scheme" to run the xqemu binary, then click the run button. Xcode has a nice
GUI for analyzing the stack frame and looking at local variables to quickly
track down bugs. Let me know if you find a better method. You can also attach
to running processes. - @mborgerson

Todo (barebones)
----------------
* IDE: Need to add IDE lock code
* USB: Needs testing
* NV2A: 3d graphics not working yet
