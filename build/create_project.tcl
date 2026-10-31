# Vivado 2026.1. Run: vivado -mode batch -source build/create_project.tcl
# Optional output: -tclargs <empty_output_directory>
set root [file normalize [file join [file dirname [info script]] ..]]
set work [file join $root work vivado]
if {$argc > 0} {set work [file normalize [lindex $argv 0]]}
if {$tcl_platform(platform) eq "windows" && [string length $work]>50} {
    error "Use a short Windows output path, for example -tclargs D:/ac16_build (HLS-generated names can exceed MAX_PATH)"
}
if {[file exists [file join $work ac16_final.xpr]]} {error "Output project already exists; select a new output directory"}
file mkdir $work
create_project ac16_final $work -part xc7z020clg400-2
set_property target_language Verilog [current_project]
set_property simulator_language Mixed [current_project]
set_property enable_vhdl_2008 true [current_project]
set_property xpm_libraries {XPM_CDC XPM_FIFO XPM_MEMORY} [current_project]
set_property ip_repo_paths [list [file join $root src vivado ip_catalog]] [current_project]
update_ip_catalog -rebuild
add_files -norecurse [glob [file join $root src rtl *.v]]
set_property file_type SystemVerilog [get_files *acoustic_hpf_200.v]
set standalone {}
foreach xci [glob [file join $root src vivado ip * *.xci]] {
    import_ip -files $xci
    lappend standalone [file rootname [file tail $xci]]
}
set_property CONFIG.Coe_File [file join $root data delay_rom.coe] [get_ips blk_mem_gen_2]
import_files -norecurse [file join $root src vivado system.bd]
add_files -norecurse [file join $root src vivado ip blk_mem_gen_2 delay_rom.coe]
add_files -fileset constrs_1 [glob [file join $root src constraints *.xdc]]
set cap [get_files *acoustic_capture_cdc.xdc]
set_property used_in_synthesis false $cap
set_property used_in_implementation true $cap
set_property processing_order LATE $cap
open_bd_design [get_files system.bd]
validate_bd_design
# Leave the exported source BD immutable: import to the project's own sources.
generate_target all [get_files system.bd]
foreach name $standalone {
    set ip [get_ips $name]
    if {![get_property IS_LOCKED $ip]} {generate_target all $ip}
}
add_files -norecurse [make_wrapper -files [get_files system.bd] -top]
set_property top system_wrapper [get_filesets sources_1]
set_property top_auto_set false [get_filesets sources_1]
update_compile_order -fileset sources_1
set_property incremental_checkpoint {} [get_runs impl_1]
set_property STEPS.OPT_DESIGN.TCL.PRE [file join $root build check_capture_cdc.tcl] [get_runs impl_1]
puts "PASS_FINAL_PROJECT_CREATED_AND_BD_VALIDATED $work"
close_project
