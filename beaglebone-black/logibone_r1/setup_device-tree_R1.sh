#!/bin/sh

dtc -O dtb -o BB-BONE-LOGIBONE-00R1.dtbo -b 0 -@ BB-BONE-LOGIBONE-00R1.dts
cp -f BB-BONE-LOGIBONE-00R1.dtbo /lib/firmware

mkdir -p /sys/kernel/config/device-tree/overlays/logibone
cp -f BB-BONE-LOGIBONE-00R1.dtbo /sys/kernel/config/device-tree/overlays/logibone/dtbo

ls -l /sys/kernel/config/device-tree/overlays/logibone

insmod logibone_r1_dma.ko
