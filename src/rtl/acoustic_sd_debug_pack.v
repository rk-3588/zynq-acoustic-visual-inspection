// Optional temporary BD module reference. NOT added to the production XPR.
// Pure wiring: preserves the microphone input and decoded-data paths.
module acoustic_sd_debug_pack (
    input wire [15:0] mic_sd,
    input wire mic_sck,
    input wire mic_ws,
    input wire [383:0] adc_data,
    input wire rx_done_edge,
    output wire [52:0] probe_data,
    output wire trigger_ws
);
    // probe_data[0]     SCK
    // probe_data[1]     WS
    // probe_data[2]     raw SD data1
    // probe_data[3]     raw SD data6
    // probe_data[27:4]  decoded signed24 data1
    // probe_data[51:28] decoded signed24 data6
    // probe_data[52]    valid at Aclk
    assign probe_data={rx_done_edge,adc_data[167:144],adc_data[47:24],
                       mic_sd[6],mic_sd[1],mic_ws,mic_sck};
    assign trigger_ws=mic_ws;
endmodule
