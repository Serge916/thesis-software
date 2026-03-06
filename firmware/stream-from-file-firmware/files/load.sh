#!/bin/bash

# Load the FPGA
fpgautil -R
rmdir /sys/kernel/config/device-tree/overlays/*
fpgautil -b /lib/firmware/xilinx/stream-from-file-firmware/stream-from-file-firmware.bit.bin -o /lib/firmware/xilinx/stream-from-file-firmware/stream-from-file-firmware.dtbo

# Bind the drivers
echo uio_pdrv_genirq > /sys/bus/platform/devices/axi/a0000000.dma/driver_override
echo a0000000.dma > /sys/bus/platform/drivers/uio_pdrv_genirq/bind

echo uio_pdrv_genirq > /sys/bus/platform/devices/axi/400000000.filter_reg_bank/driver_override
echo 400000000.filter_reg_bank  > /sys/bus/platform/drivers/uio_pdrv_genirq/bind