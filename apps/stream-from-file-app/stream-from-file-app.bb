#
# This file is the stream-from-file-app recipe.
#

SUMMARY = "Simple stream-from-file-app application"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://stream-from-file-app.c \
	   file://Makefile \
	   file://dma-api.c \
	   file://dma-api.h \
	   file://helper.h \
	   file://helper.c \
		  "

S = "${WORKDIR}"

do_compile() {
	     oe_runmake
}

do_install() {
	     install -d ${D}${bindir}
	     install -m 0755 stream-from-file-app ${D}${bindir}
}
