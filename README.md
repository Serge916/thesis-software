# SpikeVision Software

This repository contains the different custom modules, apps and firmware developed on top of Prophesee's IMX636 Kria KV260 petalinux image

## Organization

- `Firmware` contains PL binaries and device-tree overlays. Every firmware instance can be loaded at run time. Check details below.
- `Modules` contains kernel modules.
- `Apps` contains user-space apps.

#### Create firmware

In Vivado, export hardware with bitstream. The output is a .xsa file.
In the petalinux project directory, you can create a firmware app by running this command:

```bash
petalinux-create -t apps --template fpgamanager_dtg -n <app-name>-firmware --enable --srcuri <../path/to/xsa/file>
```

The generated recipe in stored in: _project-spec/meta-user/recipes-apps/user-firmware/user-firmware.bb_
If the _RM_WORK_EXCLUDE += "${PN}"_ variable is included in the recipe, the .dtsi file should be found at: _tmpworkspace/work/xilinx_k26_kv-xilinx-linux/app-name-firmware/1.0-r0/build/app-name-firmware/psu_cortexa53_0/device_tree_domain/bsp/pl.dtsi_
The generated .dtbo (final file used to program the FPGA), is found at: _$tmp_folder/sysroots-components/zynqmp_generic/user-firmware/lib/firmware/xilinx/user-firmware/user-firmware.dtbo_

### Create kernel module

```bash
$ petalinux-create -t modules --name mymodule --enable
```

After build, these are stored in build/tmp/sysroots-components/xilinx_k26_kv/module-name

### Create user space app

```bash
petalinux-create -t apps --name <app-name> --template c --enable
```

After build, these are stored in build/tmp/work/cortexa72-cortexa53-xilinx-linux/loopback-app/1.0-r0/loopback-app

### Load FPGA and overlay

```bash
fpgautil -b /lib/firmware/xilinx/loopback-firmware/loopback-firmware.bit.bin -o /lib/firmware/xilinx/loopback-firmware/loopback-firmware.dtbo
```

You can check the errors in dmesg

```bash
dmesg | tail -n 200 | grep -i -E "overlay|dtbo|fpga|fpgautil|of:|resolver|fixup"
```

In case the region was already applied, remove it/them

```bash
ls -1 /sys/kernel/config/device-tree/overlays
rmdir /sys/kernel/config/device-tree/overlays/NAME
```

### Change driver binding

To use u-dma-buf, the AXI DMA device has to be binded to `generic-uio`. However, changing the compatibility on a node that is created in an overlay proved hard. Therefore, for now, I am changing the binding manually at run time. This is not very robust, as changing the XSA might also change the address mapping of the IP. Keep it in mind.

```bash
echo a0000000.dma > /sys/bus/platform/drivers/xilinx-vdma/unbind
echo uio_pdrv_genirq > /sys/bus/platform/devices/axi/a0000000.dma/driver_override
echo a0000000.dma > /sys/bus/platform/drivers/uio_pdrv_genirq/bind
```

## Building

Any of these can be individually built and copied into the Linux system. In case you want to build the whole image.

```bash
petalinux-build
petalinux-package --wic --bootfiles "ramdisk.cpio.gz.u-boot,boot.scr,Image,system.dtb,system-zynqmp-sck-kv-g-revB.dtb"
```

And then flash it onto the SD card with

```bash
sudo dd bs=4M if=images/linux/petalinux-sdimage.wic of=/dev/sdb status=progress && sync
```
