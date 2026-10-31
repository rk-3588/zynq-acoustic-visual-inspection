SUMMARY = "Initialize OV5640 and PL video pipeline"
LICENSE = "CLOSED & OFL-1.1 & BitstreamVera"
LIC_FILES_CHKSUM = "file://font_ofl.txt;md5=9e6561baca2756628871baf0c1e23539 \
                    file://font_dejavu.txt;md5=449b2c30bfe5fa897fe87b8b70b16cfa"
inherit update-rc.d
INITSCRIPT_NAME = "video-pipeline-init"
INITSCRIPT_PARAMS = "defaults 95"
SRC_URI = "file://video_pipeline_init.c \
           file://video_init_half_clock_hflip.c \
           file://video_mixer.h \
           file://video_vdma_status.h \
           file://video_readonly_probe.h \
           file://video_status_perf.h \
           file://video_display_diag.c \
           file://video_ui_watchdog.c \
           file://video_ui_watchdog_core.h \
           file://video_mixer_ui.h \
           file://video_mixer_ui_test.c \
           file://video_status_panel.c \
           file://video_status_power.h \
           file://video_status_power_model.h \
           file://video_status_power_render.h \
           file://video_robot_service.c \
           file://video_robot_core.h \
           file://video_robot_modes_core.h \
           file://video_robot_modes.h \
           file://video_robot_protocol.h \
           file://video_status_robot.h \
           file://video_status_robot_model.h \
           file://video_status_robot_render.h \
           file://robot-control \
           file://robot-regions.conf \
           file://video_status_panel.h \
           file://video_status_render.h \
           file://video_status_temp.h \
           file://video_status_view.h \
           file://video_status_image_model.h \
           file://video_status_image_view.h \
           file://video_status_image_render.h \
           file://video_status_image.h \
           file://video_status_follow.h \
           file://video_status_touch.h \
           file://video_status_frequency.h \
           file://video_status_toolbar.h \
           file://video_status_capture.h \
           file://camera_snapshot.c \
           file://camera_snapshot_core.h \
           file://camera_album.c \
           file://camera_album_core.h \
           file://video_status_gallery.h \
           file://video_status_gallery_render.h \
           file://video_status_gallery_text.h \
           file://video-status-panel-touch.conf \
           file://video_status_font.h \
           file://font_ofl.txt \
           file://font_dejavu.txt \
           file://video-status-panel \
           file://pl_tracking.h \
           file://ov5640_720p_regs.h \
           file://video-pipeline-init"
S = "${WORKDIR}"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -O2 -Wall -Wextra -o video_pipeline_init video_init_half_clock_hflip.c
    ${CC} ${CFLAGS} ${LDFLAGS} -O2 -Wall -Wextra -o video_mixer_ui_test video_mixer_ui_test.c
    ${CC} ${CFLAGS} ${LDFLAGS} -O2 -Wall -Wextra -o video_status_panel video_status_panel.c
    ${CC} ${CFLAGS} ${LDFLAGS} -O2 -Wall -Wextra -o robot_control_daemon video_robot_service.c
    ${CC} ${CFLAGS} ${LDFLAGS} -O2 -Wall -Wextra -o camera_snapshot camera_snapshot.c
    ${CC} ${CFLAGS} ${LDFLAGS} -O2 -Wall -Wextra -o camera_album camera_album.c
    ${CC} ${CFLAGS} ${LDFLAGS} -O2 -Wall -Wextra -o video_display_diag video_display_diag.c
    ${CC} ${CFLAGS} ${LDFLAGS} -O2 -Wall -Wextra -o video_ui_watchdog video_ui_watchdog.c
}

do_install() {
    install -d ${D}${bindir} ${D}${sysconfdir}/init.d
    install -m 0755 ${S}/video_pipeline_init ${D}${bindir}/video_pipeline_init
    install -m 0755 ${S}/video_mixer_ui_test ${D}${bindir}/video_mixer_ui_test
    install -m 0755 ${S}/video_status_panel ${D}${bindir}/video_status_panel
    install -m 0755 ${S}/robot_control_daemon ${D}${bindir}/robot_control_daemon
    install -m 0755 ${S}/robot-control ${D}${sysconfdir}/init.d/robot-control
    install -m 0600 ${S}/robot-regions.conf ${D}${sysconfdir}/robot-regions.conf
    install -m 0755 ${S}/camera_snapshot ${D}${bindir}/camera_snapshot
    install -m 0755 ${S}/camera_album ${D}${bindir}/camera_album
    install -m 0755 ${S}/video_display_diag ${D}${bindir}/video_display_diag
    install -m 0755 ${S}/video_ui_watchdog ${D}${bindir}/video_ui_watchdog
    install -m 0755 ${S}/video-pipeline-init ${D}${sysconfdir}/init.d/video-pipeline-init
    install -m 0755 ${S}/video-status-panel ${D}${sysconfdir}/init.d/video-status-panel
    install -m 0644 ${S}/video-status-panel-touch.conf ${D}${sysconfdir}/video-status-panel-touch.conf
    install -d ${D}${datadir}/video-status-panel
    install -m 0644 ${S}/font_ofl.txt ${D}${datadir}/video-status-panel/fonts-OFL.txt
    install -m 0644 ${S}/font_dejavu.txt ${D}${datadir}/video-status-panel/fonts-DejaVu.txt
    # update-rc.d above owns video-pipeline-init. These second-service links
    # belong to this same package, without changing the proven video init.
    for level in 2 3 4 5; do
        install -d ${D}${sysconfdir}/rc$level.d
        ln -s ../init.d/video-status-panel ${D}${sysconfdir}/rc$level.d/S99video-status-panel
        ln -s ../init.d/robot-control ${D}${sysconfdir}/rc$level.d/S98robot-control
    done
    for level in 0 1 6; do
        install -d ${D}${sysconfdir}/rc$level.d
        ln -s ../init.d/video-status-panel ${D}${sysconfdir}/rc$level.d/K05video-status-panel
        ln -s ../init.d/robot-control ${D}${sysconfdir}/rc$level.d/K06robot-control
    done
}

FILES:${PN} += "${bindir}/video_pipeline_init ${bindir}/video_mixer_ui_test ${bindir}/video_status_panel ${sysconfdir}/init.d/video-pipeline-init ${sysconfdir}/init.d/video-status-panel ${sysconfdir}/rc[0-6].d/*video-status-panel ${datadir}/video-status-panel"
CONFFILES:${PN} += "${sysconfdir}/video-status-panel-touch.conf"
CONFFILES:${PN} += "${sysconfdir}/robot-regions.conf"
FILES:${PN} += "${bindir}/camera_snapshot"
FILES:${PN} += "${bindir}/camera_album"
FILES:${PN} += "${bindir}/video_display_diag"
FILES:${PN} += "${bindir}/video_ui_watchdog"
FILES:${PN} += "${bindir}/robot_control_daemon ${sysconfdir}/init.d/robot-control ${sysconfdir}/rc[0-6].d/*robot-control"
