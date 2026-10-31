`timescale 1ns/1ps
// Single frozen block, 2048 x signed32 (sign-extended signed24).
// No stream backpressure: this is a passive tap, never stalls the acoustic path.
// Either reset input resets BOTH domains; assert async, release synchronized.
// RAM contents are intentionally not reset; the READY flag protects old data.
module acoustic_capture #(
    parameter integer SAMPLE_RATE_HZ = 46875
) (
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axi, ASSOCIATED_RESET s_axi_aresetn, FREQ_HZ 100000000" *)
    input wire s_axi_aclk,
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *) input wire s_axi_aresetn,
    input wire [15:0] s_axi_awaddr,
    input wire [2:0] s_axi_awprot,
    input wire s_axi_awvalid, output wire s_axi_awready,
    input wire [31:0] s_axi_wdata, input wire [3:0] s_axi_wstrb,
    input wire s_axi_wvalid, output wire s_axi_wready,
    output reg [1:0] s_axi_bresp, output reg s_axi_bvalid,
    input wire s_axi_bready,
    input wire [15:0] s_axi_araddr, input wire [2:0] s_axi_arprot,
    input wire s_axi_arvalid, output wire s_axi_arready,
    output reg [31:0] s_axi_rdata, output reg [1:0] s_axi_rresp,
    output reg s_axi_rvalid, input wire s_axi_rready,
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_RESET sample_rst, FREQ_HZ 210000000" *) input wire sample_clk,
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_HIGH" *) input wire sample_rst,
    input wire [383:0] sample_data,
    input wire sample_valid
);
    localparam [31:0] ID = 32'h41434631; // ACF1
    // Synchronize EACH reset source independently into EACH clock domain.
    // Combining resets before asynchronous synchronizers creates a CDC-10
    // combinational-reset crossing. Combine only the synchronized outputs.
    wire axi_bus_resetn, axi_sample_reset, sample_bus_resetn, sample_source_reset;
    wire axi_reset_active=!axi_bus_resetn || axi_sample_reset;
    wire sample_reset_active=!sample_bus_resetn || sample_source_reset;
    xpm_cdc_async_rst #(.DEST_SYNC_FF(2),.INIT_SYNC_FF(0),.RST_ACTIVE_HIGH(0)) reset_axi_bus (
        .src_arst(s_axi_aresetn),.dest_clk(s_axi_aclk),.dest_arst(axi_bus_resetn));
    xpm_cdc_async_rst #(.DEST_SYNC_FF(2),.INIT_SYNC_FF(0),.RST_ACTIVE_HIGH(1)) reset_axi_sample (
        .src_arst(sample_rst),.dest_clk(s_axi_aclk),.dest_arst(axi_sample_reset));
    xpm_cdc_async_rst #(.DEST_SYNC_FF(2),.INIT_SYNC_FF(0),.RST_ACTIVE_HIGH(0)) reset_sample_bus (
        .src_arst(s_axi_aresetn),.dest_clk(sample_clk),.dest_arst(sample_bus_resetn));
    xpm_cdc_async_rst #(.DEST_SYNC_FF(2),.INIT_SYNC_FF(0),.RST_ACTIVE_HIGH(1)) reset_sample_source (
        .src_arst(sample_rst),.dest_clk(sample_clk),.dest_arst(sample_source_reset));
    (* ram_style="block" *) reg [31:0] capture_ram [0:2047];
    reg [31:0] ram_read_data;
    reg [3:0] channel_cfg, request_channel;
    reg request_toggle;
    (* ASYNC_REG="TRUE", SHREG_EXTRACT="NO" *) reg req_meta, req_sync;
    (* ASYNC_REG="TRUE", SHREG_EXTRACT="NO" *) reg done_meta, done_sync;
    reg done_toggle, req_seen;
    reg [3:0] active_channel;
    reg request_wait, capturing;
    reg [10:0] write_index;
    reg selected_valid;
    reg [31:0] selected_sample;
    reg busy, ready;
    reg [31:0] sequence_number;
    wire [23:0] selected_input = sample_data[active_channel*24 +: 24];

    // Bundled request_channel is held from ARM until completion and release.
    // Two control synchronizers + an extra wait cycle precede bundle capture.
    always @(posedge sample_clk) begin
        if(sample_reset_active) begin
            req_meta<=0; req_sync<=0; req_seen<=0; done_toggle<=0;
            active_channel<=0; request_wait<=0; capturing<=0;
            write_index<=0; selected_valid<=0; selected_sample<=0;
        end else begin
            req_meta<=request_toggle; req_sync<=req_meta;
            selected_valid<=0;
            if(!capturing && !request_wait && req_sync!=req_seen) begin
                req_seen<=req_sync; request_wait<=1;
            end
            if(request_wait) begin
                active_channel<=request_channel; request_wait<=0;
                capturing<=1; write_index<=0;
            end
            if(capturing && sample_valid) begin
                selected_sample<={{8{selected_input[23]}},selected_input};
                selected_valid<=1;
            end
            if(capturing && selected_valid) begin
                if(write_index==2047) begin
                    capturing<=0; selected_valid<=0; done_toggle<=req_seen;
                end else write_index<=write_index+1'b1;
            end
        end
    end
    // Separate, reset-free RAM process is important for BRAM inference.
    always @(posedge sample_clk)
        if(!sample_reset_active && capturing && selected_valid)
            capture_ram[write_index]<=selected_sample;

    reg aw_pending, w_pending;
    reg [15:0] awaddr;
    reg [31:0] wdata;
    reg [3:0] wstrb;
    reg memory_pending;
    assign s_axi_awready = !axi_reset_active && !aw_pending && !s_axi_bvalid;
    assign s_axi_wready = !axi_reset_active && !w_pending && !s_axi_bvalid;
    assign s_axi_arready = !axi_reset_active && !s_axi_rvalid && !memory_pending;
    wire read_accept = s_axi_arvalid && s_axi_arready;
    wire memory_address = s_axi_araddr>=16'h1000 && s_axi_araddr<16'h3000;
    wire memory_read = read_accept && memory_address && ready && !busy && s_axi_araddr[1:0]==0;
    wire [15:0] memory_offset = s_axi_araddr-16'h1000;
    always @(posedge s_axi_aclk)
        if(memory_read) ram_read_data<=capture_ram[memory_offset[12:2]];

    always @(posedge s_axi_aclk) begin
        if(axi_reset_active) begin
            channel_cfg<=0; request_channel<=0; request_toggle<=0;
            done_meta<=0; done_sync<=0; busy<=0; ready<=0; sequence_number<=0;
            aw_pending<=0; w_pending<=0; awaddr<=0; wdata<=0; wstrb<=0;
            s_axi_bresp<=0; s_axi_bvalid<=0; s_axi_rdata<=0;
            s_axi_rresp<=0; s_axi_rvalid<=0; memory_pending<=0;
        end else begin
            done_meta<=done_toggle; done_sync<=done_meta;
            if(busy && done_sync==request_toggle) begin
                busy<=0; ready<=1; sequence_number<=sequence_number+1'b1;
            end
            if(s_axi_bvalid && s_axi_bready) s_axi_bvalid<=0;
            if(s_axi_awready && s_axi_awvalid) begin awaddr<=s_axi_awaddr; aw_pending<=1; end
            if(s_axi_wready && s_axi_wvalid) begin wdata<=s_axi_wdata; wstrb<=s_axi_wstrb; w_pending<=1; end
            if(aw_pending && w_pending && !s_axi_bvalid) begin
                aw_pending<=0; w_pending<=0; s_axi_bvalid<=1; s_axi_bresp<=2'b10;
                if(awaddr==16'h0004 && wstrb==4'hf) begin
                    if(wdata==1 && !busy && !ready && !memory_pending && !s_axi_rvalid) begin
                        request_channel<=channel_cfg; request_toggle<=!request_toggle;
                        busy<=1; s_axi_bresp<=0;
                    end else if(wdata==2 && ready && !busy && !memory_pending && !s_axi_rvalid) begin
                        ready<=0; s_axi_bresp<=0;
                    end
                end else if(awaddr==16'h000c && wstrb==4'hf && wdata<16 && !busy && !ready) begin
                    channel_cfg<=wdata[3:0]; s_axi_bresp<=0;
                end
            end
            if(s_axi_rvalid && s_axi_rready) s_axi_rvalid<=0;
            if(memory_pending) begin
                s_axi_rdata<=ram_read_data; s_axi_rresp<=0;
                s_axi_rvalid<=1; memory_pending<=0;
            end
            if(read_accept) begin
                s_axi_rdata<=0; s_axi_rresp<=0; s_axi_rvalid<=1;
                if(s_axi_araddr[1:0]!=0) s_axi_rresp<=2'b10;
                else if(memory_address) begin
                    if(ready && !busy) begin memory_pending<=1; s_axi_rvalid<=0; end
                    else s_axi_rresp<=2'b10;
                end else case(s_axi_araddr)
                    16'h0000: s_axi_rdata<=ID;
                    16'h0004: s_axi_rdata<=0;
                    16'h0008: s_axi_rdata<={30'b0,ready,busy};
                    16'h000c: s_axi_rdata<={28'b0,channel_cfg};
                    16'h0010: s_axi_rdata<=SAMPLE_RATE_HZ;
                    16'h0014: s_axi_rdata<=2048;
                    16'h0018: s_axi_rdata<=sequence_number;
                    16'h001c: s_axi_rdata<={28'b0,request_channel};
                    16'h0020: s_axi_rdata<=32'h00180020; // input24, storage32
                    16'h0024: s_axi_rdata<=1; // protocol version
                    16'h0028: s_axi_rdata<=ready?2048:0;
                    default: s_axi_rresp<=2'b10;
                endcase
            end
        end
    end
endmodule
