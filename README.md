Moschip MCS7703 usb-serial driver for Linux
===========================================

I found the originally imported driver source on the Internet[1].
What I found is preserved in the orig_7703/ subdirectory.

As the driver is not up-to-date with current Linux kernels (does not
even compile), I intend to actualise it and clean it up (also get rid
of the highly unprofessional nature of some parts of the code).

Once it compiles and loads into the kernel, I will be able to test it,
since I happen to have 2 identical single-port devices at hand (could
never make use of these, apparently there is no driver for Windows7
either). Hopefully it will work. The end goal is getting the driver
accepted into the Linux kernel tree.

[1] http://driverscollection.com/?H=MCS7703&By=Moschip&SS=Linux
