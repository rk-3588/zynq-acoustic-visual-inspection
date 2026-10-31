`timescale 1ns/1ps
module tb_acoustic_capture;
    reg axi=0,clk=0;always #5 axi=~axi;always #2.4 clk=~clk;
    reg resetn=0,rst=1;
    reg [15:0] awaddr=0,araddr=0;
    reg awvalid=0,wvalid=0,bready=0,arvalid=0,rready=0;
    reg [31:0] wdata=0;reg [3:0] wstrb=15;
    wire awready,wready,bvalid,arready,rvalid;wire [1:0] bresp,rresp;wire [31:0] rdata;
    reg [383:0] data=0;reg valid=0;integer tick=0,sample_number=0;
    integer expected_count=0;reg record=0;integer expected_channel=0;
    reg [31:0] expected[0:2047];
    acoustic_capture dut(.s_axi_aclk(axi),.s_axi_aresetn(resetn),
        .s_axi_awaddr(awaddr),.s_axi_awprot(3'b0),.s_axi_awvalid(awvalid),.s_axi_awready(awready),
        .s_axi_wdata(wdata),.s_axi_wstrb(wstrb),.s_axi_wvalid(wvalid),.s_axi_wready(wready),
        .s_axi_bresp(bresp),.s_axi_bvalid(bvalid),.s_axi_bready(bready),
        .s_axi_araddr(araddr),.s_axi_arprot(3'b0),.s_axi_arvalid(arvalid),.s_axi_arready(arready),
        .s_axi_rdata(rdata),.s_axi_rresp(rresp),.s_axi_rvalid(rvalid),.s_axi_rready(rready),
        .sample_clk(clk),.sample_rst(rst),.sample_data(data),.sample_valid(valid));
    // Distinct channels, negative values, unrelated clock periods.
    always @(negedge clk) begin
        valid=0;
        if(!rst) begin
            tick=tick+1;
            if(tick%17==0) begin
                for(integer lane=0;lane<16;lane=lane+1) begin
                    if(sample_number%5==0)data[lane*24+:24]=24'h7fffff;
                    else if(sample_number%5==1)data[lane*24+:24]=24'h800000;
                    else data[lane*24+:24]=-500000+lane*10000+sample_number;
                end
                sample_number=sample_number+1;valid=1;
            end
        end
    end
    always @(posedge clk) if(record && valid && dut.capturing && expected_count<2048) begin
        expected[expected_count]={{8{data[expected_channel*24+23]}},data[expected_channel*24+:24]};
        expected_count=expected_count+1;
    end
    task write_reg(input [15:0] a,input [31:0] v,input integer reverse,input [1:0] response);
        begin
            @(negedge axi);bready=0;awaddr=a;wdata=v;
            if(reverse) begin
                wvalid=1;do @(posedge axi);while(!wready);
                @(negedge axi);wvalid=0;repeat(3)@(negedge axi);awvalid=1;
                do @(posedge axi);while(!awready);
                @(negedge axi);awvalid=0;
            end else begin
                awvalid=1;do @(posedge axi);while(!awready);
                @(negedge axi);awvalid=0;repeat(3)@(negedge axi);wvalid=1;
                do @(posedge axi);while(!wready);
                @(negedge axi);wvalid=0;
            end
            wait(bvalid);if(bresp!==response)$fatal(1,"BRESP a=%h actual=%h expected=%h",a,bresp,response);
            repeat(3)@(negedge axi);
            if(!bvalid || bresp!==response)$fatal(1,"unstable write response");
            bready=1;@(posedge axi);@(negedge axi);bready=0;
        end
    endtask
    task read_reg(input [15:0] a,output [31:0] v,input [1:0] response);
        reg [31:0] saved;
        begin
            @(negedge axi);rready=0;araddr=a;arvalid=1;
            do @(posedge axi);while(!arready);
            @(negedge axi);arvalid=0;
            wait(rvalid);saved=rdata;
            if(rresp!==response)$fatal(1,"RRESP a=%h actual=%h",a,rresp);
            repeat(3)@(negedge axi);
            if(!rvalid || rdata!==saved || rresp!==response)$fatal(1,"unstable read response");
            v=saved;rready=1;@(posedge axi);@(negedge axi);rready=0;
        end
    endtask
    reg [31:0] value,seq;
    initial begin
        repeat(8)@(negedge axi);resetn=1;@(negedge clk);rst=0;
        read_reg(0,value,0);if(value!==32'h41434631)$fatal(1,"ID");
        read_reg(16'h1000,value,2); // uninitialized data must not be exposed
        for(integer run=0;run<3;run=run+1) begin
            expected_count=0;expected_channel=run==0?5:run==1?15:0;record=1;
            write_reg(12,expected_channel,run,0);
            write_reg(4,1,run,0);
            write_reg(4,1,0,2); // no overwrite while busy
            write_reg(12,7,0,2);
            value=0;
            while(value!=2)read_reg(8,value,0);
            record=0;
            if(expected_count!=2048)$fatal(1,"capture count %d",expected_count);
            read_reg(24,seq,0);if(seq!==(run==2?1:run+1))$fatal(1,"sequence");
            write_reg(4,1,0,2); // frozen block cannot be overwritten
            for(integer i=0;i<2048;i=i+1) begin
                read_reg(16'h1000+4*i,value,0);
                if(value!==expected[i])$fatal(1,"sample run=%d index=%d actual=%h expected=%h",run,i,value,expected[i]);
            end
            read_reg(16'h1000,value,0);if(value!==expected[0])$fatal(1,"frozen block changed");
            read_reg(16'h1001,value,2);read_reg(16'h3000,value,2);
            write_reg(4,2,1,0);read_reg(8,value,0);if(value!==0)$fatal(1,"release");
            if(run==1) begin
                // Sample-only reset must reset the AXI request epoch too.
                @(negedge clk);rst=1;repeat(12)@(negedge axi);rst=0;
                repeat(12)@(negedge axi);
                read_reg(24,value,0);if(value!==0)$fatal(1,"sample reset epoch");
            end
        end
        wstrb=1;write_reg(4,1,0,2);wstrb=15;
        write_reg(12,16,0,2);write_reg(16'h1000,123,0,2);
        @(negedge axi);resetn=0;repeat(12)@(negedge axi);resetn=1;
        repeat(12)@(negedge axi);read_reg(8,value,0);if(value!==0)$fatal(1,"AXI reset state");
        read_reg(24,value,0);if(value!==0)$fatal(1,"AXI reset epoch");
        $display("PASS capture: asynchronous clocks, signed24, 2048 consecutive samples, channels5/15/0, frozen reads, AXI stalls/AW-W order, invalid access, independent reset inputs");
        $finish;
    end
    initial begin #2000000;$fatal(1,"simulation timeout");end
endmodule
