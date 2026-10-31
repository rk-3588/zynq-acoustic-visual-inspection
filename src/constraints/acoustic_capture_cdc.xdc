# Implementation only, PROCESSING_ORDER=LATE. Topology checks belong in
# check_capture_cdc.tcl (OPT_DESIGN pre-hook), not the restricted XDC parser.
# Only the first stage of each control synchronizer is asynchronous.
set_false_path -to [get_pins -quiet system_i/acoustic_capture_0/inst/req_meta_reg/D]
set_false_path -to [get_pins -quiet system_i/acoustic_capture_0/inst/done_meta_reg/D]

# request_channel stays fixed from ARM until completion and release. Two
# request synchronizer stages plus an extra wait cycle precede bundle capture.
# Preserve the original 8-ns data-settling and skew bounds; do not cut the bus.
set_max_delay -datapath_only 8.000 -from [get_cells -quiet -hierarchical -filter {NAME =~ system_i/acoustic_capture_0/inst/request_channel_reg*}] -to [get_cells -quiet -hierarchical -filter {NAME =~ system_i/acoustic_capture_0/inst/active_channel_reg*}]
set_bus_skew 8.000 -from [get_cells -quiet -hierarchical -filter {NAME =~ system_i/acoustic_capture_0/inst/request_channel_reg*}] -to [get_cells -quiet -hierarchical -filter {NAME =~ system_i/acoustic_capture_0/inst/active_channel_reg*}]

# The four XPM_CDC_ASYNC_RST instances supply their own scoped reset-entry
# exceptions. Their synchronous stage-to-stage paths remain normally timed.
