#!/bin/bash
: ${FORMAT:="PSEE_EVT21"}
# Force load of tkeep handler driver, so that we don't get the pass-through driver
# probed on tkeep handler
modprobe psee-tkeep-handler
modprobe psee-event-stream-smart-tracker

# Load the FPGA
fpgautil -R
rmdir /sys/kernel/config/device-tree/overlays/*
fpgautil -b /lib/firmware/xilinx/filtered-camera-feed-firmware/filtered-camera-feed-firmware.bit.bin -o /lib/firmware/xilinx/filtered-camera-feed-firmware/filtered-camera-feed-firmware.dtbo

# Bind the drivers
echo uio_pdrv_genirq > /sys/bus/platform/devices/axi/a0060000.dma/driver_override
echo a0060000.dma > /sys/bus/platform/drivers/uio_pdrv_genirq/bind

echo uio_pdrv_genirq > /sys/bus/platform/devices/axi/400000000.filter_reg_bank/driver_override
echo 400000000.filter_reg_bank  > /sys/bus/platform/drivers/uio_pdrv_genirq/bind

# Give some room for drivers to probe
sleep 2

# Load the media pipeline
media-ctl -V "'imx636 6-003c':0[fmt:$FORMAT/1280x720]"
media-ctl -V "'a0010000.mipi_csi2_rx_subsystem':1[fmt:$FORMAT/1280x720]"
media-ctl -V "'a0040000.axis_tkeep_handler':1[fmt:$FORMAT/1280x720]"
media-ctl -V "'a0050000.event_stream_smart_tra':1[fmt:$FORMAT/1280x720]"

export V4L2_HEAP=reserved
export V4L2_SENSOR_PATH=/dev/v4l-subdev3
# Force the camera on
echo on > /sys/class/video4linux/v4l-subdev3/device/power/control
