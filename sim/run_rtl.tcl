# Run in a new work/rtl directory: vivado -mode batch -source sim/run_rtl.tcl
set root [file normalize [file join [file dirname [info script]] ..]]
set work [file join $root work rtl]
file mkdir $work
cd $work
exec python [file join $root build generate_hpf_vectors.py] [file join $work vectors.txt]
set suffix ""
if {$tcl_platform(platform) eq "windows"} {set suffix ".bat"}
exec xvlog${suffix} [file join $env(XILINX_VIVADO) data verilog src glbl.v]
foreach {top inputs} {
    tb_heatmap_hflip {src/rtl/acoustic_pixel_mapping.v src/rtl/acoustic_result_adapter.v sim/rtl/tb_heatmap_hflip.sv}
    tb_acoustic_capture {src/rtl/acoustic_capture.v sim/rtl/tb_acoustic_capture.sv}
    tb_video_tone {src/rtl/video_preprocess.v sim/rtl/tb_video_tone.sv}
    tb_acoustic_hpf_200 {src/rtl/acoustic_hpf_200.v sim/rtl/tb_acoustic_hpf_200.sv}
    tb_sensor_hpf_chain {src/rtl/sensors.v src/rtl/acoustic_hpf_200.v sim/rtl/tb_sensor_hpf_chain.sv}
} {
    if {$argc>0 && [lsearch -exact $argv $top]<0} {continue}
    set args {}
    foreach f $inputs {lappend args [file join $root $f]}
    exec xvlog${suffix} -sv {*}$args
    exec xelab${suffix} $top glbl -s $top -L xpm -L unisims_ver
    set result [exec xsim${suffix} $top -runall]
    puts $result
    if {[regexp -nocase {fatal|error:|FAIL:} $result]} {error "RTL simulation failed: $top"}
}
puts PASS_RELEASE_RTL_TESTS
