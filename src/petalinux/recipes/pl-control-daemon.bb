SUMMARY = "Acoustic camera PL control daemon"
LICENSE = "CLOSED"

SRC_URI = "file://pl_control_daemon.c file://pl_tracking.h file://pl_uio.h file://pl_enable.h"
S = "${WORKDIR}"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} ${S}/pl_control_daemon.c -o pl_control_daemon
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/pl_control_daemon ${D}${bindir}/pl_control_daemon
}

FILES:${PN} += "${bindir}/pl_control_daemon"
