SUMMARY = "Single-channel acoustic FFT diagnostic and UI observation feed"
LICENSE = "CLOSED"
SRC_URI = "file://acoustic_frequency.c file://audio_fft.h file://acoustic_frequency_feed.c file://acoustic-frequency-feed"
S = "${WORKDIR}"
do_compile() {
    ${CC} ${CFLAGS} -O2 -Wall -Wextra -Werror acoustic_frequency.c -o acoustic_frequency ${LDFLAGS} -lm
    ${CC} ${CFLAGS} -O2 -Wall -Wextra -Werror acoustic_frequency_feed.c -o acoustic_frequency_feed ${LDFLAGS}
}
do_install() {
    install -d ${D}${bindir}
    install -m 0755 acoustic_frequency ${D}${bindir}/acoustic_frequency
    install -m 0755 acoustic_frequency_feed ${D}${bindir}/acoustic_frequency_feed
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 acoustic-frequency-feed ${D}${sysconfdir}/init.d/acoustic-frequency-feed
    for level in 2 3 4 5; do
        install -d ${D}${sysconfdir}/rc$level.d
        ln -s ../init.d/acoustic-frequency-feed ${D}${sysconfdir}/rc$level.d/S99acoustic-frequency-feed
    done
    for level in 0 1 6; do
        install -d ${D}${sysconfdir}/rc$level.d
        ln -s ../init.d/acoustic-frequency-feed ${D}${sysconfdir}/rc$level.d/K04acoustic-frequency-feed
    done
}
FILES:${PN} += "${sysconfdir}/init.d/acoustic-frequency-feed ${sysconfdir}/rc[0-6].d/*acoustic-frequency-feed"
