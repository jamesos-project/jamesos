SUMMARY = "JamesOS keep: puts JamesOS settings back after Clear Out Soul and keeps the badges"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI = "file://jamesos-keep.service"
S = "${UNPACKDIR}"
do_install() {
    install -d ${D}${sysconfdir}/systemd/system/multi-user.target.wants
    install -m 0644 ${UNPACKDIR}/jamesos-keep.service ${D}${sysconfdir}/systemd/system/jamesos-keep.service
    ln -sf ../jamesos-keep.service ${D}${sysconfdir}/systemd/system/multi-user.target.wants/jamesos-keep.service
}
FILES:${PN} = "${sysconfdir}/systemd/system"
