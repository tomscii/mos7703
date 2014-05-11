Moschip 7703 usb-serial driver for Linux
========================================

To use the driver:
- clone the repo
- go to the mos7703 subdirectory
- make
- sudo make load

Current status:

DONE:
- basic cleanup & porting to latest Linux kernel (3.15.0-rc4+)
- test with standard baudrates (works as expected)
- test with high baudrates: works up till 921600 baud

TODO:
- still more cleanup (check arch dependencies, locking, etc)
- see if we can fix baudrates up to 6 MBaud (theoretical max for device)
- test, test, test (with Linux kernel debug facilities turned on)
- get it accepted into the official Linux kernel tree

ORIGIN:
I found the original driver source from Moschip on the Internet[1].
What I found is preserved exactly in the orig_7703/ subdirectory.

[1] http://driverscollection.com/?H=MCS7703&By=Moschip&SS=Linux
