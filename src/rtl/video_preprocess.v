`timescale 1ns/1ps
// RGB888, R[23:16] G[15:8] B[7:0]. One clock domain, 150 MHz.
// Three elastic stages: fixed 3-cycle no-stall latency, one pixel/clock.
// Default BYPASS needs no software start. No RAM, frame buffer, or interrupts.
module video_preprocess #(
    parameter integer VIDEO_WIDTH = 1280,
    parameter integer VIDEO_HEIGHT = 720
) (
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axi:s_axis_video:m_axis_video, ASSOCIATED_RESET aresetn, FREQ_HZ 150000000" *)
    input wire aclk,
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *) input wire aresetn,
    input wire [15:0] s_axi_awaddr, input wire [2:0] s_axi_awprot,
    input wire s_axi_awvalid, output wire s_axi_awready,
    input wire [31:0] s_axi_wdata, input wire [3:0] s_axi_wstrb,
    input wire s_axi_wvalid, output wire s_axi_wready,
    output reg [1:0] s_axi_bresp, output reg s_axi_bvalid, input wire s_axi_bready,
    input wire [15:0] s_axi_araddr, input wire [2:0] s_axi_arprot,
    input wire s_axi_arvalid, output wire s_axi_arready,
    output reg [31:0] s_axi_rdata, output reg [1:0] s_axi_rresp,
    output reg s_axi_rvalid, input wire s_axi_rready,
    input wire [23:0] s_axis_video_tdata,
    input wire [2:0] s_axis_video_tkeep, input wire [2:0] s_axis_video_tstrb,
    input wire [0:0] s_axis_video_tuser, input wire s_axis_video_tlast,
    input wire [0:0] s_axis_video_tid, input wire [0:0] s_axis_video_tdest,
    input wire s_axis_video_tvalid, output wire s_axis_video_tready,
    output wire [23:0] m_axis_video_tdata,
    output wire [2:0] m_axis_video_tkeep, output wire [2:0] m_axis_video_tstrb,
    output wire [0:0] m_axis_video_tuser, output wire m_axis_video_tlast,
    output wire [0:0] m_axis_video_tid, output wire [0:0] m_axis_video_tdest,
    output wire m_axis_video_tvalid, input wire m_axis_video_tready
);
    localparam [31:0] ID = 32'h56505031; // VPP1
    localparam [31:0] GEOMETRY = (VIDEO_HEIGHT << 16) | VIDEO_WIDTH;
    localparam integer XW=$clog2(VIDEO_WIDTH+1), YW=$clog2(VIDEO_HEIGHT+1);
    localparam integer RW=2*XW+2*YW+1;
    localparam [XW-1:0] WIDTH_VALUE=VIDEO_WIDTH;
    localparam [YW-1:0] HEIGHT_VALUE=VIDEO_HEIGHT;
    // Packed rectangle: {enabled,y1,x1,y0,x0}; upper endpoints are exclusive.
    localparam [RW-1:0] ROI_RESET={1'b0,HEIGHT_VALUE,WIDTH_VALUE,{YW{1'b0}},{XW{1'b0}}};
    reg [2:0] shadow_mode, pending_mode, active_mode;
    reg [7:0] shadow_threshold, pending_threshold, active_threshold;
    reg signed [8:0] shadow_brightness,pending_brightness,active_brightness;
    reg signed [2:0] shadow_gain_shift,pending_gain_shift,active_gain_shift;
    reg [RW-1:0] shadow_roi,pending_roi,active_roi;
    reg [XW-1:0] next_x;
    reg [YW-1:0] next_y;
    reg cfg_pending, seen_sof;
    reg [31:0] frame_sequence, pixel_count, apply_sequence;

    reg valid0, valid1, valid2;
    reg [23:0] rgb0, rgb1, output_rgb;
    reg [9:0] metadata0, metadata1, metadata2;
    reg [2:0] mode0, mode1;
    reg [7:0] threshold0, threshold1, gray1;
    reg [15:0] red0, green0, blue0;
    reg inside0,inside1,bad0,bad1,end0,end1;
    reg [RW-1:0] roi0,roi1;
    reg [31:0] frame0,frame1,epoch0,epoch1;
    reg [2:0] frame_mode0,frame_mode1;
    reg signed [9:0] brightness_red0,brightness_green0,brightness_blue0;
    reg [9:0] gain_red0,gain_green0,gain_blue0;
    reg signed [8:0] brightness0,brightness1;
    reg signed [2:0] gain_shift0,gain_shift1;
    wire ready2 = !valid2 || m_axis_video_tready;
    wire ready1 = !valid1 || ready2;
    wire ready0 = !valid0 || ready1;
    assign s_axis_video_tready = aresetn && ready0;
    assign m_axis_video_tvalid = aresetn && valid2;
    assign m_axis_video_tdata = output_rgb;
    assign {m_axis_video_tkeep,m_axis_video_tstrb,m_axis_video_tuser,
            m_axis_video_tlast,m_axis_video_tid,m_axis_video_tdest} = metadata2;
    wire input_accept = s_axis_video_tvalid && s_axis_video_tready;
    wire input_sof = input_accept && s_axis_video_tuser[0];
    // The first pixel of a frame uses the newly applied config, not the old one.
    wire [2:0] input_mode = input_sof && cfg_pending ? pending_mode : active_mode;
    wire [7:0] input_threshold = input_sof && cfg_pending ? pending_threshold : active_threshold;
    wire signed [8:0] input_brightness=input_sof && cfg_pending ? pending_brightness : active_brightness;
    wire signed [2:0] input_gain_shift=input_sof && cfg_pending ? pending_gain_shift : active_gain_shift;
    wire [RW-1:0] input_roi=input_sof && cfg_pending ? pending_roi : active_roi;
    wire [XW-1:0] input_x=input_sof ? {XW{1'b0}} : next_x;
    wire [YW-1:0] input_y=input_sof ? {YW{1'b0}} : next_y;
    wire input_inside=!input_roi[RW-1] ||
        (input_x>=input_roi[0+:XW] && input_x<input_roi[XW+YW+:XW] &&
         input_y>=input_roi[XW+:YW] && input_y<input_roi[2*XW+YW+:YW]);
    wire input_bad=(input_x>=WIDTH_VALUE || input_y>=HEIGHT_VALUE ||
        s_axis_video_tlast!=(input_x==WIDTH_VALUE-1'b1) ||
        s_axis_video_tkeep!=3'b111 || s_axis_video_tstrb!=3'b111);
    function [31:0] roi_xy0;
        input [RW-1:0] value;
        begin roi_xy0={ {(16-YW){1'b0}},value[XW+:YW],{(16-XW){1'b0}},value[0+:XW] }; end
    endfunction
    function [31:0] roi_xy1;
        input [RW-1:0] value;
        begin roi_xy1={ {(16-YW){1'b0}},value[2*XW+YW+:YW],{(16-XW){1'b0}},value[XW+YW+:XW] }; end
    endfunction
    wire [15:0] red = {8'd0,s_axis_video_tdata[23:16]};
    wire [15:0] green = {8'd0,s_axis_video_tdata[15:8]};
    wire [15:0] blue = {8'd0,s_axis_video_tdata[7:0]};
    wire [15:0] gray_sum = red0 + green0 + blue0 + 16'd128;
    // Never truncate a left shift/addition to eight bits before saturation.
    function [9:0] gain_wide;
        input [7:0] pixel;
        input signed [2:0] shift;
        begin
            case(shift)
                -3'sd2: gain_wide={4'd0,pixel[7:2]}; // floor(pixel/4)
                -3'sd1: gain_wide={3'd0,pixel[7:1]}; // floor(pixel/2)
                 3'sd1: gain_wide={1'b0,pixel,1'b0};
                 3'sd2: gain_wide={pixel,2'b00};
                default: gain_wide={2'b00,pixel};
            endcase
        end
    endfunction
    function [7:0] clamp_brightness;
        input signed [9:0] value;
        begin
            if(value[9])clamp_brightness=0;
            else if(value[8])clamp_brightness=255;
            else clamp_brightness=value[7:0];
        end
    endfunction
    function [7:0] clamp_gain;
        input [9:0] value;
        begin clamp_gain=(|value[9:8]) ? 8'hff : value[7:0]; end
    endfunction

    always @(posedge aclk) begin
        if(!aresetn) begin
            valid0<=0; valid1<=0; valid2<=0;
            rgb0<=0; rgb1<=0; output_rgb<=0;
            metadata0<=0; metadata1<=0; metadata2<=0;
            mode0<=0; mode1<=0; threshold0<=128; threshold1<=128;
            red0<=0; green0<=0; blue0<=0; gray1<=0;
            inside0<=0;inside1<=0;bad0<=0;bad1<=0;end0<=0;end1<=0;
            roi0<=ROI_RESET;roi1<=ROI_RESET;frame0<=0;frame1<=0;epoch0<=0;epoch1<=0;
            frame_mode0<=0;frame_mode1<=0;
            brightness_red0<=0;brightness_green0<=0;brightness_blue0<=0;
            gain_red0<=0;gain_green0<=0;gain_blue0<=0;
            brightness0<=0;brightness1<=0;gain_shift0<=0;gain_shift1<=0;
        end else begin
            if(ready2) begin
                valid2<=valid1;
                if(valid1) begin
                    metadata2<=metadata1;
                    case(mode1)
                        3'd1: output_rgb<={gray1,gray1,gray1};
                        3'd2: output_rgb<=gray1>=threshold1 ? 24'hffffff : 24'h000000;
                        default: output_rgb<=rgb1;
                    endcase
                end
            end
            if(ready1) begin
                valid1<=valid0;
                if(valid0) begin
                    // The three tone modes are mutually exclusive. Other modes
                    // retain original RGB, and gray1 always describes input RGB.
                    case(mode0)
                        3'd3: rgb1<={clamp_brightness(brightness_red0),
                            clamp_brightness(brightness_green0),clamp_brightness(brightness_blue0)};
                        3'd4: rgb1<=~rgb0;
                        3'd5: rgb1<={clamp_gain(gain_red0),clamp_gain(gain_green0),clamp_gain(gain_blue0)};
                        default: rgb1<=rgb0;
                    endcase
                    metadata1<=metadata0;
                    mode1<=mode0; threshold1<=threshold0;
                    gray1<=gray_sum[15:8];
                    inside1<=inside0;bad1<=bad0;end1<=end0;roi1<=roi0;
                    frame1<=frame0;epoch1<=epoch0;frame_mode1<=frame_mode0;
                    brightness1<=brightness0;gain_shift1<=gain_shift0;
                end
            end
            if(ready0) begin
                valid0<=s_axis_video_tvalid;
                if(s_axis_video_tvalid) begin
                    rgb0<=s_axis_video_tdata;
                    metadata0<={s_axis_video_tkeep,s_axis_video_tstrb,s_axis_video_tuser,
                                s_axis_video_tlast,s_axis_video_tid,s_axis_video_tdest};
                    mode0<=input_inside ? input_mode : 3'd0; threshold0<=input_threshold;
                    inside0<=input_inside;bad0<=input_bad;
                    end0<=s_axis_video_tlast && input_y==HEIGHT_VALUE-1'b1;
                    roi0<=input_roi;frame_mode0<=input_mode;
                    frame0<=frame_sequence+{{31{1'b0}},input_sof};
                    epoch0<=apply_sequence+{{31{1'b0}},(input_sof && cfg_pending)};
                    // Y = (77R + 150G + 29B + 128) >> 8. Balanced shift/add trees.
                    red0<=((red<<6)+(red<<3))+((red<<2)+red);
                    green0<=((green<<7)+(green<<4))+((green<<2)+(green<<1));
                    blue0<=((blue<<4)+(blue<<3))+((blue<<2)+blue);
                    brightness_red0<=$signed({2'b00,s_axis_video_tdata[23:16]})+
                        $signed({input_brightness[8],input_brightness});
                    brightness_green0<=$signed({2'b00,s_axis_video_tdata[15:8]})+
                        $signed({input_brightness[8],input_brightness});
                    brightness_blue0<=$signed({2'b00,s_axis_video_tdata[7:0]})+
                        $signed({input_brightness[8],input_brightness});
                    gain_red0<=gain_wide(s_axis_video_tdata[23:16],input_gain_shift);
                    gain_green0<=gain_wide(s_axis_video_tdata[15:8],input_gain_shift);
                    gain_blue0<=gain_wide(s_axis_video_tdata[7:0],input_gain_shift);
                    brightness0<=input_brightness;gain_shift0<=input_gain_shift;
                end
            end
        end
    end

    // No frame/line RAM. Collect original grayscale statistics at stage1 transfer.
    // A frozen AXI snapshot below is independent of continuously published data.
    reg stats_tracking,stats_bad,pub_seen,pub_good;
    reg [31:0] area_acc,bright_acc,total_acc;
    reg [31:0] pub_area,pub_bright,pub_total,pub_frame,pub_epoch,pub_config;
    reg [RW-1:0] pub_roi;
    reg signed [8:0] pub_brightness,snapshot_brightness;
    reg signed [2:0] pub_gain_shift,snapshot_gain_shift;
    reg snapshot_seen,snapshot_good;
    reg [31:0] snapshot_area,snapshot_bright,snapshot_total;
    reg [31:0] snapshot_frame,snapshot_epoch,snapshot_config;
    reg [RW-1:0] snapshot_roi;
    wire stats_start=metadata1[3];
    wire [31:0] total_next=(stats_start ? 32'd0 : total_acc)+32'd1;
    wire [31:0] area_next=(stats_start ? 32'd0 : area_acc)+{31'd0,inside1};
    wire [31:0] bright_next=(stats_start ? 32'd0 : bright_acc)+
                            {31'd0,(inside1 && gray1>=threshold1)};
    wire bad_next=bad1 || (!stats_start && stats_bad);
    always @(posedge aclk) begin
        if(!aresetn) begin
            stats_tracking<=0;stats_bad<=0;area_acc<=0;bright_acc<=0;total_acc<=0;
            pub_seen<=0;pub_good<=0;pub_area<=0;pub_bright<=0;pub_total<=0;
            pub_frame<=0;pub_epoch<=0;pub_config<=0;pub_roi<=ROI_RESET;
            pub_brightness<=0;pub_gain_shift<=0;
        end else if(valid1 && ready2) begin
            if(stats_start)stats_tracking<=1;
            if(stats_tracking || stats_start) begin
                area_acc<=area_next;bright_acc<=bright_next;total_acc<=total_next;stats_bad<=bad_next;
                if(end1) begin
                    stats_tracking<=0;pub_seen<=1;
                    pub_good<=!bad_next && total_next==VIDEO_WIDTH*VIDEO_HEIGHT;
                    pub_area<=area_next;pub_bright<=bright_next;pub_total<=total_next;
                    pub_frame<=frame1;pub_epoch<=epoch1;pub_roi<=roi1;
                    pub_config<={15'd0,roi1[RW-1],threshold1,5'd0,frame_mode1};
                    pub_brightness<=brightness1;pub_gain_shift<=gain_shift1;
                end
            end
        end
    end

    reg aw_pending, w_pending;
    reg [15:0] awaddr;
    reg [31:0] wdata;
    reg [3:0] wstrb;
    assign s_axi_awready = aresetn && !aw_pending && !s_axi_bvalid;
    assign s_axi_wready = aresetn && !w_pending && !s_axi_bvalid;
    assign s_axi_arready = aresetn && !s_axi_rvalid;
    always @(posedge aclk) begin
        if(!aresetn) begin
            shadow_mode<=0; pending_mode<=0; active_mode<=0;
            shadow_threshold<=128; pending_threshold<=128; active_threshold<=128;
            shadow_brightness<=0;pending_brightness<=0;active_brightness<=0;
            shadow_gain_shift<=0;pending_gain_shift<=0;active_gain_shift<=0;
            cfg_pending<=0; seen_sof<=0;
            frame_sequence<=0; pixel_count<=0; apply_sequence<=0;
            shadow_roi<=ROI_RESET;pending_roi<=ROI_RESET;active_roi<=ROI_RESET;next_x<=0;next_y<=0;
            snapshot_seen<=0;snapshot_good<=0;snapshot_area<=0;snapshot_bright<=0;snapshot_total<=0;
            snapshot_frame<=0;snapshot_epoch<=0;snapshot_config<=0;snapshot_roi<=ROI_RESET;
            snapshot_brightness<=0;snapshot_gain_shift<=0;
            aw_pending<=0; w_pending<=0; awaddr<=0; wdata<=0; wstrb<=0;
            s_axi_bresp<=0; s_axi_bvalid<=0;
            s_axi_rdata<=0; s_axi_rresp<=0; s_axi_rvalid<=0;
        end else begin
            if(input_accept) begin
                pixel_count<=pixel_count+1'b1;
                if(s_axis_video_tlast) begin
                    next_x<=0;
                    next_y<=input_y<HEIGHT_VALUE ? input_y+1'b1 : HEIGHT_VALUE;
                end else begin
                    next_x<=input_x<WIDTH_VALUE ? input_x+1'b1 : WIDTH_VALUE;
                    next_y<=input_y;
                end
            end
            if(input_sof) begin
                seen_sof<=1; frame_sequence<=frame_sequence+1'b1;
                if(cfg_pending) begin
                    active_mode<=pending_mode; active_threshold<=pending_threshold;
                    active_roi<=pending_roi;
                    active_brightness<=pending_brightness;active_gain_shift<=pending_gain_shift;
                    cfg_pending<=0; apply_sequence<=apply_sequence+1'b1;
                end
            end
            if(s_axi_bvalid && s_axi_bready)s_axi_bvalid<=0;
            if(s_axi_awvalid && s_axi_awready)begin awaddr<=s_axi_awaddr; aw_pending<=1; end
            if(s_axi_wvalid && s_axi_wready)begin wdata<=s_axi_wdata; wstrb<=s_axi_wstrb; w_pending<=1; end
            if(aw_pending && w_pending && !s_axi_bvalid) begin
                aw_pending<=0; w_pending<=0; s_axi_bvalid<=1; s_axi_bresp<=2'b10;
                // Full-word configuration writes only; all other writes SLVERR.
                if(wstrb==4'hf)case(awaddr)
                    16'h0004: if(wdata<=5)begin shadow_mode<=wdata[2:0]; s_axi_bresp<=0; end
                    16'h0008: if(wdata<=255)begin shadow_threshold<=wdata[7:0]; s_axi_bresp<=0; end
                    16'h000c: if(wdata==1 && !cfg_pending &&
                        shadow_roi[0+:XW]<shadow_roi[XW+YW+:XW] &&
                        shadow_roi[XW+:YW]<shadow_roi[2*XW+YW+:YW])begin
                        pending_mode<=shadow_mode; pending_threshold<=shadow_threshold;
                        pending_roi<=shadow_roi;
                        pending_brightness<=shadow_brightness;pending_gain_shift<=shadow_gain_shift;
                        cfg_pending<=1; s_axi_bresp<=0;
                    end
                    16'h0030: if(wdata[15:0]<=VIDEO_WIDTH && wdata[31:16]<=VIDEO_HEIGHT) begin
                        shadow_roi[0+:XW]<=wdata[XW-1:0];shadow_roi[XW+:YW]<=wdata[16+:YW];s_axi_bresp<=0;
                    end
                    16'h0034: if(wdata[15:0]<=VIDEO_WIDTH && wdata[31:16]<=VIDEO_HEIGHT) begin
                        shadow_roi[XW+YW+:XW]<=wdata[XW-1:0];shadow_roi[2*XW+YW+:YW]<=wdata[16+:YW];s_axi_bresp<=0;
                    end
                    16'h0038: if(wdata<=1)begin shadow_roi[RW-1]<=wdata[0];s_axi_bresp<=0;end
                    16'h0060: if(wdata==1 && pub_seen)begin
                        snapshot_seen<=1;snapshot_good<=pub_good;snapshot_area<=pub_area;
                        snapshot_bright<=pub_bright;snapshot_total<=pub_total;snapshot_roi<=pub_roi;
                        snapshot_frame<=pub_frame;snapshot_epoch<=pub_epoch;snapshot_config<=pub_config;
                        snapshot_brightness<=pub_brightness;snapshot_gain_shift<=pub_gain_shift;
                        s_axi_bresp<=0;
                    end
                    // Canonical sign-extended 32-bit two's complement only.
                    16'h0088: if($signed(wdata)>=-32'sd255 && $signed(wdata)<=32'sd255)begin
                        shadow_brightness<=wdata[8:0];s_axi_bresp<=0;
                    end
                    16'h008c: if($signed(wdata)>=-32'sd2 && $signed(wdata)<=32'sd2)begin
                        shadow_gain_shift<=wdata[2:0];s_axi_bresp<=0;
                    end
                    default: s_axi_bresp<=2'b10;
                endcase
            end
            // A COMMIT executed on the same edge as SOF waits for the next SOF.
            // A previously pending COMMIT applies on this SOF; another COMMIT
            // on the same edge is rejected (old cfg_pending is still 1).
            if(s_axi_rvalid && s_axi_rready)s_axi_rvalid<=0;
            if(s_axi_arvalid && s_axi_arready) begin
                s_axi_rdata<=0; s_axi_rresp<=0; s_axi_rvalid<=1;
                case(s_axi_araddr)
                    16'h0000: s_axi_rdata<=ID;
                    16'h0004: s_axi_rdata<={29'd0,shadow_mode};
                    16'h0008: s_axi_rdata<={24'd0,shadow_threshold};
                    16'h000c: s_axi_rdata<=0;
                    16'h0010: s_axi_rdata<={16'd0,active_threshold,5'd0,active_mode};
                    16'h0014: s_axi_rdata<={16'd0,pending_threshold,5'd0,pending_mode};
                    16'h0018: s_axi_rdata<={29'd0,(valid0|valid1|valid2),seen_sof,cfg_pending};
                    16'h001c: s_axi_rdata<=frame_sequence;
                    16'h0020: s_axi_rdata<=pixel_count;
                    16'h0024: s_axi_rdata<=3;
                    16'h0028: s_axi_rdata<=GEOMETRY;
                    16'h002c: s_axi_rdata<=apply_sequence;
                    16'h0030: s_axi_rdata<=roi_xy0(shadow_roi);
                    16'h0034: s_axi_rdata<=roi_xy1(shadow_roi);
                    16'h0038: s_axi_rdata<={31'd0,shadow_roi[RW-1]};
                    16'h003c: s_axi_rdata<=roi_xy0(active_roi);
                    16'h0040: s_axi_rdata<=roi_xy1(active_roi);
                    16'h0044: s_axi_rdata<={31'd0,active_roi[RW-1]};
                    16'h0048: s_axi_rdata<=roi_xy0(pending_roi);
                    16'h004c: s_axi_rdata<=roi_xy1(pending_roi);
                    16'h0050: s_axi_rdata<={31'd0,pending_roi[RW-1]};
                    16'h0054: s_axi_rdata<=31; // ROI, frozen stats, brightness, invert, gain.
                    16'h0058: s_axi_rdata<={30'd0,pub_good,pub_seen}; // Poll available before SNAPSHOT.
                    16'h0060: s_axi_rdata<=0;
                    16'h0064: s_axi_rdata<={28'd0,(snapshot_seen && !snapshot_good),
                        snapshot_roi[RW-1],snapshot_good,snapshot_seen};
                    16'h0068: s_axi_rdata<=snapshot_frame;
                    16'h006c: s_axi_rdata<=snapshot_epoch;
                    16'h0070: s_axi_rdata<=snapshot_area;
                    16'h0074: s_axi_rdata<=snapshot_bright;
                    16'h0078: s_axi_rdata<=snapshot_total;
                    16'h007c: s_axi_rdata<=snapshot_config;
                    16'h0080: s_axi_rdata<=roi_xy0(snapshot_roi);
                    16'h0084: s_axi_rdata<=roi_xy1(snapshot_roi);
                    16'h0088: s_axi_rdata<={{23{shadow_brightness[8]}},shadow_brightness};
                    16'h008c: s_axi_rdata<={{29{shadow_gain_shift[2]}},shadow_gain_shift};
                    16'h0090: s_axi_rdata<={{23{active_brightness[8]}},active_brightness};
                    16'h0094: s_axi_rdata<={{29{active_gain_shift[2]}},active_gain_shift};
                    16'h0098: s_axi_rdata<={{23{pending_brightness[8]}},pending_brightness};
                    16'h009c: s_axi_rdata<={{29{pending_gain_shift[2]}},pending_gain_shift};
                    16'h00a0: s_axi_rdata<={{23{snapshot_brightness[8]}},snapshot_brightness};
                    16'h00a4: s_axi_rdata<={{29{snapshot_gain_shift[2]}},snapshot_gain_shift};
                    default: s_axi_rresp<=2'b10;
                endcase
            end
        end
    end
endmodule
