`timescale 1ns/1ps
module tb_acoustic_hpf_200;
    reg clk=0, rst=1;
    always #2.381 clk=~clk;
    reg [383:0] data_in=0;
    reg valid_in=0;
    reg [31:0] mode=0;
    wire [383:0] data_out;
    wire valid_out, ready, overrun;
`ifdef HPF_NETLIST
    system_acoustic_hpf_200_0_0 dut(.Aclk(clk),.rst(rst),.sample_in(data_in),
`else
    acoustic_hpf_200 dut(.Aclk(clk),.rst(rst),.sample_in(data_in),
`endif
        .sample_valid_in(valid_in),.mode_axi(mode),.sample_out(data_out),
        .sample_valid_out(valid_out),.sample_ready(ready),.input_overrun(overrun));
    reg [383:0] expected, last_output=0;
    integer cycle=0, accepted_cycle=0, accepted=0, checked=0;
    reg pending=0;
    always @(posedge clk) begin
        cycle=cycle+1;
        if (!rst && valid_in && ready) begin
            if (pending) $fatal(1,"FAIL: overlapping accepted groups");
            accepted_cycle=cycle;
            accepted=accepted+1;
            pending=1;
        end
        #0.1;
        if (rst) begin pending=0; last_output=data_out; end
        else begin
            if (valid_out) begin
                if (!pending || cycle-accepted_cycle!=6)
                    $fatal(1,"FAIL: valid latency cycle=%0d accepted=%0d",cycle,accepted_cycle);
                if (data_out !== expected)
                    $fatal(1,"FAIL: vector=%0d got=%096x expected=%096x",checked,data_out,expected);
                checked=checked+1;
                pending=0;
            end else if (data_out !== last_output)
                $fatal(1,"FAIL: output changed without valid");
            last_output=data_out;
        end
    end
    integer fd, count, code;
    reg [7:0] reset_flag,bypass_flag;
    reg [383:0] vector_in,vector_out;
    function automatic longint signed round_reference(input longint signed value,input integer bits);
        if(value>=0) round_reference=(value+(64'sd1<<(bits-1)))>>>bits;
        else round_reference=-(((-value)+(64'sd1<<(bits-1)))>>>bits);
    endfunction
    initial begin
`ifdef HPF_NETLIST
        #200; // Allow the vendor primitive model's 100-ns global reset.
`endif
`ifndef HPF_NETLIST
        // Exhaust every Q12 remainder for both signs, plus product half ties.
        for(integer q=-4;q<=4;q=q+1) begin
            for(integer r=0;r<4096;r=r+1) begin
                longint signed v;
                v=q*64'sd4096+r;
                if($signed(dut.round_output(v[37:0]))!=round_reference(v,12))
                    $fatal(1,"FAIL: output-round signed boundary");
            end
            for(integer r=-2;r<=2;r=r+1) begin
                longint signed v;
                v=q*64'sd1048576+64'sd524288+r;
                if($signed(dut.round_product(v[55:0]))!=round_reference(v,20))
                    $fatal(1,"FAIL: product-round signed half tie");
            end
        end
        if($signed(dut.round_output(38'sh1fffffffff))!=64'sd33554432 ||
           $signed(dut.round_output(38'sh2000000000))!= -64'sd33554432)
            $fatal(1,"FAIL: full-width output rounding");
        $display("PASS: signed rounding remainder and half-tie boundaries");
`endif
        repeat(8) @(negedge clk);
        rst=0;
        fd=$fopen("vectors.txt","r");
        if (!fd) $fatal(1,"FAIL: vectors file missing");
        count=0;
        while (!$feof(fd)) begin
            code=$fscanf(fd,"%h %h %h %h\n",reset_flag,bypass_flag,vector_in,vector_out);
            if (code!=4) begin
                if (!$feof(fd)) $fatal(1,"FAIL: vector format");
            end else begin
                if (reset_flag) begin
                    @(negedge clk); rst=1; valid_in=0;
                    repeat(5) @(negedge clk);
                    rst=0;
                end
                mode=bypass_flag ? 32'h00010000 : 0;
                repeat(3) @(negedge clk); // Allow mode's two-flop CDC.
                if (!ready) $fatal(1,"FAIL: not ready for spaced sample");
                data_in=vector_in; expected=vector_out; valid_in=1;
                @(negedge clk); valid_in=0;
                repeat(7) @(negedge clk);
                if (overrun) $fatal(1,"FAIL: unexpected input overrun");
                count=count+1;
            end
        end
        $fclose(fd);
        if (count!=checked || accepted!=checked) $fatal(1,"FAIL: output count mismatch");
        // Drop/reset an in-flight sample: no stale valid may escape reset.
        @(negedge clk); valid_in=1;
        @(negedge clk); valid_in=0; rst=1;
        repeat(8) @(negedge clk);
        rst=0;
        repeat(8) @(negedge clk);
        if (valid_out || pending || overrun) $fatal(1,"FAIL: reset flush");
        // A deliberately too-fast token must be flagged, not corrupt state.
        expected=0; mode=0; data_in=0;
        @(negedge clk); valid_in=1;
        repeat(2) @(negedge clk);
        valid_in=0;
        repeat(6) @(negedge clk);
        if (!overrun) $fatal(1,"FAIL: overrun was not flagged");
        $display("PASS: HPF200 %0d vectors x16 bit-exact, latency6, bypass tracking, signed saturation, reset flush, overrun flag",count);
        $finish;
    end
    initial begin #5000000; $fatal(1,"FAIL: HPF timeout"); end
endmodule
