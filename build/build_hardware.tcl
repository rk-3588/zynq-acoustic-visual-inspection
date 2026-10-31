# Rebuild from source without incremental checkpoints or prebuilt IP caches.
set root [file normalize [file join [file dirname [info script]] ..]]
set project [file join $root work vivado ac16_final.xpr]
if {$argc>0} {set project [file normalize [lindex $argv 0]]}
set_param general.maxThreads 2
open_project $project
set_property incremental_checkpoint {} [get_runs impl_1]
launch_runs synth_1 -jobs 2
wait_on_run synth_1
if {![string match *Complete* [get_property STATUS [get_runs synth_1]]]} {error "Synthesis failed"}
# Load implementation-only/late constraints through the project run, as in the
# verified build; opening synthesis alone can omit the capture CDC constraints.
launch_runs impl_1 -to_step opt_design -jobs 2
wait_on_run impl_1
set dcp [glob -nocomplain [file join [get_property DIRECTORY [get_runs impl_1]] *_opt.dcp]]
if {[llength $dcp]!=1} {error "Implementation opt checkpoint missing"}
open_checkpoint [lindex $dcp 0]
# Match the verified standalone implementation sequence. No timing relaxation.
place_design -directive Explore
phys_opt_design -directive AggressiveExplore
route_design -directive Explore
phys_opt_design -directive AggressiveExplore
source [file join $root build check_capture_cdc.tcl]
set output [file join $root work implementation]
file mkdir $output
report_timing_summary -delay_type min_max -report_unconstrained -file [file join $output timing.rpt]
report_utilization -file [file join $output utilization.rpt]
report_drc -file [file join $output drc.rpt]
report_route_status -file [file join $output route_status.rpt]
report_bus_skew -file [file join $output bus_skew.rpt]
set setup [get_timing_paths -delay_type max -max_paths 1]
set hold [get_timing_paths -delay_type min -max_paths 1]
if {[llength $setup]!=1 || [llength $hold]!=1} {error "Timing paths missing; bit not exported"}
if {[get_property SLACK $setup]<0 || [get_property SLACK $hold]<0} {error "Timing failed; bit not exported"}
if {[llength [get_drc_violations -quiet -filter {SEVERITY == Error}]]} {error "DRC failed"}
set f [open [file join $output route_status.rpt] r];set route [read $f];close $f
if {![regexp {# of nets with routing errors\.+\s*:\s*0\s} $route]} {error "Routing errors"}
if {![regexp {# of routable nets\.+\s*:\s*(\d+)} $route -> routable] ||
    ![regexp {# of fully routed nets\.+\s*:\s*(\d+)} $route -> routed] || $routable!=$routed} {error "Incomplete routing"}
set f [open [file join $output bus_skew.rpt] r];set skew [read $f];close $f
if {[string first "VIOLATED" $skew]>=0} {error "Bus skew constraint failed; bit not exported"}
write_checkpoint -force [file join $output system_wrapper.dcp]
write_debug_probes -force [file join $output system_wrapper.ltx]
write_bitstream -force [file join $output system_wrapper.bit]
write_hw_platform -fixed -include_bit -force -file [file join $output system_wrapper.xsa]
puts PASS_FINAL_HARDWARE_BUILD
close_project
