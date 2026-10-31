SUMMARY = "Manual frame-boundary video mode and threshold control via verified UIO"
LICENSE = "CLOSED"
SRC_URI = "file://video_preprocess_control.c file://vpp_control_logic.h file://video_preprocess_regs.h"
S = "${WORKDIR}"

do_compile() {
    ${CC} ${CFLAGS} -O2 -Wall -Wextra -Werror video_preprocess_control.c -o video_preprocess_control ${LDFLAGS}
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 video_preprocess_control ${D}${bindir}/video_preprocess_control
}
