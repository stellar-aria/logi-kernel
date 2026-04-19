# logi-kernel

Linux kernel-side support for LogiBone on BeagleBone Black.

## Current Scope

- Active target: BeagleBone Black + LogiBone R1
- Active code path: [beaglebone-black/logibone_r1](beaglebone-black/logibone_r1)
- Supported workflow: Linux 6.x (Debian 13.x tested)

This repository has been pared down to the maintained R1 path.

## Quick Start

Use the modern setup guide:

- [docs/bbb-setup.md](docs/bbb-setup.md)

Typical build flow:

```bash
cd beaglebone-black
make
```

Artifacts are generated in [beaglebone-black/out](beaglebone-black/out).

## Repository Layout

- [beaglebone-black/common](beaglebone-black/common): shared driver code
- [beaglebone-black/logibone_r1](beaglebone-black/logibone_r1): R1 module + DTS overlay sources
- [beaglebone-black/KERNEL](beaglebone-black/KERNEL): kernel patch/staging content
- [docs](docs): setup and operational docs

## Notes

- Overlay loading uses configfs on modern kernels (not legacy cape manager slots).
- Both DMA and non-DMA module variants are built in the current flow.
