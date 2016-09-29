To build idlestat natively, run 'make' from the top-level directory.

Cross Compiling for ARM
=======================

These are instructions to cross compile for ARM and ARM64 on an Ubuntu system
running x86_64.

Requirements
------------

Depending on the target platform, you will need to download either the gnueabi
or gnueabihf toolchain.

For arm, run:

 'sudo apt-get install gcc-arm-linux-gnueabi'

or

 'sudo apt-get install gcc-arm-linux-gnueabihf'

Alternatively you can also download these toolchains from Linaro at:

 http://releases.linaro.org/components/toolchain/binaries/latest-5/arm-linux-gnueabi

or

http://releases.linaro.org/components/toolchain/binaries/latest-5/arm-linux-gnueabihf

For arm64, go to:

 http://releases.linaro.org/components/toolchain/binaries/latest-5/aarch64-linux-gnu/

to download the latest aarch64 toolchain from Linaro. Then untar the toolchain
tarball.

Add the aarch64 toolchain directory path to the PATH environment variable by
running 'export PATH=$PATH:<toolchain dir/bin>'

For instance:

 'export PATH=$PATH:/home/<user>/gcc-linaro-5.3-2016.02-x86_64_aarch64-linux-gnu/bin'

Steps
------

To build for arm, run from the top-level directory:

 'make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi-' 

To build for arm64, run from the top-level directory:

 'make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-' 

Usage
------

Please refer to the README file or the idlestat manpage for details on how to
use idlestat.
