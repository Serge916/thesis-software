#!/bin/bash

# Force load of tkeep handler driver, so that we don't get the pass-through driver
# probed on tkeep handler
modprobe psee-tkeep-handler
modprobe psee-event-stream-smart-tracker

# Load the FPGA
fpgautil -R
rmdir /sys/kernel/config/device-tree/overlays/*
fpgautil -b /lib/firmware/xilinx/filtered-camera-feed-firmware/filtered-camera-feed-firmware.bit.bin -o /lib/firmware/xilinx/filtered-camera-feed-firmware/filtered-camera-feed-firmware.dtbo

# Bind the drivers
echo uio_pdrv_genirq > /sys/bus/platform/devices/axi/a1000000.dma/driver_override
echo a1000000.dma > /sys/bus/platform/drivers/uio_pdrv_genirq/bind

echo uio_pdrv_genirq > /sys/bus/platform/devices/axi/400000000.filter_reg_bank/driver_override
echo 400000000.filter_reg_bank  > /sys/bus/platform/drivers/uio_pdrv_genirq/bind

# Force the camera on
echo on > /sys/class/video4linux/v4l-subdev3/device/power/control

