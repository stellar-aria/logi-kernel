#!/bin/sh

LINUX_DEV=$1


#if problem such as "gcc-linaro-arm-linux-gnueabihf-4.7-2013.04-20130415_linux not found" occurs, locate the actual gcc toolchain
#and modify the path accordingly. Will automate this in a script later
CC_PREFIX=${LINUX_DEV}/dl/gcc-linaro-arm-linux-gnueabihf-4.7-2013.04-20130415_linux/bin/arm-linux-gnueabihf-
KERNEL_DIR=${LINUX_DEV}/KERNEL


if [ -z "${LINUX_DEV}" ]; then
	echo "usage: $0 <path-to-linux-dev>"
	exit 1
fi


cd logibone_r1
make clean
make ARCH=arm CROSS_COMPILE=${CC_PREFIX} KERNELDIR=${KERNEL_DIR}
cd ..
mkdir -p deploy
cp logibone_r1/logibone_r1_dma.ko deploy/
cp logibone_r1/setup_device-tree_R1.sh deploy/
cp logibone_r1/BB-BONE-LOGIBONE-00R1.dts deploy/
