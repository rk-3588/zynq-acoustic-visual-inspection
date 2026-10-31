# C simulation only; the frozen Vivado build uses the checked-in exported RTL.
# vitis-run --mode hls --tcl sim/run_hls.tcl
set root [file normalize [file join [file dirname [info script]] ..]]
set work [file join $root work hls_csim]
if {[info exists env(AC16_HLS_WORK)]} {set work [file normalize $env(AC16_HLS_WORK)]}
file mkdir $work
foreach {name top source testbench} {
    delay delay delay.cpp tb_delay.cpp
    color_mapping color_mapping color_mapping.cpp color_mapping_tb.cpp
} {
    open_project [file join $work $name]
    set_top $top
    add_files [file join $root src hls $name $source]
    add_files -tb [file join $root src hls $name $testbench]
    open_solution solution1 -flow_target vivado
    set_part xc7z020clg400-2
    create_clock -period 5
    if {$name eq "delay"} {
        csim_design -argv [list [file join $root data delay_rom.coe]]
    } else {
        csim_design
    }
    close_project
}
puts PASS_RELEASE_HLS_CSIM_DELAY_AND_COLOR_MAPPING
exit
