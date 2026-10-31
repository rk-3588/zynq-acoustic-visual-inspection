# Real Tcl, not XDC. Attach to STEPS.OPT_DESIGN.TCL.PRE on impl_1.
# Also source this after opening a checkpoint to audit the endpoint topology.
proc capture_cdc_expect {objects count label} {
    if {[llength $objects] != $count} {
        error "Capture CDC: $label expected $count objects, got [llength $objects]: $objects"
    }
}
set cap_check_root system_i/acoustic_capture_0/inst
capture_cdc_expect [get_pins -quiet ${cap_check_root}/req_meta_reg/D] 1 req_meta
capture_cdc_expect [get_pins -quiet ${cap_check_root}/done_meta_reg/D] 1 done_meta
capture_cdc_expect [get_cells -quiet -hierarchical -filter "NAME =~ ${cap_check_root}/request_channel_reg*"] 4 request_channel
capture_cdc_expect [get_cells -quiet -hierarchical -filter "NAME =~ ${cap_check_root}/active_channel_reg*"] 4 active_channel
foreach pipe {reset_axi_bus reset_axi_sample reset_sample_bus reset_sample_source} {
    foreach stage {0 1} {
        set stem [format {%s/%s/arststages_ff_reg[%d]} $cap_check_root $pipe $stage]
        set cell [get_cells -quiet $stem]
        capture_cdc_expect $cell 1 $stem
        set reset_pin [get_pins -quiet -of_objects $cell -filter {REF_PIN_NAME == PRE || REF_PIN_NAME == CLR}]
        capture_cdc_expect $reset_pin 1 $stem-reset-entry
        set async_value [get_property ASYNC_REG $cell]
        puts "CAPTURE_RESET_CELL $stem ASYNC_REG=$async_value TYPE=[get_property REF_NAME $cell]"
        if {[string toupper $async_value] ni {1 TRUE}} {
            error "Capture CDC: ASYNC_REG missing on $stem (value=$async_value)"
        }
        if {[get_property REF_NAME $cell] ni {FDPE FDCE}} {
            error "Capture CDC: expected asynchronous reset FF on $stem"
        }
    }
    set first [format {%s/%s/arststages_ff_reg[0]/Q} $cap_check_root $pipe]
    set second [format {%s/%s/arststages_ff_reg[1]/D} $cap_check_root $pipe]
    # all_fanin -startpoints_only reports sequential CLOCK pins in Vivado;
    # compare actual Q/D nets instead to prove direct stage-to-stage wiring.
    set source_net [get_nets -of_objects [get_pins $first]]
    set destination_net [get_nets -of_objects [get_pins $second]]
    capture_cdc_expect $source_net 1 $pipe-source-net
    capture_cdc_expect $destination_net 1 $pipe-destination-net
    if {$source_net ne $destination_net} {
        error "Capture CDC: reset deassertion chain is not direct: $source_net -> $destination_net"
    }
}
puts "PASS_CAPTURE_CDC_TOPOLOGY: four XPM reset-release chains/eight flops, mailbox endpoints"
