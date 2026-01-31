#
# This file is the filtered-camera-feed-firmware recipe.
#

SUMMARY = "Simple filtered-camera-feed-firmware to use fpgamanager class"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit fpgamanager_dtg

XSA_FILE = "filtered_camera_feed.xsa"
SRC_URI = "file://${XSA_FILE}"

# Make sure the class uses the correct local XSA filename
python () {
    d.setVar("XSCTH_HDF_PATH", d.getVar("XSA_FILE"))
}
RM_WORK_EXCLUDE += "${PN}"
