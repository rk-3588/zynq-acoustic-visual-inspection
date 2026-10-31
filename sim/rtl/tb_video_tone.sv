`timescale 1ns/1ps
module tb_video_tone;
    reg aclk=0;
    always #3.333333 aclk=~aclk;
    reg aresetn=0;
    reg [15:0] s_axi_awaddr=0,s_axi_araddr=0;
    reg [2:0] s_axi_awprot=0,s_axi_arprot=0;
    reg s_axi_awvalid=0,s_axi_wvalid=0,s_axi_bready=0,s_axi_arvalid=0,s_axi_rready=0;
    wire s_axi_awready,s_axi_wready,s_axi_bvalid,s_axi_arready,s_axi_rvalid;
    reg [31:0] s_axi_wdata=0;
    reg [3:0] s_axi_wstrb=0;
    wire [1:0] s_axi_bresp,s_axi_rresp;
    wire [31:0] s_axi_rdata;
    reg [23:0] s_axis_video_tdata=0;
    reg [2:0] s_axis_video_tkeep=7,s_axis_video_tstrb=7;
    reg [0:0] s_axis_video_tuser=0,s_axis_video_tid=0,s_axis_video_tdest=0;
    reg s_axis_video_tlast=0,s_axis_video_tvalid=0;
    wire s_axis_video_tready;
    wire [23:0] m_axis_video_tdata;
    wire [2:0] m_axis_video_tkeep,m_axis_video_tstrb;
    wire [0:0] m_axis_video_tuser,m_axis_video_tid,m_axis_video_tdest;
    wire m_axis_video_tlast,m_axis_video_tvalid;
    reg m_axis_video_tready=0;
    video_preprocess #(.VIDEO_WIDTH(8),.VIDEO_HEIGHT(4)) dut(.*);

    reg random_stalls=0,force_stall=0;
    reg [15:0] lfsr=16'h1ace;
    always @(negedge aclk) begin
        lfsr={lfsr[14:0],lfsr[15]^lfsr[13]^lfsr[12]^lfsr[10]};
        m_axis_video_tready=aresetn && !force_stall && (!random_stalls || lfsr[2:0]<5);
    end
    reg [33:0] expected[0:31];
    integer accepted=0,delivered=0,cycles=0,checked=0;
    integer model_mode=0,model_threshold=128,queued_mode=0,queued_threshold=128;
    integer model_brightness=0,model_gain_shift=0,queued_brightness=0,queued_gain_shift=0;
    reg model_pending=0;
    integer model_x=0,model_y=0,model_frame=0,model_epoch=0;
    integer model_x0=0,model_y0=0,model_x1=8,model_y1=4;
    integer queued_x0=0,queued_y0=0,queued_x1=8,queued_y1=4;
    reg model_roi=0,queued_roi=0;
    reg held_video=0,held_b=0,held_r=0;
    reg [33:0] last_video;
    reg [1:0] last_b,last_rresp;
    reg [31:0] last_rdata;
    function automatic [23:0] reference_pixel(input [23:0] pixel,input integer mode,input integer threshold);
        integer y,r,g,b;
        begin
            r=pixel[23:16];g=pixel[15:8];b=pixel[7:0];
            y=(77*r+150*g+29*b+128)/256;
            case(mode)
                1:reference_pixel={y[7:0],y[7:0],y[7:0]};
                2:reference_pixel=y>=threshold?24'hffffff:24'h000000;
                3:begin
                    r=r+model_brightness;g=g+model_brightness;b=b+model_brightness;
                    if(r<0)r=0;else if(r>255)r=255;
                    if(g<0)g=0;else if(g>255)g=255;
                    if(b<0)b=0;else if(b>255)b=255;
                    reference_pixel={r[7:0],g[7:0],b[7:0]};
                end
                4:reference_pixel=24'hffffff^pixel;
                5:begin
                    if(model_gain_shift<0)begin
                        r=r/(1<<(-model_gain_shift));g=g/(1<<(-model_gain_shift));b=b/(1<<(-model_gain_shift));
                    end else begin
                        r=r*(1<<model_gain_shift);g=g*(1<<model_gain_shift);b=b*(1<<model_gain_shift);
                    end
                    if(r>255)r=255;if(g>255)g=255;if(b>255)b=255;
                    reference_pixel={r[7:0],g[7:0],b[7:0]};
                end
                default:reference_pixel=pixel;
            endcase
        end
    endfunction
    wire [33:0] actual={m_axis_video_tdata,m_axis_video_tkeep,m_axis_video_tstrb,
                       m_axis_video_tuser,m_axis_video_tlast,m_axis_video_tid,m_axis_video_tdest};
    always @(posedge aclk) begin
        cycles=cycles+1;
        if(!aresetn) begin
            accepted=0;delivered=0;model_mode=0;model_threshold=128;model_pending=0;
            model_brightness=0;model_gain_shift=0;
            model_x=0;model_y=0;model_frame=0;model_epoch=0;
            model_roi=0;model_x0=0;model_y0=0;model_x1=8;model_y1=4;
            held_video=0;held_b=0;held_r=0;
        end else begin
            if(held_video && (!m_axis_video_tvalid || actual!==last_video))
                $fatal(1,"Output changed under backpressure");
            if(held_b && (!s_axi_bvalid || s_axi_bresp!==last_b))$fatal(1,"B response changed while stalled");
            if(held_r && (!s_axi_rvalid || s_axi_rdata!==last_rdata || s_axi_rresp!==last_rresp))
                $fatal(1,"R response changed while stalled");
            held_video=m_axis_video_tvalid && !m_axis_video_tready;last_video=actual;
            held_b=s_axi_bvalid && !s_axi_bready;last_b=s_axi_bresp;
            held_r=s_axi_rvalid && !s_axi_rready;last_rdata=s_axi_rdata;last_rresp=s_axi_rresp;
            if(m_axis_video_tvalid && m_axis_video_tready) begin
                if(delivered>=accepted)$fatal(1,"Unexpected/duplicated output");
                if(actual!==expected[delivered%32])
                    $fatal(1,"Pixel/sideband mismatch index=%0d got=%h expected=%h",delivered,actual,expected[delivered%32]);
                delivered=delivered+1;checked=checked+1;
            end
            if(s_axis_video_tvalid && s_axis_video_tready) begin
                if(s_axis_video_tuser)begin model_x=0;model_y=0;model_frame=model_frame+1;end
                if(s_axis_video_tuser && model_pending) begin
                    model_mode=queued_mode;model_threshold=queued_threshold;model_pending=0;
                    model_roi=queued_roi;model_x0=queued_x0;model_y0=queued_y0;
                    model_x1=queued_x1;model_y1=queued_y1;model_epoch=model_epoch+1;
                    model_brightness=queued_brightness;model_gain_shift=queued_gain_shift;
                end
                expected[accepted%32]={reference_pixel(s_axis_video_tdata,
                    (!model_roi || (model_x>=model_x0 && model_x<model_x1 && model_y>=model_y0 && model_y<model_y1)) ? model_mode : 0,model_threshold),
                    s_axis_video_tkeep,s_axis_video_tstrb,s_axis_video_tuser,s_axis_video_tlast,
                    s_axis_video_tid,s_axis_video_tdest};
                accepted=accepted+1;
                if(s_axis_video_tlast)begin model_x=0;model_y=model_y+1;end
                else model_x=model_x+1;
            end
            if(accepted-delivered>3)$fatal(1,"Elastic capacity exceeded");
        end
    end

    task automatic idle_source;
        @(negedge aclk);s_axis_video_tvalid=0;
    endtask
    task automatic pixel(input [23:0] rgb,input bit sof,input bit eol,input bit bubble,input bit arbitrary_mask);
        if(bubble) begin
            @(negedge aclk);s_axis_video_tvalid=0;
            repeat(2)@(negedge aclk);
        end
        @(negedge aclk);
        s_axis_video_tdata=rgb;s_axis_video_tuser=sof;s_axis_video_tlast=eol;
        s_axis_video_tid=rgb[0];s_axis_video_tdest=rgb[1];
        s_axis_video_tkeep=arbitrary_mask?rgb[5:3]:3'b111;
        s_axis_video_tstrb=arbitrary_mask?rgb[8:6]:3'b111;
        s_axis_video_tvalid=1;
        do @(posedge aclk); while(!s_axis_video_tready);
    endtask
    task automatic frame(input integer width,input integer height,input bit bubbles,input bit masks);
        reg [23:0] rgb;
        for(integer y=0;y<height;y=y+1)for(integer x=0;x<width;x=x+1) begin
            rgb={8'((x*17+y*3)%256),8'((x*7+y*23)%256),8'((x*11+y*5)%256)};
            pixel(rgb,x==0 && y==0,x==width-1,bubbles && ((x+y)%11==0),masks);
        end
        idle_source();
    endtask
    task automatic drain;
        wait(delivered==accepted);
        repeat(5)@(negedge aclk);
        if(m_axis_video_tvalid)$fatal(1,"Extra output after drain");
    endtask
    task automatic write_reg(input [15:0] addr,input [31:0] value,input integer order,
                             input [3:0] strobe,input [1:0] response);
        fork
            begin
                if(order==1)repeat(4)@(negedge aclk);
                @(negedge aclk);s_axi_awaddr=addr;s_axi_awvalid=1;
                do @(posedge aclk);while(!s_axi_awready);
                @(negedge aclk);s_axi_awvalid=0;
            end
            begin
                if(order==0)repeat(4)@(negedge aclk);
                @(negedge aclk);s_axi_wdata=value;s_axi_wstrb=strobe;s_axi_wvalid=1;
                do @(posedge aclk);while(!s_axi_wready);
                @(negedge aclk);s_axi_wvalid=0;
            end
        join
        wait(s_axi_bvalid);
        repeat(4)@(negedge aclk);
        if(s_axi_bresp!==response)$fatal(1,"Write response addr=%h got=%h expected=%h",addr,s_axi_bresp,response);
        s_axi_bready=1;
        @(posedge aclk);@(negedge aclk);s_axi_bready=0;
    endtask
    task automatic read_reg(input [15:0] addr,input [31:0] value,input [1:0] response);
        @(negedge aclk);s_axi_araddr=addr;s_axi_arvalid=1;
        do @(posedge aclk);while(!s_axi_arready);
        @(negedge aclk);s_axi_arvalid=0;
        wait(s_axi_rvalid);
        repeat(4)@(negedge aclk);
        if(s_axi_rresp!==response || s_axi_rdata!==value)
            $fatal(1,"Read addr=%h got=%h/%h expected=%h/%h",addr,s_axi_rdata,s_axi_rresp,value,response);
        s_axi_rready=1;
        @(posedge aclk);@(negedge aclk);s_axi_rready=0;
    endtask
    task automatic configure(input integer mode,input integer threshold);
        write_reg(16'h4,mode,0,15,0);
        write_reg(16'h8,threshold,1,15,0);
        write_reg(16'hc,1,2,15,0);
        queued_mode=mode;queued_threshold=threshold;model_pending=1;
    endtask
    task automatic reset_dut;
        @(negedge aclk);aresetn=0;s_axis_video_tvalid=0;
        s_axi_awvalid=0;s_axi_wvalid=0;s_axi_arvalid=0;s_axi_bready=0;s_axi_rready=0;
        repeat(4)@(negedge aclk);
        aresetn=1;
        repeat(3)@(negedge aclk);
    endtask

    task automatic roi_config(input integer mode,input integer threshold,input bit enable,
                              input integer x0,input integer y0,input integer x1,input integer y1);
        write_reg('h30,(y0<<16)|x0,0,15,0);
        write_reg('h34,(y1<<16)|x1,1,15,0);
        write_reg('h38,enable,2,15,0);
        queued_roi=enable;queued_x0=x0;queued_y0=y0;queued_x1=x1;queued_y1=y1;
        configure(mode,threshold);
    endtask
    function automatic [23:0] pattern(input integer x,input integer y);
        pattern={8'((x*17+y*3)%256),8'((x*7+y*23)%256),8'((x*11+y*5)%256)};
    endfunction
    function automatic integer bright_count(input integer x0,input integer y0,input integer x1,input integer y1,input integer threshold);
        integer r,g,b,yv;
        bright_count=0;
        for(integer y=y0;y<y1;y++)for(integer x=x0;x<x1;x++)begin
            r=(x*17+y*3)%256;g=(x*7+y*23)%256;b=(x*11+y*5)%256;
            yv=(77*r+150*g+29*b+128)/256;
            if(yv>=threshold)bright_count=bright_count+1;
        end
    endfunction
    task automatic check_stats(input integer status,input integer area,input integer bright,input integer total);
        write_reg('h60,1,2,15,0);
        read_reg('h64,status,0);read_reg('h68,model_frame,0);read_reg('h6c,model_epoch,0);
        read_reg('h70,area,0);read_reg('h74,bright,0);read_reg('h78,total,0);
        read_reg('h7c,(model_roi<<16)|(model_threshold<<8)|model_mode,0);
        read_reg('h80,(model_y0<<16)|model_x0,0);read_reg('h84,(model_y1<<16)|model_x1,0);
        read_reg('ha0,model_brightness,0);read_reg('ha4,model_gain_shift,0);
    endtask
    task automatic tone_config(input integer mode,input integer brightness,input integer gain,
                               input bit enable,input integer x0,input integer y0,input integer x1,input integer y1);
        write_reg('h88,brightness,0,15,0);write_reg('h8c,gain,1,15,0);
        queued_brightness=brightness;queued_gain_shift=gain;
        roi_config(mode,64,enable,x0,y0,x1,y1);
    endtask
    integer frozen_frame,frozen_epoch,frozen_brightness,frozen_gain;
    integer tone_pixels=0;
    initial begin
        reset_dut();random_stalls=1;
        read_reg('h24,3,0);read_reg('h28,(4<<16)|8,0);read_reg('h54,31,0);
        read_reg('h3c,0,0);read_reg('h40,(4<<16)|8,0);read_reg('h44,0,0);
        read_reg('h58,0,0);write_reg('h60,1,0,15,2);read_reg('h64,0,0); // No completed frame yet.
        frame(8,4,1,0);drain();check_stats(3,32,bright_count(0,0,8,4,128),32);

        // Canonical 32-bit signed range, partial writes and read-only protection.
        write_reg('h88,-256,0,15,2);write_reg('h88,256,1,15,2);
        write_reg('h88,32'h1ff,2,15,2);write_reg('h88,32'h80000000,0,15,2);
        write_reg('h8c,-3,1,15,2);write_reg('h8c,3,2,15,2);
        write_reg('h8c,7,0,15,2);write_reg('h8c,32'h7fffffff,1,15,2);
        write_reg('h88,-1,2,1,2);write_reg('h8c,1,0,1,2);
        write_reg('h90,0,1,15,2);write_reg('h94,0,2,15,2);
        write_reg('h98,0,0,15,2);write_reg('h9c,0,1,15,2);
        write_reg('ha0,0,2,15,2);write_reg('ha4,0,0,15,2);
        read_reg('h89,0,2);read_reg('ha8,0,2);
        write_reg('h4,7,0,15,2);write_reg('h4,32'hffffffff,1,15,2);
        read_reg('h88,0,0);read_reg('h8c,0,0);

        // Every signed brightness setting and every eight-bit input in each RGB
        // lane. Unequal RGB catches byte swapping and accidental gray processing.
        for(integer bias=-255;bias<=255;bias++)begin
            tone_config(3,bias,0,0,0,0,8,4);
            for(integer v=0;v<256;v++)begin
                pixel({8'(v),8'(255-v),8'(73*v)},v%32==0,v%8==7,v%19==0,0);
                tone_pixels++;
            end
            idle_source();drain();read_reg('h90,bias,0);read_reg('h94,0,0);
        end
        for(integer gain=-2;gain<=2;gain++)begin
            tone_config(5,0,gain,0,0,0,8,4);
            for(integer v=0;v<256;v++)begin
                pixel({8'(v),8'(255-v),8'(73*v)},v%32==0,v%8==7,v%13==0,0);
                tone_pixels++;
            end
            idle_source();drain();read_reg('h94,gain,0);
        end
        tone_config(4,255,2,0,0,0,8,4); // Inversion ignores inactive mode parameters.
        for(integer v=0;v<256;v++)begin
            pixel({8'(v),8'(255-v),8'(73*v)},v%32==0,v%8==7,v%11==0,0);
            tone_pixels++;
        end
        idle_source();drain();

        // Every mode preserves outside ROI, and stats always count original RGB.
        for(integer mode=0;mode<=5;mode++)begin
            tone_config(mode,-173,-2,1,2,1,6,3);
            frame(8,4,1,0);drain();check_stats(7,8,bright_count(2,1,6,3,64),32);
        end
        tone_config(3,255,2,1,7,3,8,4);frame(8,4,1,0);drain();
        check_stats(7,1,bright_count(7,3,8,4,64),32);
        tone_config(5,0,2,1,0,0,1,1);frame(8,4,1,0);drain();
        check_stats(7,1,bright_count(0,0,1,1,64),32);

        // Mid-frame commit and overwrite shadows: pending mode/ROI/tone fields
        // stay isolated, old frame finishes with old epoch, SOF gets new fields.
        tone_config(3,77,-1,1,1,0,7,4);frame(8,4,1,0);drain();
        check_stats(7,24,bright_count(1,0,7,4,64),32);
        frozen_frame=model_frame;frozen_epoch=model_epoch;
        frozen_brightness=model_brightness;frozen_gain=model_gain_shift;
        for(integer x=0;x<8;x++)pixel(pattern(x,0),x==0,x==7,0,0);
        idle_source();tone_config(5,-255,2,1,0,0,2,2);
        write_reg('h88,255,0,15,0);write_reg('h8c,-2,1,15,0);write_reg('h4,4,2,15,0);
        read_reg('h98,-255,0);read_reg('h9c,2,0);write_reg('hc,1,2,15,2);
        for(integer y=1;y<4;y++)for(integer x=0;x<8;x++)pixel(pattern(x,y),0,x==7,1,0);
        idle_source();drain();read_reg('h90,77,0);read_reg('h94,-1,0);
        read_reg('h68,frozen_frame,0);read_reg('ha0,frozen_brightness,0);read_reg('ha4,frozen_gain,0);
        check_stats(7,24,bright_count(1,0,7,4,64),32);
        frozen_frame=model_frame;frozen_epoch=model_epoch;
        frame(8,4,1,0);drain();read_reg('h90,-255,0);read_reg('h94,2,0);
        read_reg('h68,frozen_frame,0);read_reg('ha0,77,0);read_reg('ha4,-1,0);
        check_stats(7,4,bright_count(0,0,2,2,64),32);

        // Newly executed COMMIT on the SOF edge must NOT affect this frame,
        // including its signed tone fields and snapshot tags.
        random_stalls=0;
        write_reg('h4,3,0,15,0);write_reg('h88,50,1,15,0);write_reg('h8c,-1,2,15,0);
        write_reg('h30,0,0,15,0);write_reg('h34,(4<<16)|8,1,15,0);write_reg('h38,0,2,15,0);
        fork
            write_reg('hc,1,2,15,0);
            begin
                wait(dut.aw_pending && dut.w_pending);
                pixel(pattern(0,0),1,0,0,0);idle_source();
            end
        join
        queued_mode=3;queued_threshold=64;queued_brightness=50;queued_gain_shift=-1;
        queued_roi=0;queued_x0=0;queued_y0=0;queued_x1=8;queued_y1=4;model_pending=1;
        for(integer x=1;x<8;x++)pixel(pattern(x,0),0,x==7,0,0);
        for(integer y=1;y<4;y++)for(integer x=0;x<8;x++)pixel(pattern(x,y),0,x==7,0,0);
        idle_source();drain();read_reg('h90,-255,0);read_reg('h94,2,0);
        check_stats(7,4,bright_count(0,0,2,2,64),32);
        frame(8,4,0,0);drain();read_reg('h90,50,0);read_reg('h94,-1,0);
        check_stats(3,32,bright_count(0,0,8,4,64),32);random_stalls=1;

        // Masks remain bit-exact; incomplete pixels invalidate stats, not video.
        frame(8,4,1,1);drain();write_reg('h60,1,2,15,0);read_reg('h64,9,0);
        tone_config(3,-255,-2,1,1,1,7,4);force_stall=1;
        pixel(24'h123456,1,0,0,0);pixel(24'h654321,0,0,0,0);pixel(24'hffffff,0,0,0,0);
        idle_source();repeat(8)@(negedge aclk);
        if(s_axis_video_tready)$fatal(1,"Full tone pipeline did not stall input");
        reset_dut();force_stall=0;
        read_reg('h44,0,0);read_reg('h3c,0,0);read_reg('h40,(4<<16)|8,0);
        read_reg('h64,0,0);read_reg('h2c,0,0);
        read_reg('h88,0,0);read_reg('h8c,0,0);read_reg('h90,0,0);read_reg('h94,0,0);
        read_reg('h98,0,0);read_reg('h9c,0,0);read_reg('ha0,0,0);read_reg('ha4,0,0);
        frame(8,4,1,0);drain();check_stats(3,32,bright_count(0,0,8,4,128),32);
        $display("PASS_VIDEO_TONE_EXHAUSTIVE_SIGNED_SATURATION_GAIN_INVERT_ROI_RAW_STATS_TAGS_RESET pixels=%0d checked=%0d",tone_pixels,checked);
        $finish;
    end
    initial begin #100000000;$fatal(1,"Simulation watchdog");end
endmodule
