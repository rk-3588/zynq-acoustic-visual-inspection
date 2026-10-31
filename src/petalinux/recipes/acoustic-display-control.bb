SUMMARY = "Quiet gate and adaptive log-color control for acoustic overlay"
LICENSE = "CLOSED"
inherit update-rc.d
INITSCRIPT_NAME = "acoustic-display-control"
INITSCRIPT_PARAMS = "defaults 99"
SRC_URI = "file://acoustic_display_control.c \
           file://acoustic_display_logic.h \
           file://pl_enable.h file://pl_tracking.h \
           file://acoustic-display.conf \
           file://acoustic-display-control"
S = "${WORKDIR}"
do_compile() {
    ${CC} ${CFLAGS} -O2 -Wall -Wextra -o acoustic_display_control acoustic_display_control.c ${LDFLAGS} -lm
}
do_install() {
    install -d ${D}${bindir} ${D}${sysconfdir}/init.d
    install -m 0755 acoustic_display_control ${D}${bindir}/acoustic_display_control
    install -m 0755 acoustic-display-control ${D}${sysconfdir}/init.d/acoustic-display-control
    install -m 0644 acoustic-display.conf ${D}${sysconfdir}/acoustic-display.conf
}
CONFFILES:${PN} = "${sysconfdir}/acoustic-display.conf"
FILES:${PN} += "${bindir}/acoustic_display_control ${sysconfdir}/init.d/acoustic-display-control ${sysconfdir}/acoustic-display.conf"
