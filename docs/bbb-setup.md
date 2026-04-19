# BeagleBone Black Setup (Linux 6.x)

This guide applies to Debian 13.x with a 6.x kernel.

## 1. Build module + overlay artifacts

From the repository root:

```bash
cd beaglebone-black
make
```

Artifacts are staged in:

- `beaglebone-black/out/logibone_r1_dm.ko`
- `beaglebone-black/out/logibone_r1_dma.ko`
- `beaglebone-black/out/BB-BONE-LOGIBONE-00R1.dts`
- `beaglebone-black/out/BB-BONE-LOGIBONE-00R1.dtbo`

If `/lib/modules/$(uname -r)/build` is missing, build with:

```bash
make KERNELDIR=/usr/src/linux-headers-$(uname -r)
```

## 2. Install and load overlay with configfs

```bash
sudo cp beaglebone-black/out/BB-BONE-LOGIBONE-00R1.dtbo /lib/firmware/
sudo mkdir -p /sys/kernel/config/device-tree/overlays/logibone
sudo cp /lib/firmware/BB-BONE-LOGIBONE-00R1.dtbo /sys/kernel/config/device-tree/overlays/logibone/dtbo
```

To auto-load on boot, add this line to `/boot/uEnv.txt`:

```ini
dtb_overlay=/lib/firmware/BB-BONE-LOGIBONE-00R1.dtbo
```

## 3. Load and test the non-DMA module

```bash
sudo insmod beaglebone-black/out/logibone_r1_dm.ko
ls -l /dev/logibone /dev/logibone_mem
```

## 4. Load and test the DMA module

The DMA variant probes from device tree and requires this compatible string in the overlay target node:

```dts
compatible = "logibone_ra1";
```

Then load:

```bash
sudo insmod beaglebone-black/out/logibone_r1_dma.ko
ls -l /dev/logibone /dev/logibone_mem
```

## 5. Notes

- The old `bone_capemgr/slots` mechanism is deprecated and not used.
- Overlay source now lives at `beaglebone-black/logibone_r1/BB-BONE-LOGIBONE-00R1.dts`.
