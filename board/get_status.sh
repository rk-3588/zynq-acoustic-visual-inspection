#!/bin/sh
# Read-only: no resets, IRQ clearing, calibration, capture trigger or UART TX.
cat /proc/cmdline
findmnt /
for uio in /sys/class/uio/uio*; do
    [ -d "$uio" ] || continue
    printf '%s ' "$uio"
    cat "$uio/name" "$uio/maps/map0/addr"
done
for state in /run/video-pipeline.state /run/video-status-panel-watchdog.state /run/video-status-panel.state; do
    [ ! -r "$state" ] || cat "$state"
done
/usr/bin/video_display_diag --samples 3 --interval-ms 1000
/usr/bin/video_status_panel --temperature
