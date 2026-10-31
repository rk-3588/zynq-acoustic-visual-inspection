FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append = " file://system-user.dtsi"

require ${@'device-tree-sdt.inc' if d.getVar('SYSTEM_DTFILE') != '' else ''}

SRC_URI:append = " file://acoustic-capture.dtsi"

SRC_URI:append = " file://video-preprocess.dtsi"

SRC_URI:append = " file://uart1.dtsi"
