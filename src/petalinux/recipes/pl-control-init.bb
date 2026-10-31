SUMMARY = "Start acoustic camera PL control daemon"
LICENSE = "CLOSED"
inherit update-rc.d

INITSCRIPT_NAME = "pl-control-daemon"
INITSCRIPT_PARAMS = "start 40 2 3 4 5 . stop 90 0 1 6 ."

SRC_URI = "file://pl-control-init"
S = "${WORKDIR}"

do_install() {
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${S}/pl-control-init ${D}${sysconfdir}/init.d/pl-control-daemon
}

FILES:${PN} += "${sysconfdir}/init.d/pl-control-daemon"
