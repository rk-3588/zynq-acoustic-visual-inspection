`timescale 1 ns / 1 ps

// Frame-level acoustic hotspot detector and CDC mailbox.
// The source side scans one 64x36 acoustic map (2304 samples).  The
// destination side exposes a stable AXI-clock-domain snapshot.
module acoustic_result_adapter #(
    parameter integer VALUE_WIDTH = 24,
    parameter integer INDEX_WIDTH = 14
) (
    // Acoustic processing clock domain (currently clk_wiz_1/Aclk, 210 MHz)
    input  wire                     acoustic_clk,
    input  wire                     acoustic_rstn,
    input  wire                     sample_valid,
    input  wire                     sample_last,
    input  wire [INDEX_WIDTH-1:0]   sample_index,
    input  wire [VALUE_WIDTH-1:0]   sample_value,

    // AXI/control clock domain (currently PS FCLK_CLK0, 100 MHz)
    input  wire                     axi_clk,
    input  wire                     axi_rstn,
    input  wire [31:0]              threshold_axi,
    output reg  [31:0]              status_axi,
    output reg  [31:0]              hotspot_x_axi,
    output reg  [31:0]              hotspot_y_axi,
    output reg  [31:0]              hotspot_value_axi,
    output reg  [31:0]              confidence_axi,
    output reg                      new_result_axi
);

    localparam integer DATA_WIDTH = VALUE_WIDTH + INDEX_WIDTH + 1;

    reg [VALUE_WIDTH-1:0] max_value;
    reg [INDEX_WIDTH-1:0] max_index;
    reg                    src_req_toggle;
    reg                    src_overrun;
    reg [DATA_WIDTH-1:0]   src_data_hold;

    // Keep each two-flop synchronizer adjacent; only its first D pin is
    // excluded from synchronous setup/hold analysis by acoustic_cdc.xdc.
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg src_ack_meta, src_ack_sync;
    reg dst_ack_toggle;
    wire src_busy = src_req_toggle ^ src_ack_sync;

    wire [VALUE_WIDTH-1:0] candidate_value =
        (sample_index == {INDEX_WIDTH{1'b0}} || sample_value > max_value)
        ? sample_value : max_value;
    wire [INDEX_WIDTH-1:0] candidate_index =
        (sample_index == {INDEX_WIDTH{1'b0}} || sample_value > max_value)
        ? sample_index : max_index;

    // Acoustic-domain frame scan and mailbox producer.
    always @(posedge acoustic_clk or negedge acoustic_rstn) begin
        if (!acoustic_rstn) begin
            max_value      <= {VALUE_WIDTH{1'b0}};
            max_index      <= {INDEX_WIDTH{1'b0}};
            src_req_toggle <= 1'b0;
            src_overrun    <= 1'b0;
            src_data_hold  <= {DATA_WIDTH{1'b0}};
            src_ack_meta   <= 1'b0;
            src_ack_sync   <= 1'b0;
        end else begin
            // Return acknowledgement crosses back through two flops.
            src_ack_meta <= dst_ack_toggle;
            src_ack_sync <= src_ack_meta;

            if (sample_valid) begin
                max_value <= candidate_value;
                max_index <= candidate_index;

                if (sample_last) begin
                    if (!src_busy) begin
                        src_data_hold  <= {src_overrun, candidate_index,
                                            candidate_value};
                        src_req_toggle <= ~src_req_toggle;
                        src_overrun    <= 1'b0;
                    end else begin
                        // A new frame arrived before the previous result was
                        // consumed. Keep the old snapshot and report it.
                        src_overrun <= 1'b1;
                    end
                end
            end
        end
    end

    // AXI-domain mailbox consumer. The payload is held stable by the source
    // until dst_ack_toggle returns, so the bundled bus is sampled safely after
    // the request toggle has passed through two synchronizer stages.
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg dst_req_meta, dst_req_sync;
    reg [DATA_WIDTH-1:0] dst_data_hold;
    reg [31:0] threshold_meta, threshold_sync;
    wire dst_new = dst_req_sync ^ dst_ack_toggle;

    always @(posedge axi_clk or negedge axi_rstn) begin
        if (!axi_rstn) begin
            dst_req_meta    <= 1'b0;
            dst_req_sync    <= 1'b0;
            dst_ack_toggle  <= 1'b0;
            dst_data_hold   <= {DATA_WIDTH{1'b0}};
            threshold_meta  <= 32'b0;
            threshold_sync  <= 32'b0;
            status_axi      <= 32'b0;
            hotspot_x_axi   <= 32'b0;
            hotspot_y_axi   <= 32'b0;
            hotspot_value_axi <= 32'b0;
            confidence_axi <= 32'b0;
            new_result_axi <= 1'b0;
        end else begin
            dst_req_meta   <= src_req_toggle;
            dst_req_sync   <= dst_req_meta;
            threshold_meta <= threshold_axi;
            threshold_sync <= threshold_meta;
            new_result_axi <= 1'b0;

            if (dst_new) begin
                // The source holds this bundled payload stable until the
                // acknowledgement returns. Two toggle synchronizer stages
                // provide settling time before this sample.
                dst_data_hold  <= src_data_hold;
                dst_ack_toggle <= dst_req_sync;

                // Keep the PS ABI in legacy 128x72 coordinates: cell centres.
                // Mirror the 64-column grid; legacy odd-centre x becomes 128-x.
                hotspot_x_axi     <= {25'b0, ~src_data_hold[VALUE_WIDTH+5:VALUE_WIDTH], 1'b1};
                hotspot_y_axi     <= {25'b0, src_data_hold[VALUE_WIDTH+11:VALUE_WIDTH+6], 1'b1};
                hotspot_value_axi <= {{(32-VALUE_WIDTH){1'b0}}, src_data_hold[VALUE_WIDTH-1:0]};
                confidence_axi    <= {{(32-VALUE_WIDTH){1'b0}}, src_data_hold[VALUE_WIDTH-1:0]};
                status_axi[0]     <= 1'b1; // result_valid
                status_axi[1]     <= (threshold_sync[VALUE_WIDTH-1:0] != {VALUE_WIDTH{1'b0}}) &&
                                      (src_data_hold[VALUE_WIDTH-1:0] >= threshold_sync[VALUE_WIDTH-1:0]);
                status_axi[2]     <= 1'b0; // busy, result is a snapshot
                status_axi[3]     <= src_data_hold[DATA_WIDTH-1]; // overrun
                new_result_axi    <= 1'b1;
            end
        end
    end
endmodule
