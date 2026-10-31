# Top-level constraints for the actual handshakes, not blanket clock groups.
# Read LATE, implementation only: the module-reference netlist must be linked.

# mclk / 10 is a fabric-generated clock. This does not change RTL/frequency.
# The divider toggles every fifth mclk rising edge. Its phase is reset-dependent;
# CDC paths out of this clock are bounded explicitly below.
create_generated_clock -name mic_bclk -source [get_pins system_i/sensors_0/inst/u_divider/clk_10_reg/C] -edges {9 19 29} [get_pins system_i/sensors_0/inst/u_divider/clk_10_reg/Q]

# Both divider counters have the SAME reset. WS toggles every 320 mclk edges,
# exactly on a BCLK falling edge (320 is a multiple of 10). It is therefore
# stable for FIVE mclk periods (~166.7 ns at 30 MHz) before BCLK rising capture,
# not a signal that can actually launch on every mclk edge. Bound these
# specific WS paths to 10 ns; do not mask any other mclk -> BCLK path.
set_max_delay -datapath_only 10.000 -from [get_cells system_i/sensors_0/inst/u_divider/clk_640_reg] -to [get_clocks mic_bclk]

# Control toggles: asynchronous arrival only at the FIRST synchronizer stage.
# The meta -> sync path remains normally timed in its receiving clock domain.
set_false_path -to [get_pins system_i/acoustic_result_adap_0/inst/dst_req_meta_reg/D]
set_false_path -to [get_pins system_i/acoustic_result_adap_0/inst/src_ack_meta_reg/D]
set_false_path -to [get_pins system_i/sensors_0/inst/rx_done1_reg/D]
# Single MODE bypass bit: first stage only. Its meta -> sync -> token latch
# paths remain timed at the full 210-MHz receiving clock.
set_false_path -to [get_pins system_i/acoustic_hpf_200_0/inst/bypass_meta_reg/D]

# Acoustic result mailbox: src_data_hold cannot change until the ACK returns.
# Two AXI synchronizer stages precede the consumer's capture (>= 2 AXI periods).
# Bound the complete payload path (including the status comparator) to ONE
# 100-MHz AXI period, and its skew to the same conservative settling budget.
# Do not false-path this bus: implementation must still meet these bounds.
set_max_delay -datapath_only 10.000 -from [get_cells -hierarchical -filter {NAME =~ system_i/acoustic_result_adap_0/inst/src_data_hold_reg*}] -to [get_cells -hierarchical -filter {NAME =~ system_i/acoustic_result_adap_0/inst/*axi_reg*}]
# Bus skew applies to direct bundle captures, not the multi-level comparator.
# The comparator remains covered by the full 10-ns max-delay constraint above.
set_bus_skew 10.000 -from [get_cells -hierarchical -filter {NAME =~ system_i/acoustic_result_adap_0/inst/src_data_hold_reg*}] -to [get_cells -hierarchical -filter {NAME =~ system_i/acoustic_result_adap_0/inst/*axi_reg* && NAME != "system_i/acoustic_result_adap_0/inst/status_axi_reg[1]"}]

# Microphone bundle: the last serial bit is stable a full BCLK period BEFORE
# rx_done rises. buffer_ctrl writes BRAM only after the two-stage synchronizer,
# edge detector and write-enable pipeline. It completes well before the next
# left slot starts. A 10-ns bound is far below the 333-ns BCLK settling interval.
set_max_delay -datapath_only 10.000 -from [get_cells -hierarchical -filter {NAME =~ system_i/sensors_0/inst/gen_sensor*.adc_data_reg*}] -to [get_clocks -of_objects [get_pins system_i/sensors_0/inst/rx_done1_reg/C]]

