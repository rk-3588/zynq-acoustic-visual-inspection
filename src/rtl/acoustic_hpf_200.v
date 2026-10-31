`timescale 1ns/1ps
// 16 identical first-order high-pass channels, Fs=46875Hz, Fc=199.99765Hz.
// LP-state formulation of the normalized bilinear high-pass:
//   u=x-d; delta=(1-a)*u; d'=d+delta; y=u-delta/2.
// One constant multiplication per lane, 12 internal fractional bits.
// MODE register bit16=1 bypasses OUTPUT only; filter states keep tracking.
// Input must be held stable at sample_valid_in. Minimum accepted spacing is
// 7 Aclk periods; the microphone interval is 4480 periods at Aclk=210MHz.
// Both modes have the same 6-Aclk acceptance-to-output-valid latency.
module acoustic_hpf_200 #(
    parameter integer FRAC_BITS = 12,
    parameter integer B_COEFF_Q20 = 27740
) (
    input wire Aclk,
    input wire rst,
    input wire [383:0] sample_in,
    input wire sample_valid_in,
    input wire [31:0] mode_axi,
    output reg [383:0] sample_out,
    output reg sample_valid_out,
    output wire sample_ready,
    output reg input_overrun
);
    localparam integer SW = 24+2+FRAC_BITS;
    localparam integer PW = SW+18;
    localparam integer LPW = 25+18;
    localparam integer HPW = PW-24;
    localparam integer OW = SW-FRAC_BITS+1;
    localparam signed [17:0] B_MULT = B_COEFF_Q20;
    (* ASYNC_REG="TRUE", SHREG_EXTRACT="NO" *) reg bypass_meta, bypass_sync;
    reg [5:0] valid_pipe;
    reg [5:0] first_pipe, bypass_pipe;
    reg initialized;
    reg [383:0] raw0, raw1, raw2, raw3, raw4, raw5;
    assign sample_ready = ~(|valid_pipe);
    wire accept_sample = sample_valid_in && sample_ready;

    always @(posedge Aclk) begin
        if (rst) begin
            bypass_meta <= 0; bypass_sync <= 0;
            valid_pipe <= 0; first_pipe <= 0; bypass_pipe <= 0;
            initialized <= 0; input_overrun <= 0;
            raw0 <= 0; raw1 <= 0; raw2 <= 0; raw3 <= 0; raw4 <= 0; raw5 <= 0;
            sample_valid_out <= 0;
        end else begin
            bypass_meta <= mode_axi[16];
            bypass_sync <= bypass_meta;
            valid_pipe <= {valid_pipe[4:0],accept_sample};
            first_pipe <= {first_pipe[4:0],!initialized};
            bypass_pipe <= {bypass_pipe[4:0],bypass_sync};
            sample_valid_out <= valid_pipe[5];
            if (sample_valid_in && !sample_ready) input_overrun <= 1;
            if (accept_sample) begin
                initialized <= 1;
                raw0 <= sample_in;
            end
            if (valid_pipe[0]) raw1 <= raw0;
            if (valid_pipe[1]) raw2 <= raw1;
            if (valid_pipe[2]) raw3 <= raw2;
            if (valid_pipe[3]) raw4 <= raw3;
            if (valid_pipe[4]) raw5 <= raw4;
        end
    end

    // Signed round-to-nearest, ties away from zero, retaining SW Q12 bits.
    function signed [SW-1:0] round_product;
        input signed [PW-1:0] value;
        reg signed [SW-1:0] quotient;
        reg increment;
        begin
            // Arithmetic shift gives floor(value/2^20). Increment at >=half
            // for positive values, >half for negative values (ties away).
            // This avoids two full-width negations in the critical path.
            quotient = value >>> 20;
            increment = value[19] && (!value[PW-1] || |value[18:0]);
            round_product = quotient + increment;
        end
    endfunction

    function signed [OW-1:0] round_output;
        input signed [SW-1:0] value;
        reg signed [OW-1:0] quotient;
        reg increment;
        begin
            quotient = value >>> FRAC_BITS;
            increment = value[FRAC_BITS-1] &&
                        (!value[SW-1] || |(value & ((1<<(FRAC_BITS-1))-1)));
            round_output = quotient + increment;
        end
    endfunction

    // Saturation is a SEPARATE pipeline stage, with no wide add/subtract.
    // A rounded value fits in 24 bits iff its sign-extension bits agree.
    function [23:0] saturate_output;
        input signed [OW-1:0] rounded;
        begin
            if (rounded[OW-1:23] == {(OW-23){rounded[23]}})
                saturate_output = rounded[23:0];
            else saturate_output = rounded[OW-1] ? 24'h800000 : 24'h7fffff;
        end
    endfunction

    for (genvar lane=0; lane<16; lane=lane+1) begin : channel
        reg signed [SW-1:0] dc_state;
        reg signed [SW-1:0] u0, u1, u2, u3;
        // Decompose u into SIGNED upper bits and UNSIGNED low 24 bits.
        // Register both partial products BEFORE summing; a wide multiply
        // must not traverse an unregistered DSP cascade at 210 MHz.
        (* use_dsp="yes" *) reg signed [LPW-1:0] product_lo1;
        (* use_dsp="yes" *) reg signed [HPW-1:0] product_hi1;
        wire signed [24:0] operand_lo = $signed({1'b0,u0[23:0]});
        wire signed [SW-25:0] operand_hi = $signed(u0[SW-1:24]);
        wire signed [HPW-1:0] combined_hi = product_hi1 +
            {{(HPW-(LPW-24)){product_lo1[LPW-1]}},product_lo1[LPW-1:24]};
        reg signed [PW-1:0] product2;
        reg signed [SW-1:0] delta3;
        reg signed [SW-1:0] output_q4;
        reg signed [OW-1:0] rounded5;
        wire signed [23:0] input_word = sample_in[24*lane+:24];
        wire signed [SW-1:0] input_q = {{2{input_word[23]}},input_word,{FRAC_BITS{1'b0}}};
        wire signed [23:0] raw_word3 = raw3[24*lane+:24];
        wire signed [SW-1:0] raw_q3 = {{2{raw_word3[23]}},raw_word3,{FRAC_BITS{1'b0}}};
        always @(posedge Aclk) begin
            if (rst) begin
                dc_state <= 0; u0 <= 0; u1 <= 0; u2 <= 0; u3 <= 0;
                product_lo1 <= 0; product_hi1 <= 0; product2 <= 0;
                delta3 <= 0; output_q4 <= 0; rounded5 <= 0;
                sample_out[24*lane+:24] <= 0;
            end else begin
                if (accept_sample) u0 <= input_q-dc_state;
                if (valid_pipe[0]) begin
                    product_lo1 <= operand_lo*B_MULT;
                    product_hi1 <= operand_hi*B_MULT;
                    u1 <= u0;
                end
                if (valid_pipe[1]) begin
                    product2 <= {combined_hi,product_lo1[23:0]};
                    u2 <= u1;
                end
                if (valid_pipe[2]) begin
                    delta3 <= round_product(product2);
                    u3 <= u2;
                end
                if (valid_pipe[3]) begin
                    if (first_pipe[3]) begin
                        // First sample establishes the DC state, avoiding a
                        // large startup pulse from channel offset.
                        dc_state <= raw_q3;
                        output_q4 <= 0;
                    end else begin
                        dc_state <= dc_state+delta3;
                        output_q4 <= u3-(delta3 >>> 1);
                    end
                end
                if (valid_pipe[4]) begin
                    rounded5 <= round_output(output_q4);
                end
                if (valid_pipe[5]) begin
                    if (bypass_pipe[5]) sample_out[24*lane+:24] <= raw5[24*lane+:24];
                    else sample_out[24*lane+:24] <= saturate_output(rounded5);
                end
            end
        end
    end
    // synthesis translate_off
    initial begin
        if (FRAC_BITS<1 || FRAC_BITS>16 || B_COEFF_Q20<=0 || B_COEFF_Q20>=131072)
            $fatal(1,"Invalid high-pass precision/coefficient");
    end
    // synthesis translate_on
endmodule
