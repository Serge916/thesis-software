#
# This file is the filtered-camera-feed-firmware recipe.
#

SUMMARY = "Simple filtered-camera-feed-firmware to use fpgamanager class"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit fpgamanager_dtg

XSA_FILE = "filtered-camera-feed.xsa"
SRC_URI = "file://${XSA_FILE} \
	file://load.sh \
	file://pl-filtered-camera.dtsi"

RDEPENDS:${PN} += "bash"

# Make sure the class uses the correct local XSA filename
python () {
    d.setVar("XSCTH_HDF_PATH", d.getVar("XSA_FILE"))
}

do_install:append () {
	install -d ${D}/${bindir}
	install -m 0700 ${WORKDIR}/load.sh ${D}/${bindir}/load-${PN}.sh
}
FILES:${PN} += "${bindir}/load-${PN}.sh"

RM_WORK_EXCLUDE += "${PN}"
