2.x Rebase
==========

Hello! This is the bleeding edge development branch focused on rebasing the
xqemu project onto the latest [Qemu](https://github.com/qemu/qemu) tag, which
at the time of writing is
[v2.12.0](https://github.com/qemu/qemu/tree/v2.12.0). This will bring
many years of performance enhancements to xqemu including support for native
virtualization APIs. If you are interested in helping to create a functional,
accurate, and **performant** Xbox emulator, you are **most welcome** to
contribute.

Status
------
3D graphics are now working!

![Halo](screenshot.png)

Keyboard based controller emu is now back! See below for handy controller mapping.

Chat
----
Keep up with the latest developments. Chat with us on #xqemu on irc.freenode.net or on [Discord](https://discord.gg/WxJPPyz). - [@mborgerson](https://github.com/mborgerson)

macOS Build
-----------
Use the build script to build:

	./build_macos.sh
	
Then run with something like:
	
	./i386-softmmu/qemu-system-i386 \
		-cpu pentium3 \
		-machine xbox,bootrom=$MCPX \
		-m 64 \
		-bios $BIOS \
		-drive file=$HDD,index=0,media=disk \
		-drive index=1,media=cdrom,file=$DISC \
		-monitor stdio \
		-s

This will start the Qemu monitor on the command line, which includes lots of
really helpful debugging commands!

Windows Build
-------------
You can [probably follow 
this](https://github.com/xqemu/xqemu/wiki/Getting-Started#for-windows) for installing prerequisites, then use the
`build_windows.sh` script in this tree to do the build. Luke has got it working,
you can bug him for directions :).

Debugging on macOS
------------------
If your build of Qemu is crashing, I've found it easiest to use the Xcode
debugger to look at stack traces. Fire up Xcode, create a project, edit the
"Scheme" to run the xqemu binary, then click the run button. Xcode has a nice
GUI for analyzing the stack frame and looking at local variables to quickly
track down bugs. Let me know if you find a better method. You can also attach
to running processes. - @mborgerson

Todo (barebones)
----------------
* IDE: Need to add IDE lock code

Appendix A: Keyboard-Controller Mapping
---------------------------------------

Defined in xid.c:
```
    [Q_KEY_CODE_UP]    = GAMEPAD_DPAD_UP,
    [Q_KEY_CODE_KP_8]  = GAMEPAD_DPAD_UP,
    [Q_KEY_CODE_DOWN]  = GAMEPAD_DPAD_DOWN,
    [Q_KEY_CODE_KP_2]  = GAMEPAD_DPAD_DOWN,
    [Q_KEY_CODE_LEFT]  = GAMEPAD_DPAD_LEFT,
    [Q_KEY_CODE_KP_4]  = GAMEPAD_DPAD_LEFT,
    [Q_KEY_CODE_RIGHT] = GAMEPAD_DPAD_RIGHT,
    [Q_KEY_CODE_KP_6]  = GAMEPAD_DPAD_RIGHT,

    [Q_KEY_CODE_RET]   = GAMEPAD_START,
    [Q_KEY_CODE_BACKSPACE] = GAMEPAD_BACK,

    [Q_KEY_CODE_W]     = GAMEPAD_X,
    [Q_KEY_CODE_E]     = GAMEPAD_Y,
    [Q_KEY_CODE_S]     = GAMEPAD_A,
    [Q_KEY_CODE_D]     = GAMEPAD_B,
    [Q_KEY_CODE_X]     = GAMEPAD_WHITE,
    [Q_KEY_CODE_C]     = GAMEPAD_BLACK,

    [Q_KEY_CODE_Q]     = GAMEPAD_LEFT_TRIGGER,
    [Q_KEY_CODE_R]     = GAMEPAD_RIGHT_TRIGGER,

    [Q_KEY_CODE_V]     = GAMEPAD_LEFT_THUMB,
    [Q_KEY_CODE_T]     = GAMEPAD_LEFT_THUMB_UP,
    [Q_KEY_CODE_F]     = GAMEPAD_LEFT_THUMB_LEFT,
    [Q_KEY_CODE_G]     = GAMEPAD_LEFT_THUMB_DOWN,
    [Q_KEY_CODE_H]     = GAMEPAD_LEFT_THUMB_RIGHT,

    [Q_KEY_CODE_M]     = GAMEPAD_RIGHT_THUMB,
    [Q_KEY_CODE_I]     = GAMEPAD_RIGHT_THUMB_UP,
    [Q_KEY_CODE_J]     = GAMEPAD_RIGHT_THUMB_LEFT,
    [Q_KEY_CODE_K]     = GAMEPAD_RIGHT_THUMB_DOWN,
    [Q_KEY_CODE_L]     = GAMEPAD_RIGHT_THUMB_RIGHT,
```
